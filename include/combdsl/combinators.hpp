/*
 * C++ combinator DSL
 * Copyright (C) 2026  David W. Gero
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <csignal>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace combdsl {

struct constant;
class quoted_expression;

template <class Expression>
class deferred_basis_expression;

template <class Expression, class... Arguments>
class basis_application_expression;

namespace detail {

template <class Value>
class deferred_combinator;

template <class Function, class Argument>
class substitution_function;

class symbolic_string_expression;

template <class Value>
using operand_t = std::remove_reference_t<Value>;

template <class Value>
using unqualified_operand_t =
    std::remove_cv_t<operand_t<Value>>;

template <class Value>
inline constexpr bool is_raw_string_operand_v =
    !std::is_volatile_v<operand_t<Value>> &&
    (std::same_as<unqualified_operand_t<Value>, std::string> ||
     std::same_as<unqualified_operand_t<Value>, std::string_view> ||
     (std::is_pointer_v<unqualified_operand_t<Value>> &&
      (std::same_as<
           std::remove_pointer_t<unqualified_operand_t<Value>>, char> ||
       std::same_as<
           std::remove_pointer_t<unqualified_operand_t<Value>>,
           const char>)) ||
     (std::is_array_v<operand_t<Value>> &&
      (std::same_as<std::remove_extent_t<operand_t<Value>>, char> ||
       std::same_as<
           std::remove_extent_t<operand_t<Value>>, const char>)));

template <class Value>
concept raw_string_operand = is_raw_string_operand_v<Value>;

template <class Value>
using stored_operand_t = std::conditional_t<
    raw_string_operand<Value>,
    symbolic_string_expression,
    std::decay_t<Value>>;

template <raw_string_operand Value>
[[nodiscard]] symbolic_string_expression
normalize_operand(Value&& value);

template <class Value>
    requires (!raw_string_operand<Value>)
[[nodiscard]] constexpr Value&& normalize_operand(Value&& value) noexcept;

template <raw_string_operand Value>
[[nodiscard]] symbolic_string_expression
store_operand(Value&& value);

template <class Value>
    requires (!raw_string_operand<Value>)
[[nodiscard]] constexpr Value&&
store_operand(Value&& value);

struct combinator_expression {};

struct application_expression : combinator_expression {};

template <class Value>
struct is_sk_application : std::false_type {};

template <class Argument>
struct is_sk_application<
    substitution_function<deferred_combinator<constant>, Argument>>
    : std::true_type {};

template <class Value>
inline constexpr bool is_sk_application_v =
    is_sk_application<std::remove_cvref_t<Value>>::value;

enum class printed_token : long {
    none,
    other,
    left_parenthesis,
    right_parenthesis,
    multicharacter_basis
};

[[nodiscard]] constexpr bool is_parenthesis(printed_token token) noexcept {
    return token == printed_token::left_parenthesis ||
           token == printed_token::right_parenthesis;
}

[[nodiscard]] inline int printed_token_index() {
    static int const index = std::ios_base::xalloc();
    return index;
}

[[nodiscard]] inline int print_depth_index() {
    static int const index = std::ios_base::xalloc();
    return index;
}

class print_scope {
public:
    explicit print_scope(std::ostream& output) : output_(output) {
        auto& depth = output_.iword(print_depth_index());
        if (depth++ == 0) {
            output_.iword(printed_token_index()) =
                static_cast<long>(printed_token::none);
        }
    }

    print_scope(print_scope const&) = delete;
    print_scope& operator=(print_scope const&) = delete;

    ~print_scope() {
        auto& depth = output_.iword(print_depth_index());
        if (--depth == 0) {
            output_.iword(printed_token_index()) =
                static_cast<long>(printed_token::none);
        }
    }

private:
    std::ostream& output_;
};

[[nodiscard]] inline printed_token previous_printed_token(
    std::ostream& output) {
    return static_cast<printed_token>(output.iword(printed_token_index()));
}

inline void record_printed_token(
    std::ostream& output, printed_token token) {
    output.iword(printed_token_index()) = static_cast<long>(token);
}

inline void print_token(
    std::ostream& output,
    std::string_view text,
    printed_token token = printed_token::other) {
    auto const previous = previous_printed_token(output);
    auto const follows_multicharacter_basis =
        previous == printed_token::multicharacter_basis &&
        !is_parenthesis(token);
    auto const is_unseparated_multicharacter_basis =
        token == printed_token::multicharacter_basis &&
        previous != printed_token::none &&
        !is_parenthesis(previous);

    if (follows_multicharacter_basis ||
        is_unseparated_multicharacter_basis) {
        output.put(' ');
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    record_printed_token(output, token);
}

inline void print_token(
    std::ostream& output,
    char value,
    printed_token token = printed_token::other) {
    char const text[] = {value};
    print_token(output, std::string_view(text, 1), token);
}

inline void print_layout(std::ostream& output, std::string_view text) {
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    record_printed_token(output, printed_token::none);
}

class html_escaping_streambuf final : public std::streambuf {
public:
    explicit html_escaping_streambuf(std::streambuf* destination)
        : destination_(destination) {}

    void begin_raw_output() noexcept {
        ++raw_output_depth_;
    }

    void end_raw_output() noexcept {
        --raw_output_depth_;
    }

protected:
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) {
            return traits_type::not_eof(value);
        }

        if (raw_output_depth_ != 0) {
            return destination_->sputc(
                traits_type::to_char_type(value));
        }

        return write_escaped(traits_type::to_char_type(value))
                   ? value
                   : traits_type::eof();
    }

    std::streamsize xsputn(
        char const* text,
        std::streamsize size) override {
        if (raw_output_depth_ != 0) {
            return destination_->sputn(text, size);
        }

        std::streamsize written = 0;
        while (written < size && write_escaped(text[written])) {
            ++written;
        }
        return written;
    }

    int sync() override {
        return destination_->pubsync();
    }

private:
    [[nodiscard]] bool write_raw(std::string_view text) {
        return destination_->sputn(
                   text.data(),
                   static_cast<std::streamsize>(text.size())) ==
               static_cast<std::streamsize>(text.size());
    }

    [[nodiscard]] bool write_escaped(char value) {
        switch (value) {
        case '&':
            return write_raw("&amp;");
        case '<':
            return write_raw("&lt;");
        case '>':
            return write_raw("&gt;");
        case '"':
            return write_raw("&quot;");
        case '\'':
            return write_raw("&#39;");
        default:
            return destination_->sputc(value) != traits_type::eof();
        }
    }

    std::streambuf* destination_;
    std::size_t raw_output_depth_ = 0;
};

[[nodiscard]] inline int html_escaping_streambuf_index() {
    static int const index = std::ios_base::xalloc();
    return index;
}

class raw_html_output_scope {
public:
    explicit raw_html_output_scope(
        html_escaping_streambuf& buffer) noexcept
        : buffer_(buffer) {
        buffer_.begin_raw_output();
    }

    raw_html_output_scope(raw_html_output_scope const&) = delete;
    raw_html_output_scope& operator=(
        raw_html_output_scope const&) = delete;

    ~raw_html_output_scope() {
        buffer_.end_raw_output();
    }

private:
    html_escaping_streambuf& buffer_;
};

inline void print_html_markup(
    std::ostream& output,
    std::string_view markup) {
    auto* escaping = static_cast<html_escaping_streambuf*>(
        output.pword(html_escaping_streambuf_index()));
    if (escaping != nullptr) {
        raw_html_output_scope raw_output(*escaping);
        output.write(
            markup.data(),
            static_cast<std::streamsize>(markup.size()));
        return;
    }

    output.write(
        markup.data(), static_cast<std::streamsize>(markup.size()));
}

template <raw_string_operand Value>
[[nodiscard]] constexpr std::optional<std::string_view>
raw_string_view(Value&& value) noexcept {
    using value_type = unqualified_operand_t<Value>;

    if constexpr (std::is_pointer_v<value_type>) {
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string_view(value);
    } else if constexpr (std::is_array_v<value_type>) {
        std::size_t length = 0;
        while (length < std::extent_v<value_type> &&
               value[length] != '\0') {
            ++length;
        }
        return std::string_view(value, length);
    } else {
        return std::string_view(value);
    }
}

inline void print_symbolic_string(
    std::ostream& output,
    std::optional<std::string_view> value) {
    if (!value) {
        print_token(output, "<?>");
    } else if (value->empty()) {
        print_token(output, "<>");
    } else {
        print_token(
            output,
            *value,
            value->size() > 1
                ? printed_token::multicharacter_basis
                : printed_token::other);
    }
}

class basis_label {
public:
    constexpr explicit basis_label(std::string_view name)
        : basis_label(validate(name), std::make_index_sequence<8>{}) {}

    void print_to(std::ostream& output) const {
        auto const name = view();
        print_token(
            output,
            name,
            name.size() > 1
                ? printed_token::multicharacter_basis
                : printed_token::other);
    }

    void print_as_operand_to(std::ostream& output) const {
        print_to(output);
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        std::size_t length = 0;
        while (length < 7 && characters_[length] != '\0') {
            ++length;
        }
        return std::string_view(characters_, length);
    }

private:
    [[nodiscard]] static constexpr std::string_view validate(
        std::string_view name) {
        if (name.empty() || name[0] == '\0') {
            throw std::invalid_argument(
                "combdsl::basis names cannot be empty");
        }

        if (name[0] == '(' || name[0] == ')' || name[0] == '"' ||
            name[0] == '\\' || name[0] == ' ' ||
            name[0] == '\t' || name[0] == '\n' || name[0] == '\r' ||
            name[0] == '\f' || name[0] == '\v') {
            throw std::invalid_argument(
                "combdsl::basis names cannot begin with parser whitespace, "
                "a parenthesis, a double quote, or a backslash");
        }

        std::size_t length = 0;
        while (length < name.size() && name[length] != '\0') {
            if (++length >= 8) {
                throw std::length_error(
                    "combdsl::basis names are limited to 7 characters");
            }
        }

        return name.substr(0, length);
    }

    template <std::size_t... Indexes>
    constexpr basis_label(
        std::string_view name, std::index_sequence<Indexes...>)
        : characters_{
              (Indexes < name.size() ? name[Indexes] : '\0')...} {}

    const char characters_[8];
};

template <class Value>
concept has_unambiguous_call_operator = requires {
    &Value::operator();
};

template <class Value>
concept has_adl_stream_insertion = requires(
    std::ostream& output, Value const& value) {
    { operator<<(output, value) } -> std::same_as<std::ostream&>;
};

template <class Value>
void print_operand(std::ostream& output, Value const& value) {
    print_scope scope(output);

    if constexpr (std::same_as<
                      std::remove_cvref_t<Value>, quoted_expression>) {
        value.print_as_operand_to(output);
    } else if constexpr (std::derived_from<std::remove_cvref_t<Value>,
                                           application_expression>) {
        if constexpr (is_sk_application_v<Value>) {
            print_token(output, 'I');
        } else {
            print_token(output, '(', printed_token::left_parenthesis);
            value.print_to(output);
            print_token(output, ')', printed_token::right_parenthesis);
        }
    } else if constexpr (std::derived_from<
                             std::remove_cvref_t<Value>,
                             combinator_expression>) {
        if constexpr (requires { value.print_as_operand_to(output); }) {
            value.print_as_operand_to(output);
        } else {
            value.print_to(output);
        }
    } else if constexpr (raw_string_operand<Value>) {
        print_symbolic_string(output, raw_string_view(value));
    } else {
        print_token(output, '<');

        if constexpr (requires { value.print_to(output); }) {
            value.print_to(output);
        } else if constexpr (requires { output << value.toString(); }) {
            output << value.toString();
        } else if constexpr (requires { output << value.toString; }) {
            output << value.toString;
        } else if constexpr (
            has_unambiguous_call_operator<std::remove_cvref_t<Value>> &&
            !has_adl_stream_insertion<Value>) {
            // Do not let a callable's conversion to bool make a lambda print
            // as "1", and never execute arbitrary functions while rendering.
            print_token(output, '?');
        } else if constexpr (requires { output << value; }) {
            output << value;
        } else {
            print_token(output, '?');
        }

        print_token(output, '>');
    }
}

template <class Value>
void print_result(std::ostream& output, Value&& value) {
    print_scope scope(output);

    if constexpr (std::derived_from<
                      std::remove_cvref_t<Value>, combinator_expression>) {
        value.print_to(output);
    } else {
        print_operand(output, value);
    }
}

template <std::size_t Index = 0, class Function, class Tuple>
void print_curried_result(
    std::ostream& output, Function&& function, Tuple&& arguments) {
    if constexpr (
        Index == std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
        print_result(output, std::forward<Function>(function));
    } else {
        decltype(auto) next = std::invoke(
            std::forward<Function>(function),
            std::get<Index>(std::forward<Tuple>(arguments)));
        print_curried_result<Index + 1>(
            output,
            std::forward<decltype(next)>(next),
            std::forward<Tuple>(arguments));
    }
}

template <std::size_t Index = 0, class Function, class Tuple>
[[nodiscard]] constexpr auto evaluate_curried(
    Function&& function, Tuple&& arguments) {
    if constexpr (
        Index == std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
        return std::forward<Function>(function);
    } else {
        return evaluate_curried<Index + 1>(
            std::invoke(
                std::forward<Function>(function),
                std::get<Index>(std::forward<Tuple>(arguments))),
            std::forward<Tuple>(arguments));
    }
}

template <class Function, class Argument>
class symbolic_application : public application_expression {
public:
    template <class F, class A>
        requires std::constructible_from<Function, F> &&
                 std::constructible_from<Argument, A>
    constexpr symbolic_application(F&& function, A&& argument)
        noexcept(std::is_nothrow_constructible_v<Function, F> &&
                 std::is_nothrow_constructible_v<Argument, A>)
        : function_(std::forward<F>(function)),
          argument_(std::forward<A>(argument)) {}

    void print_to(std::ostream& output) const {
        print_scope scope(output);

        if constexpr (std::derived_from<
                          std::remove_cvref_t<Function>,
                          application_expression>) {
            function_.print_to(output);
        } else {
            print_operand(output, function_);
        }

        print_operand(output, argument_);
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr Function const& function() const noexcept {
        return function_;
    }

    [[nodiscard]] constexpr Argument const& argument() const noexcept {
        return argument_;
    }

    template <class Next>
        requires std::copy_constructible<Function> &&
                 std::copy_constructible<Argument>
    [[nodiscard]] constexpr auto operator()(Next&& next) const& {
        using self_type = symbolic_application<Function, Argument>;
        using next_type = stored_operand_t<Next>;
        return symbolic_application<self_type, next_type>(
            *this, store_operand(std::forward<Next>(next)));
    }

    template <class Next>
    [[nodiscard]] constexpr auto operator()(Next&& next) && {
        using self_type = symbolic_application<Function, Argument>;
        using next_type = stored_operand_t<Next>;
        return symbolic_application<self_type, next_type>(
            std::move(*this), store_operand(std::forward<Next>(next)));
    }

private:
    [[no_unique_address]] Function function_;
    [[no_unique_address]] Argument argument_;
};

class symbolic_string_expression : public combinator_expression {
public:
    explicit symbolic_string_expression(std::string_view value)
        : value_(validated_value(value)) {}

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        print_symbolic_string(output, value_);
    }

    void print_as_operand_to(std::ostream& output) const {
        print_to(output);
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Argument>
    [[nodiscard]] auto operator()(Argument&& argument) const {
        using argument_type = stored_operand_t<Argument>;
        return symbolic_application<
            symbolic_string_expression, argument_type>(
            *this, store_operand(std::forward<Argument>(argument)));
    }

private:
    [[nodiscard]] static std::string_view validated_value(
        std::string_view value) {
        if (value.empty()) {
            throw std::invalid_argument(
                "combdsl raw string operands cannot be empty");
        }
        return value;
    }

    std::string value_;
};

template <raw_string_operand Value>
symbolic_string_expression normalize_operand(Value&& value) {
    auto const view = raw_string_view(std::forward<Value>(value));
    if (!view) {
        throw std::invalid_argument(
            "combdsl raw string operands cannot be null");
    }
    return symbolic_string_expression(*view);
}

template <class Value>
    requires (!raw_string_operand<Value>)
constexpr Value&& normalize_operand(Value&& value) noexcept {
    return std::forward<Value>(value);
}

template <raw_string_operand Value>
symbolic_string_expression store_operand(Value&& value) {
    return normalize_operand(std::forward<Value>(value));
}

template <class Value>
    requires (!raw_string_operand<Value>)
constexpr Value&& store_operand(Value&& value) {
    return std::forward<Value>(value);
}

template <class Thunk>
class deferred_value {
private:
    using result_type = std::invoke_result_t<Thunk&>;

    static_assert(!std::is_void_v<result_type>,
                  "combdsl::defer requires a non-void computation");

public:
    using value_type = std::conditional_t<
        std::is_reference_v<result_type>,
        std::remove_reference_t<result_type>,
        std::remove_cvref_t<result_type>>;

private:
    using stored_type = std::conditional_t<
        std::is_reference_v<result_type>,
        std::reference_wrapper<value_type>,
        value_type>;

    class shared_state {
    public:
        template <class F>
            requires std::constructible_from<Thunk, F>
        explicit shared_state(F&& thunk)
            : thunk_(std::forward<F>(thunk)) {}

        [[nodiscard]] value_type& get() {
            if (!cached_) {
                if constexpr (std::is_reference_v<result_type>) {
                    cached_.emplace(std::ref(std::invoke(thunk_)));
                } else {
                    cached_.emplace(std::invoke(thunk_));
                }
            }

            if constexpr (std::is_reference_v<result_type>) {
                return cached_->get();
            } else {
                return (*cached_);
            }
        }

        void print_to(std::ostream& output) const {
            print_scope scope(output);

            if constexpr (requires { thunk_.print_to(output); }) {
                thunk_.print_to(output);
            } else {
                print_token(output, "deferred");
            }
        }

    private:
        [[no_unique_address]] Thunk thunk_;
        std::optional<stored_type> cached_;
    };

public:
    template <class F>
        requires std::constructible_from<Thunk, F>
    explicit deferred_value(F&& thunk)
        : state_(std::make_shared<shared_state>(std::forward<F>(thunk))) {}

    [[nodiscard]] value_type& get() const {
        return state_->get();
    }

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        state_->print_to(output);
    }

    template <class Argument>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) const {
        return std::invoke(get(), std::forward<Argument>(argument));
    }

    // Typed consumers force a deferred value naturally. Generic consumers can
    // preserve laziness and call combdsl::force explicitly when needed.
    [[nodiscard]] operator value_type&() const {
        return get();
    }

private:
    std::shared_ptr<shared_state> state_;
};

template <class Value>
class deferred_combinator : public combinator_expression {
public:
    using value_type = Value;

    [[nodiscard]] static Value& get() {
        static Value value{};
        return value;
    }

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        get().print_to(output);
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Argument>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) const {
        return std::invoke(get(), std::forward<Argument>(argument));
    }

    [[nodiscard]] operator Value&() const {
        return get();
    }
};

template <class Value>
struct is_deferred_value : std::false_type {};

template <class Thunk>
struct is_deferred_value<deferred_value<Thunk>> : std::true_type {};

template <class Value>
struct is_deferred_value<deferred_combinator<Value>> : std::true_type {};

template <class Expression>
struct is_deferred_value<deferred_basis_expression<Expression>>
    : std::true_type {};

template <class Value>
inline constexpr bool is_deferred_value_v =
    is_deferred_value<std::remove_cvref_t<Value>>::value;

template <class Function, class Argument>
constexpr decltype(auto) invoke_deferred(
    Function&& function, Argument&& argument) {
    if constexpr (is_deferred_value_v<Function>) {
        return std::invoke(
            function.get(), std::forward<Argument>(argument));
    } else {
        return std::invoke(
            std::forward<Function>(function),
            std::forward<Argument>(argument));
    }
}

template <class Value>
class constant_function : public application_expression {
public:
    template <class T>
        requires std::constructible_from<Value, T>
    constexpr explicit constant_function(T&& value)
        noexcept(std::is_nothrow_constructible_v<Value, T>)
        : value_(std::forward<T>(value)) {}

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        print_token(output, 'K');
        print_operand(output, value_);
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr Value const& value() const noexcept {
        return value_;
    }

    template <class Ignored>
        requires std::copy_constructible<Value>
    [[nodiscard]] constexpr Value operator()(Ignored&&) const&
        noexcept(std::is_nothrow_copy_constructible_v<Value>) {
        return value_;
    }

    template <class Ignored>
        requires std::copy_constructible<Value>
    [[nodiscard]] constexpr Value operator()(Ignored&&) &
        noexcept(std::is_nothrow_copy_constructible_v<Value>) {
        return value_;
    }

    template <class Ignored>
    [[nodiscard]] constexpr Value operator()(Ignored&&) &&
        noexcept(std::is_nothrow_move_constructible_v<Value>) {
        return std::move(value_);
    }

private:
    [[no_unique_address]] Value value_;
};

template <class Function, class Argument>
class substitution_function : public application_expression {
public:
    template <class F, class G>
        requires std::constructible_from<Function, F> &&
                 std::constructible_from<Argument, G>
    constexpr substitution_function(F&& function, G&& argument)
        noexcept(std::is_nothrow_constructible_v<Function, F> &&
                 std::is_nothrow_constructible_v<Argument, G>)
        : function_(std::forward<F>(function)),
          argument_(std::forward<G>(argument)) {}

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        print_token(output, 'S');
        print_operand(output, function_);
        print_operand(output, argument_);
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr Function const& function() const noexcept {
        return function_;
    }

    [[nodiscard]] constexpr Argument const& argument() const noexcept {
        return argument_;
    }

    template <class Value>
    [[nodiscard]] constexpr decltype(auto) operator()(Value&& value) & {
        return apply(*this, std::forward<Value>(value));
    }

    template <class Value>
    [[nodiscard]] constexpr decltype(auto) operator()(Value&& value) const& {
        return apply(*this, std::forward<Value>(value));
    }

    template <class Value>
    [[nodiscard]] constexpr decltype(auto) operator()(Value&& value) && {
        return apply(std::move(*this), std::forward<Value>(value));
    }

private:
    template <class Self, class Value>
    static constexpr decltype(auto) apply(Self&& self, Value&& value) {
        // The z expression has already been evaluated to enter operator().
        // Memoize that result in one binding so S(x)(y)(z) shares the same z
        // between both branches instead of reconstructing or moving it twice.
        decltype(auto) memoized_z =
            normalize_operand(std::forward<Value>(value));

        decltype(auto) function = invoke_deferred(
            std::forward<Self>(self).function_, memoized_z);
        decltype(auto) argument = invoke_deferred(
            std::forward<Self>(self).argument_, memoized_z);

        using result_type = decltype(invoke_deferred(
            std::forward<decltype(function)>(function),
            std::forward<decltype(argument)>(argument)));

        if constexpr (
            std::is_rvalue_reference_v<result_type> ||
            (raw_string_operand<Value> &&
             std::is_reference_v<result_type> &&
             std::same_as<
                 std::remove_cvref_t<result_type>,
                 symbolic_string_expression>)) {
            // A forwarding callable such as I can otherwise expose an rvalue
            // reference to the local result of g(z). Materialize it before
            // the branch-result bindings leave scope.
            return std::remove_cvref_t<result_type>(invoke_deferred(
                std::forward<decltype(function)>(function),
                std::forward<decltype(argument)>(argument)));
        } else {
            return invoke_deferred(
                std::forward<decltype(function)>(function),
                std::forward<decltype(argument)>(argument));
        }
    }

    [[no_unique_address]] Function function_;
    [[no_unique_address]] Argument argument_;
};

template <class Function>
class substitution_with_function : public application_expression {
public:
    template <class F>
        requires std::constructible_from<Function, F>
    constexpr explicit substitution_with_function(F&& function)
        noexcept(std::is_nothrow_constructible_v<Function, F>)
        : function_(std::forward<F>(function)) {}

    void print_to(std::ostream& output) const {
        print_scope scope(output);
        print_token(output, 'S');
        print_operand(output, function_);
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr Function const& function() const noexcept {
        return function_;
    }

    template <class Argument>
        requires std::copy_constructible<Function>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) const& {
        using argument_type = stored_operand_t<Argument>;
        return substitution_function<Function, argument_type>(
            function_, store_operand(std::forward<Argument>(argument)));
    }

    template <class Argument>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) && {
        using argument_type = stored_operand_t<Argument>;
        return substitution_function<Function, argument_type>(
            std::move(function_),
            store_operand(std::forward<Argument>(argument)));
    }

private:
    [[no_unique_address]] Function function_;
};

template <class Generator>
struct recursion_state;

template <class Generator>
struct unfolding_cache;

template <class Generator>
class recursion_reference {
public:
    explicit recursion_reference(
        std::shared_ptr<unfolding_cache<Generator>> cache)
        : cache_(std::move(cache)) {}

    template <class Argument>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) const;

private:
    std::shared_ptr<unfolding_cache<Generator>> cache_;
};

template <class Generator>
struct recursion_thunk {
    std::shared_ptr<recursion_state<Generator>> state;

    void print_to(std::ostream& output) const;

    [[nodiscard]] recursion_reference<Generator> operator()() const;
};

template <class Generator>
using recursive_argument = deferred_value<recursion_thunk<Generator>>;

template <class Generator>
[[nodiscard]] auto make_recursive_argument(
    std::shared_ptr<recursion_state<Generator>> state) {
    return recursive_argument<Generator>(
        recursion_thunk<Generator>{std::move(state)});
}

template <class Generator>
using generated_result =
    std::invoke_result_t<Generator&, recursive_argument<Generator>>;

template <class Generator>
struct recursion_state {
    template <class F>
        requires std::constructible_from<Generator, F>
    explicit recursion_state(F&& source)
        : generator(std::forward<F>(source)) {}

    [[no_unique_address]] Generator generator;
};

template <class Generator>
void recursion_thunk<Generator>::print_to(std::ostream& output) const {
    print_scope scope(output);
    print_token(output, "deferred Y");
    print_token(output, '(', printed_token::left_parenthesis);
    print_result(output, std::as_const(state->generator));
    print_token(output, ')', printed_token::right_parenthesis);
}

template <class Generator>
struct unfolding_cache {
    using result_type = generated_result<Generator>;

    static_assert(!std::is_void_v<result_type>,
                  "combdsl::Y requires a non-void generator result");
    static_assert(!std::is_rvalue_reference_v<result_type>,
                  "combdsl::Y cannot memoize an rvalue reference");

    using value_type = std::remove_reference_t<result_type>;
    using stored_type = std::conditional_t<
        std::is_lvalue_reference_v<result_type>,
        std::reference_wrapper<value_type>,
        std::remove_cv_t<value_type>>;

    explicit unfolding_cache(
        std::shared_ptr<recursion_state<Generator>> source)
        : state(std::move(source)) {}

    [[nodiscard]] decltype(auto) get() {
        if (!result) {
            auto self = make_recursive_argument(state);

            if constexpr (std::is_lvalue_reference_v<result_type>) {
                result.emplace(std::ref(
                    std::invoke(state->generator, std::move(self))));
            } else {
                result.emplace(
                    std::invoke(state->generator, std::move(self)));
            }
        }

        if constexpr (std::is_lvalue_reference_v<result_type>) {
            return result->get();
        } else {
            return (*result);
        }
    }

    std::shared_ptr<recursion_state<Generator>> state;
    std::optional<stored_type> result;
};

template <class Generator>
recursion_reference<Generator>
recursion_thunk<Generator>::operator()() const {
    auto cache = std::make_shared<unfolding_cache<Generator>>(state);
    static_cast<void>(cache->get());
    return recursion_reference<Generator>(std::move(cache));
}

template <class Generator>
template <class Argument>
decltype(auto) recursion_reference<Generator>::operator()(
    Argument&& argument) const {
    return std::invoke(
        cache_->get(), std::forward<Argument>(argument));
}

}  // namespace detail

[[nodiscard]] constexpr bool is_single_utf8_char(
    std::string_view str) noexcept {
    std::size_t const len = str.length();
    if (len == 0 || len > 4) {
        return false;
    }

    auto const b1 = static_cast<std::uint8_t>(str[0]);
    if (b1 <= 0x7F) {
        return len == 1;
    }

    auto const is_trail = [](std::uint8_t byte) {
        return byte >= 0x80 && byte <= 0xBF;
    };

    if (b1 >= 0xC2 && b1 <= 0xDF) {
        return len == 2 &&
               is_trail(static_cast<std::uint8_t>(str[1]));
    }

    if (b1 >= 0xE0 && b1 <= 0xEF) {
        if (len != 3 ||
            !is_trail(static_cast<std::uint8_t>(str[1])) ||
            !is_trail(static_cast<std::uint8_t>(str[2]))) {
            return false;
        }

        auto const b2 = static_cast<std::uint8_t>(str[1]);
        if (b1 == 0xE0 && b2 < 0xA0) {
            return false;
        }
        if (b1 == 0xED && b2 >= 0xA0) {
            return false;
        }

        return true;
    }

    if (b1 >= 0xF0 && b1 <= 0xF4) {
        if (len != 4 ||
            !is_trail(static_cast<std::uint8_t>(str[1])) ||
            !is_trail(static_cast<std::uint8_t>(str[2])) ||
            !is_trail(static_cast<std::uint8_t>(str[3]))) {
            return false;
        }

        auto const b2 = static_cast<std::uint8_t>(str[1]);
        if (b1 == 0xF0 && b2 < 0x90) {
            return false;
        }
        if (b1 == 0xF4 && b2 >= 0x90) {
            return false;
        }

        return true;
    }

    return false;
}

class symbol_expression : public detail::combinator_expression {
public:
    constexpr explicit symbol_expression(char name)
        : name_{validated_name(name)}, length_(1) {}

    constexpr explicit symbol_expression(std::string_view name)
        : name_{}, length_(validated_length(name)) {
        for (std::size_t index = 0; index < name.length(); ++index) {
            name_[index] = name[index];
        }
    }

    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        detail::print_token(
            output, std::string_view(name_, length_));
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Argument>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) const {
        using argument_type = detail::stored_operand_t<Argument>;
        return detail::symbolic_application<
            symbol_expression, argument_type>(
            *this,
            detail::store_operand(std::forward<Argument>(argument)));
    }

private:
    [[nodiscard]] static constexpr char validated_name(char name) {
        if (name < 'a' || name > 'z') {
            throw std::invalid_argument(
                "combdsl::symbol names must be lowercase ASCII letters");
        }
        return name;
    }

    [[nodiscard]] static constexpr std::uint8_t validated_length(
        std::string_view name) {
        if (!is_single_utf8_char(name)) {
            throw std::invalid_argument(
                "combdsl::symbol names must contain one valid UTF-8 "
                "character");
        }
        return static_cast<std::uint8_t>(name.length());
    }

    char name_[4];
    std::uint8_t length_;
};

[[nodiscard]] constexpr symbol_expression symbol(char name) {
    return symbol_expression(name);
}

[[nodiscard]] constexpr symbol_expression symbol(std::string_view name) {
    return symbol_expression(name);
}

template <class Expression, class... Arguments>
class basis_application_expression : public detail::application_expression {
private:
    using arguments_type = std::tuple<Arguments...>;

public:
    template <class Value, class Tuple>
        requires std::constructible_from<Expression, Value> &&
                 std::constructible_from<arguments_type, Tuple>
    constexpr basis_application_expression(
        detail::basis_label name,
        std::size_t arity,
        Value&& expression,
        Tuple&& arguments)
        : name_(std::move(name)),
          arity_(arity),
          expression_(std::forward<Value>(expression)),
          arguments_(std::forward<Tuple>(arguments)) {}

    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);

        if (sizeof...(Arguments) < arity_) {
            name_.print_to(output);
            std::apply(
                [&output](auto const&... arguments) {
                    (detail::print_operand(output, arguments), ...);
                },
                arguments_);
        } else {
            detail::print_curried_result(
                output, expression_, arguments_);
        }
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr detail::basis_label const& name() const noexcept {
        return name_;
    }

    [[nodiscard]] constexpr std::size_t arity() const noexcept {
        return arity_;
    }

    [[nodiscard]] constexpr Expression& expression() noexcept {
        return expression_;
    }

    [[nodiscard]] constexpr Expression const& expression() const noexcept {
        return expression_;
    }

    [[nodiscard]] constexpr auto const& arguments() const noexcept {
        return arguments_;
    }

    template <class Next>
        requires std::copy_constructible<Expression> &&
                 (std::copy_constructible<Arguments> && ...)
    [[nodiscard]] constexpr auto operator()(Next&& next) const& {
        using next_type = detail::stored_operand_t<Next>;
        using result_type = basis_application_expression<
            Expression, Arguments..., next_type>;

        return result_type(
            name_,
            arity_,
            expression_,
            std::tuple_cat(
                arguments_,
                std::tuple<next_type>(detail::store_operand(
                    std::forward<Next>(next)))));
    }

    template <class Next>
    [[nodiscard]] constexpr auto operator()(Next&& next) && {
        using next_type = detail::stored_operand_t<Next>;
        using result_type = basis_application_expression<
            Expression, Arguments..., next_type>;

        return result_type(
            std::move(name_),
            arity_,
            std::move(expression_),
            std::tuple_cat(
                std::move(arguments_),
                std::tuple<next_type>(detail::store_operand(
                    std::forward<Next>(next)))));
    }

    [[nodiscard]] constexpr auto evaluate() & {
        require_saturated();
        return detail::evaluate_curried(expression_, arguments_);
    }

    [[nodiscard]] constexpr auto evaluate() const& {
        require_saturated();
        return detail::evaluate_curried(expression_, arguments_);
    }

    [[nodiscard]] constexpr auto evaluate() && {
        require_saturated();
        return detail::evaluate_curried(
            std::move(expression_), std::move(arguments_));
    }

private:
    constexpr void require_saturated() const {
        if (sizeof...(Arguments) < arity_) {
            throw std::logic_error(
                "cannot evaluate an undersaturated combdsl::basis");
        }
    }

    detail::basis_label name_;
    std::size_t arity_;
    [[no_unique_address]] Expression expression_;
    [[no_unique_address]] arguments_type arguments_;
};

template <class Expression>
class basis_expression : public detail::combinator_expression {
public:
    template <class Value>
        requires std::constructible_from<Expression, Value>
    constexpr basis_expression(
        std::string_view name,
        std::size_t arity,
        Value&& expression)
        : name_(name),
          arity_(arity),
          expression_(std::forward<Value>(expression)) {}

    template <class Value>
        requires std::constructible_from<Expression, Value>
    constexpr basis_expression(
        detail::basis_label name,
        std::size_t arity,
        Value&& expression)
        : name_(std::move(name)),
          arity_(arity),
          expression_(std::forward<Value>(expression)) {}

    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        name_.print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const {
        detail::print_scope scope(output);
        name_.print_as_operand_to(output);
    }

    void operator()() const {
        print_to(std::cout);
    }

    [[nodiscard]] constexpr detail::basis_label const& name() const noexcept {
        return name_;
    }

    [[nodiscard]] constexpr std::size_t arity() const noexcept {
        return arity_;
    }

    [[nodiscard]] constexpr Expression& expression() noexcept {
        return expression_;
    }

    [[nodiscard]] constexpr Expression const& expression() const noexcept {
        return expression_;
    }

    template <class Argument>
        requires std::copy_constructible<Expression>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) const& {
        using argument_type = detail::stored_operand_t<Argument>;
        return basis_application_expression<Expression, argument_type>(
            name_,
            arity_,
            expression_,
            std::tuple<argument_type>(
                detail::store_operand(
                    std::forward<Argument>(argument))));
    }

    template <class Argument>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) && {
        using argument_type = detail::stored_operand_t<Argument>;
        return basis_application_expression<Expression, argument_type>(
            std::move(name_),
            arity_,
            std::move(expression_),
            std::tuple<argument_type>(
                detail::store_operand(
                    std::forward<Argument>(argument))));
    }

private:
    detail::basis_label name_;
    std::size_t arity_;
    [[no_unique_address]] Expression expression_;
};

template <class Expression>
class deferred_basis_expression : public detail::combinator_expression {
private:
    struct basis_thunk {
        detail::basis_label name;
        std::size_t arity;
        [[no_unique_address]] Expression expression;

        [[nodiscard]] basis_expression<Expression> operator()() {
            return basis_expression<Expression>(
                std::move(name), arity, std::move(expression));
        }
    };

    using deferred_type = detail::deferred_value<basis_thunk>;

    template <class Value>
    [[nodiscard]] static basis_thunk make_thunk(
        detail::basis_label name,
        std::size_t arity,
        Value&& expression) {
        return basis_thunk{
            std::move(name),
            arity,
            Expression(std::forward<Value>(expression))};
    }

public:
    using value_type = basis_expression<Expression>;

    template <class Value>
        requires std::constructible_from<Expression, Value>
    explicit deferred_basis_expression(
        std::string_view name,
        std::size_t arity,
        Value&& expression)
        : deferred_(make_thunk(
              detail::basis_label(name),
              arity,
              std::forward<Value>(expression))) {}

    template <class Value>
        requires std::constructible_from<Expression, Value>
    explicit deferred_basis_expression(
        detail::basis_label name,
        std::size_t arity,
        Value&& expression)
        : deferred_(make_thunk(
              std::move(name),
              arity,
              std::forward<Value>(expression))) {}

    [[nodiscard]] value_type& get() const {
        return deferred_.get();
    }

    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        get().print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const {
        detail::print_scope scope(output);
        get().print_as_operand_to(output);
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Argument>
        requires std::invocable<value_type&, Argument&&>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) & {
        return std::invoke(get(), std::forward<Argument>(argument));
    }

    template <class Argument>
        requires std::invocable<value_type&, Argument&&>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) const& {
        return std::invoke(get(), std::forward<Argument>(argument));
    }

    template <class Argument>
        requires std::invocable<value_type&&, Argument&&>
    [[nodiscard]] decltype(auto) operator()(Argument&& argument) && {
        return std::invoke(
            std::move(get()), std::forward<Argument>(argument));
    }

    [[nodiscard]] operator value_type&() const {
        return get();
    }

private:
    deferred_type deferred_;
};

template <class Expression>
[[nodiscard]] auto basis(
    std::string_view name,
    std::size_t arity,
    Expression&& expression)
    -> deferred_basis_expression<detail::stored_operand_t<Expression>>;

struct identity : detail::combinator_expression {
    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        detail::print_token(output, 'I');
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Value>
    [[nodiscard]] constexpr decltype(auto) operator()(Value&& value) const
        noexcept(noexcept(detail::normalize_operand(
            std::forward<Value>(value)))) {
        return detail::normalize_operand(std::forward<Value>(value));
    }
};

struct constant : detail::combinator_expression {
    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        detail::print_token(output, 'K');
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Value>
    [[nodiscard]] constexpr auto operator()(Value&& value) const {
        using value_type = detail::stored_operand_t<Value>;
        return detail::constant_function<value_type>(
            detail::store_operand(std::forward<Value>(value)));
    }
};

struct substitution : detail::combinator_expression {
    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        detail::print_token(output, 'S');
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Function>
    [[nodiscard]] constexpr auto operator()(Function&& function) const {
        using function_type = detail::stored_operand_t<Function>;
        return detail::substitution_with_function<function_type>(
            detail::store_operand(std::forward<Function>(function)));
    }
};

struct fixed_point_combinator : detail::combinator_expression {
    void print_to(std::ostream& output) const {
        detail::print_scope scope(output);
        detail::print_token(output, 'Y');
    }

    void operator()() const {
        print_to(std::cout);
    }

    template <class Generator>
    [[nodiscard]] auto operator()(Generator&& generator) const {
        using stored_generator = detail::stored_operand_t<Generator>;

        auto state =
            std::make_shared<detail::recursion_state<stored_generator>>(
                detail::store_operand(
                    std::forward<Generator>(generator)));
        auto self = detail::make_recursive_argument(state);

        // Operationally: Y(x) = x(defer([owned x] { return Y(x); })).
        // Shared state gives the recursive argument a stable lifetime, while
        // each demanded unfolding is memoized without an ownership cycle.
        return std::invoke(state->generator, std::move(self));
    }
};

struct defer_computation {
    template <class Thunk>
        requires std::invocable<std::decay_t<Thunk>&> &&
                 (!std::is_void_v<std::invoke_result_t<std::decay_t<Thunk>&>>)
    [[nodiscard]] auto operator()(Thunk&& thunk) const {
        return detail::deferred_value<std::decay_t<Thunk>>(
            std::forward<Thunk>(thunk));
    }
};

struct force_value {
    template <class Thunk>
    [[nodiscard]] auto operator()(
        detail::deferred_value<Thunk> const& value) const
        -> typename detail::deferred_value<Thunk>::value_type& {
        return value.get();
    }

    template <class Value>
    [[nodiscard]] auto operator()(
        detail::deferred_combinator<Value> const& value) const -> Value& {
        return value.get();
    }

    template <class Expression>
    [[nodiscard]] auto operator()(
        deferred_basis_expression<Expression> const& value) const
        -> typename deferred_basis_expression<Expression>::value_type& {
        return value.get();
    }

    template <class Expression, class... Arguments>
    [[nodiscard]] auto operator()(
        basis_application_expression<Expression, Arguments...>& value) const {
        return value.evaluate();
    }

    template <class Expression, class... Arguments>
    [[nodiscard]] auto operator()(
        basis_application_expression<Expression, Arguments...> const& value)
        const {
        return value.evaluate();
    }

    template <class Expression, class... Arguments>
    [[nodiscard]] auto operator()(
        basis_application_expression<Expression, Arguments...>&& value) const {
        return std::move(value).evaluate();
    }

    template <class Value>
        requires (!detail::is_deferred_value_v<Value>)
    [[nodiscard]] constexpr Value&& operator()(Value&& value) const noexcept {
        return std::forward<Value>(value);
    }
};

template <class Value>
[[nodiscard]] quoted_expression quote(Value&& value);

namespace detail {

enum class quoted_node_kind {
    opaque,
    identity,
    constant,
    substitution,
    fixed_point,
    application,
    recursive_y,
    basis_argument,
    html_argument,
    basis
};

class quoted_node {
public:
    virtual ~quoted_node() = default;

    [[nodiscard]] virtual quoted_node_kind kind() const noexcept = 0;
    [[nodiscard]] virtual bool is_application() const noexcept { return false; }
    virtual void print_to(std::ostream& output) const = 0;
    virtual void print_as_operand_to(std::ostream& output) const {
        print_to(output);
    }
};

struct quoted_access;

} // namespace detail

class quoted_expression : public detail::combinator_expression {
public:
    quoted_expression(quoted_expression const&) = default;
    quoted_expression(quoted_expression&&) noexcept = default;
    quoted_expression& operator=(quoted_expression const&) = default;
    quoted_expression& operator=(quoted_expression&&) noexcept = default;

    void print_to(std::ostream& output) const;
    void print_as_operand_to(std::ostream& output) const;

    void operator()() const { print_to(std::cout); }

    template <class Argument>
    [[nodiscard]] quoted_expression operator()(Argument&& argument) const;

private:
    explicit quoted_expression(std::shared_ptr<detail::quoted_node const> root)
        : root_(std::move(root)) {}

    std::shared_ptr<detail::quoted_node const> root_;

    friend struct detail::quoted_access;
};

namespace detail {

struct quoted_access {
    [[nodiscard]] static quoted_expression
    make(std::shared_ptr<quoted_node const> root) {
        return quoted_expression(std::move(root));
    }

    [[nodiscard]] static std::shared_ptr<quoted_node const> const&
    root(quoted_expression const& expression) noexcept {
        return expression.root_;
    }
};

class quoted_primitive_node final : public quoted_node {
public:
    explicit quoted_primitive_node(quoted_node_kind kind) : kind_(kind) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return kind_;
    }

    void print_to(std::ostream& output) const override {
        switch (kind_) {
        case quoted_node_kind::identity:
            print_token(output, 'I');
            break;
        case quoted_node_kind::constant:
            print_token(output, 'K');
            break;
        case quoted_node_kind::substitution:
            print_token(output, 'S');
            break;
        case quoted_node_kind::fixed_point:
            print_token(output, 'Y');
            break;
        default:
            print_token(output, '?');
            break;
        }
    }

private:
    quoted_node_kind kind_;
};

template <class Value>
class quoted_leaf_node final : public quoted_node {
public:
    explicit quoted_leaf_node(std::shared_ptr<Value const> value)
        : value_(std::move(value)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::opaque;
    }

    [[nodiscard]] bool is_application() const noexcept override {
        return std::derived_from<Value, application_expression>;
    }

    void print_to(std::ostream& output) const override {
        print_result(output, *value_);
    }

private:
    std::shared_ptr<Value const> value_;
};

class quoted_application_node final : public quoted_node {
public:
    quoted_application_node(quoted_expression function,
                            quoted_expression argument)
        : function_(std::move(function)), argument_(std::move(argument)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::application;
    }

    [[nodiscard]] bool is_application() const noexcept override { return true; }

    void print_to(std::ostream& output) const override {
        function_.print_to(output);
        argument_.print_as_operand_to(output);
    }

    [[nodiscard]] quoted_expression const& function() const noexcept {
        return function_;
    }

    [[nodiscard]] quoted_expression const& argument() const noexcept {
        return argument_;
    }

private:
    quoted_expression function_;
    quoted_expression argument_;
};

[[nodiscard]] inline bool
is_quoted_sk_application(quoted_expression const& expression) noexcept {
    auto const& outer_root = quoted_access::root(expression);
    if (outer_root->kind() != quoted_node_kind::application) {
        return false;
    }

    auto const& outer =
        static_cast<quoted_application_node const&>(*outer_root);
    auto const& inner_root = quoted_access::root(outer.function());
    if (inner_root->kind() != quoted_node_kind::application) {
        return false;
    }

    auto const& inner =
        static_cast<quoted_application_node const&>(*inner_root);
    return quoted_access::root(inner.function())->kind() ==
               quoted_node_kind::substitution &&
           quoted_access::root(inner.argument())->kind() ==
               quoted_node_kind::constant;
}

class quoted_recursive_y_node final : public quoted_node {
public:
    explicit quoted_recursive_y_node(quoted_expression generator)
        : generator_(std::move(generator)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::recursive_y;
    }

    void print_to(std::ostream& output) const override {
        print_token(output, "<deferred Y");
        print_token(output, '(', printed_token::left_parenthesis);
        generator_.print_to(output);
        print_token(output, ')', printed_token::right_parenthesis);
        print_token(output, '>');
    }

    [[nodiscard]] quoted_expression const& generator() const noexcept {
        return generator_;
    }

private:
    quoted_expression generator_;
};

class quoted_basis_argument_node final : public quoted_node {
public:
    explicit quoted_basis_argument_node(quoted_expression argument)
        : argument_(std::move(argument)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::basis_argument;
    }

    void print_to(std::ostream& output) const override {
        argument_.print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const override {
        argument_.print_as_operand_to(output);
    }

    [[nodiscard]] quoted_expression const& argument() const noexcept {
        return argument_;
    }

private:
    quoted_expression argument_;
};

enum class html_argument_color {
    red,
    green,
    blue,
    dark_orange,
    munsel_purple
};

class quoted_html_argument_node final : public quoted_node {
public:
    quoted_html_argument_node(
        quoted_expression argument,
        html_argument_color color)
        : argument_(std::move(argument)), color_(color) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::html_argument;
    }

    void print_to(std::ostream& output) const override {
        print_with_markup(output, false);
    }

    void print_as_operand_to(std::ostream& output) const override {
        print_with_markup(output, true);
    }

    [[nodiscard]] quoted_expression const& argument() const noexcept {
        return argument_;
    }

private:
    void print_with_markup(
        std::ostream& output,
        bool as_operand) const {
        print_html_markup(output, opening_markup());
        if (as_operand) {
            argument_.print_as_operand_to(output);
        } else {
            argument_.print_to(output);
        }
        print_html_markup(output, "</span>");
    }

    [[nodiscard]] std::string_view opening_markup() const noexcept {
        switch (color_) {
        case html_argument_color::red:
            return "<span class=\"wor\">";
        case html_argument_color::green:
            return "<span class=\"wog\">";
        case html_argument_color::blue:
            return "<span class=\"wob\">";
        case html_argument_color::dark_orange:
            return "<span class=\"woo\">";
        case html_argument_color::munsel_purple:
            return "<span class=\"wop\">";
        }
        return {};
    }

    quoted_expression argument_;
    html_argument_color color_;
};

class quoted_basis_node_base : public quoted_node {
public:
    [[nodiscard]] quoted_node_kind kind() const noexcept final {
        return quoted_node_kind::basis;
    }

    [[nodiscard]] virtual std::size_t arity() const noexcept = 0;
    [[nodiscard]] virtual quoted_expression body() const = 0;
};

template <class Expression>
class quoted_basis_node final : public quoted_basis_node_base {
public:
    quoted_basis_node(basis_label name, std::size_t arity,
                      std::shared_ptr<Expression const> expression)
        : name_(std::move(name)), arity_(arity),
          expression_(std::move(expression)) {}

    [[nodiscard]] std::size_t arity() const noexcept override { return arity_; }

    void print_to(std::ostream& output) const override {
        name_.print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const override {
        name_.print_as_operand_to(output);
    }

    [[nodiscard]] quoted_expression body() const override;

private:
    basis_label name_;
    std::size_t arity_;
    std::shared_ptr<Expression const> expression_;
};

[[nodiscard]] inline quoted_expression
make_quoted_primitive(quoted_node_kind kind) {
    return quoted_access::make(std::make_shared<quoted_primitive_node>(kind));
}

[[nodiscard]] inline quoted_expression
make_quoted_application(quoted_expression function,
                        quoted_expression argument) {
    return quoted_access::make(std::make_shared<quoted_application_node>(
        std::move(function), std::move(argument)));
}

[[nodiscard]] inline quoted_expression
make_quoted_recursive_y(quoted_expression generator) {
    return quoted_access::make(
        std::make_shared<quoted_recursive_y_node>(std::move(generator)));
}

[[nodiscard]] inline quoted_expression
make_quoted_basis_argument(quoted_expression argument) {
    return quoted_access::make(
        std::make_shared<quoted_basis_argument_node>(std::move(argument)));
}

[[nodiscard]] inline quoted_expression make_quoted_html_argument(
    quoted_expression argument,
    html_argument_color color) {
    return quoted_access::make(
        std::make_shared<quoted_html_argument_node>(
            std::move(argument), color));
}

template <class Value>
[[nodiscard]] quoted_expression
make_quoted_native(Value const& value, std::shared_ptr<void const> owner);

[[nodiscard]] inline quoted_expression
make_quoted_native(quoted_expression const& value,
                   std::shared_ptr<void const>) {
    return value;
}

template <class Function, class Argument>
[[nodiscard]] quoted_expression
make_quoted_native(symbolic_application<Function, Argument> const& value,
                   std::shared_ptr<void const> owner);

template <class Value>
[[nodiscard]] quoted_expression
make_quoted_native(constant_function<Value> const& value,
                   std::shared_ptr<void const> owner);

template <class Function>
[[nodiscard]] quoted_expression
make_quoted_native(substitution_with_function<Function> const& value,
                   std::shared_ptr<void const> owner);

template <class Function, class Argument>
[[nodiscard]] quoted_expression
make_quoted_native(substitution_function<Function, Argument> const& value,
                   std::shared_ptr<void const> owner);

template <class Expression>
[[nodiscard]] quoted_expression
make_quoted_native(basis_expression<Expression> const& value,
                   std::shared_ptr<void const> owner);

template <class Expression>
[[nodiscard]] quoted_expression
make_quoted_native(deferred_basis_expression<Expression> const& value,
                   std::shared_ptr<void const> owner);

template <class Expression, class... Arguments>
[[nodiscard]] quoted_expression make_quoted_native(
    basis_application_expression<Expression, Arguments...> const& value,
    std::shared_ptr<void const> owner);

template <class Expression>
[[nodiscard]] quoted_expression
make_quoted_basis(basis_label const& name, std::size_t arity,
                  Expression const& expression,
                  std::shared_ptr<void const> owner) {
    auto expression_owner = std::shared_ptr<Expression const>(
        std::move(owner), std::addressof(expression));
    return quoted_access::make(std::make_shared<quoted_basis_node<Expression>>(
        name, arity, std::move(expression_owner)));
}

template <class Expression>
[[nodiscard]] quoted_expression
make_quoted_basis_snapshot(basis_label const& name, std::size_t arity,
                           Expression&& expression) {
    using expression_type = std::remove_cvref_t<Expression>;
    auto expression_owner =
        std::make_shared<expression_type>(std::forward<Expression>(expression));
    return quoted_access::make(
        std::make_shared<quoted_basis_node<expression_type>>(
            name, arity, std::move(expression_owner)));
}

template <class Value>
quoted_expression make_quoted_native(Value const& value,
                                     std::shared_ptr<void const> owner) {
    using value_type = std::remove_cv_t<Value>;

    if constexpr (std::same_as<value_type, identity> ||
                  std::same_as<value_type, deferred_combinator<identity>>) {
        return make_quoted_primitive(quoted_node_kind::identity);
    } else if constexpr (std::same_as<value_type, constant> ||
                         std::same_as<value_type,
                                      deferred_combinator<constant>>) {
        return make_quoted_primitive(quoted_node_kind::constant);
    } else if constexpr (std::same_as<value_type, substitution> ||
                         std::same_as<value_type,
                                      deferred_combinator<substitution>>) {
        return make_quoted_primitive(quoted_node_kind::substitution);
    } else if constexpr (std::same_as<value_type, fixed_point_combinator>) {
        return make_quoted_primitive(quoted_node_kind::fixed_point);
    } else {
        auto value_owner = std::shared_ptr<value_type const>(
            std::move(owner), std::addressof(value));
        return quoted_access::make(
            std::make_shared<quoted_leaf_node<value_type>>(
                std::move(value_owner)));
    }
}

template <class Function, class Argument>
quoted_expression
make_quoted_native(symbolic_application<Function, Argument> const& value,
                   std::shared_ptr<void const> owner) {
    auto function = make_quoted_native(value.function(), owner);
    auto argument = make_quoted_native(value.argument(), std::move(owner));
    return make_quoted_application(std::move(function), std::move(argument));
}

template <class Value>
quoted_expression make_quoted_native(constant_function<Value> const& value,
                                     std::shared_ptr<void const> owner) {
    return make_quoted_application(
        make_quoted_primitive(quoted_node_kind::constant),
        make_quoted_native(value.value(), std::move(owner)));
}

template <class Function>
quoted_expression
make_quoted_native(substitution_with_function<Function> const& value,
                   std::shared_ptr<void const> owner) {
    return make_quoted_application(
        make_quoted_primitive(quoted_node_kind::substitution),
        make_quoted_native(value.function(), std::move(owner)));
}

template <class Function, class Argument>
quoted_expression
make_quoted_native(substitution_function<Function, Argument> const& value,
                   std::shared_ptr<void const> owner) {
    auto result = make_quoted_application(
        make_quoted_primitive(quoted_node_kind::substitution),
        make_quoted_native(value.function(), owner));
    return make_quoted_application(
        std::move(result),
        make_quoted_native(value.argument(), std::move(owner)));
}

template <class Expression>
quoted_expression make_quoted_native(basis_expression<Expression> const& value,
                                     std::shared_ptr<void const> owner) {
    return make_quoted_basis(value.name(), value.arity(), value.expression(),
                             std::move(owner));
}

template <class Expression>
quoted_expression
make_quoted_native(deferred_basis_expression<Expression> const& value,
                   std::shared_ptr<void const> owner) {
    auto const& forced = value.get();
    if constexpr (std::copy_constructible<Expression>) {
        return make_quoted_basis_snapshot(forced.name(), forced.arity(),
                                          forced.expression());
    } else {
        return make_quoted_basis(forced.name(), forced.arity(),
                                 forced.expression(), std::move(owner));
    }
}

template <class Expression, class... Arguments>
quoted_expression make_quoted_native(
    basis_application_expression<Expression, Arguments...> const& value,
    std::shared_ptr<void const> owner) {
    auto result = make_quoted_basis(value.name(), value.arity(),
                                    value.expression(), owner);

    std::apply([&result, &owner](auto const&... arguments) {
        ((result = make_quoted_application(
              std::move(result), make_quoted_native(arguments, owner))),
         ...);
    }, value.arguments());
    return result;
}

template <class Expression>
quoted_expression quoted_basis_node<Expression>::body() const {
    return make_quoted_native(*expression_, expression_);
}

template <class Value>
struct is_deferred_basis_expression : std::false_type {};

template <class Expression>
struct is_deferred_basis_expression<deferred_basis_expression<Expression>>
    : std::true_type {};

template <class Value>
inline constexpr bool is_deferred_basis_expression_v =
    is_deferred_basis_expression<std::remove_cvref_t<Value>>::value;

template <class Expression>
    requires std::copy_constructible<Expression>
[[nodiscard]] quoted_expression
make_owned_quoted(deferred_basis_expression<Expression> const& value) {
    auto const& forced = value.get();
    return make_quoted_basis_snapshot(forced.name(), forced.arity(),
                                      forced.expression());
}

template <class Expression>
    requires std::copy_constructible<Expression>
[[nodiscard]] quoted_expression
make_owned_quoted(deferred_basis_expression<Expression>& value) {
    return make_owned_quoted(std::as_const(value));
}

template <class Expression>
    requires std::move_constructible<Expression>
[[nodiscard]] quoted_expression
make_owned_quoted(deferred_basis_expression<Expression>&& value) {
    auto& forced = value.get();
    return make_quoted_basis_snapshot(forced.name(), forced.arity(),
                                      std::move(forced.expression()));
}

template <class Value>
    requires (!is_deferred_basis_expression_v<Value>)
[[nodiscard]] quoted_expression make_owned_quoted(Value&& value) {
    using value_type = std::decay_t<Value>;
    auto owner = std::make_shared<value_type>(std::forward<Value>(value));
    return make_quoted_native(std::as_const(*owner),
                              std::shared_ptr<void const>(owner));
}

} // namespace detail

inline void quoted_expression::print_to(std::ostream& output) const {
    detail::print_scope scope(output);
    root_->print_to(output);
}

inline void quoted_expression::print_as_operand_to(std::ostream& output) const {
    detail::print_scope scope(output);

    if (detail::is_quoted_sk_application(*this)) {
        detail::print_token(output, 'I');
    } else if (root_->is_application()) {
        detail::print_token(
            output, '(', detail::printed_token::left_parenthesis);
        root_->print_to(output);
        detail::print_token(
            output, ')', detail::printed_token::right_parenthesis);
    } else {
        root_->print_as_operand_to(output);
    }
}

template <class Argument>
quoted_expression quoted_expression::operator()(Argument&& argument) const {
    auto function = *this;
    auto quoted_argument = quote(std::forward<Argument>(argument));
    return detail::make_quoted_application(std::move(function),
                                           std::move(quoted_argument));
}

template <class Value>
quoted_expression quote(Value&& value) {
    if constexpr (std::same_as<std::remove_cvref_t<Value>, quoted_expression>) {
        return quoted_expression(std::forward<Value>(value));
    } else if constexpr (detail::raw_string_operand<Value>) {
        return detail::make_owned_quoted(
            detail::store_operand(std::forward<Value>(value)));
    } else {
        return detail::make_owned_quoted(std::forward<Value>(value));
    }
}

namespace detail {

struct reduction_options {
    bool basis_step = false;
    bool reduce_bare_recursive_y = true;
};

struct reduction_trace {
    std::optional<quoted_expression> before;
    std::optional<quoted_expression> after;
};

[[nodiscard]] inline std::optional<quoted_expression>
reduce_next_redex(quoted_expression const& expression,
                  reduction_options options,
                  reduction_trace* trace = nullptr);

[[nodiscard]] inline quoted_expression
restore_basis_arguments(quoted_expression const& expression) {
    auto const& root = quoted_access::root(expression);
    switch (root->kind()) {
    case quoted_node_kind::basis_argument:
        return static_cast<quoted_basis_argument_node const&>(*root)
            .argument();
    case quoted_node_kind::application: {
        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        return make_quoted_application(
            restore_basis_arguments(application.function()),
            restore_basis_arguments(application.argument()));
    }
    case quoted_node_kind::recursive_y: {
        auto const& recursive =
            static_cast<quoted_recursive_y_node const&>(*root);
        return make_quoted_recursive_y(
            restore_basis_arguments(recursive.generator()));
    }
    default:
        return expression;
    }
}

[[nodiscard]] inline quoted_expression
strip_html_argument_colors(quoted_expression const& expression) {
    auto const& root = quoted_access::root(expression);
    switch (root->kind()) {
    case quoted_node_kind::html_argument:
        return strip_html_argument_colors(
            static_cast<quoted_html_argument_node const&>(*root)
                .argument());
    case quoted_node_kind::application: {
        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        return make_quoted_application(
            strip_html_argument_colors(application.function()),
            strip_html_argument_colors(application.argument()));
    }
    case quoted_node_kind::recursive_y: {
        auto const& recursive =
            static_cast<quoted_recursive_y_node const&>(*root);
        return make_quoted_recursive_y(
            strip_html_argument_colors(recursive.generator()));
    }
    case quoted_node_kind::basis_argument:
        return make_quoted_basis_argument(
            strip_html_argument_colors(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
    default:
        return expression;
    }
}

inline void print_quoted_html(
    std::ostream& output,
    quoted_expression const& expression) {
    if (!output.good()) {
        return;
    }
    if (output.pword(html_escaping_streambuf_index()) != nullptr) {
        expression.print_to(output);
        return;
    }

    auto const context_index = html_escaping_streambuf_index();
    auto* destination = output.rdbuf();
    auto* previous_context = output.pword(context_index);
    html_escaping_streambuf escaping(destination);
    output.rdbuf(&escaping);
    output.pword(context_index) = &escaping;

    auto restore_output = [&] {
        auto const state = output.rdstate();
        output.pword(context_index) = previous_context;
        output.rdbuf(destination);
        if (state != std::ios_base::goodbit) {
            output.setstate(state);
        }
    };

    try {
        expression.print_to(output);
    } catch (...) {
        restore_output();
        throw;
    }
    restore_output();
}

[[nodiscard]] inline quoted_expression
reduce_at_head(
    quoted_expression expression,
    reduction_options options,
    reduction_trace* trace) {
    std::vector<quoted_expression> reversed_arguments;
    auto head = expression;

    while (quoted_access::root(head)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(head));
        reversed_arguments.push_back(application.argument());
        head = application.function();
    }

    std::reverse(reversed_arguments.begin(), reversed_arguments.end());

    auto append_arguments = [&reversed_arguments](quoted_expression result,
                                                  std::size_t first) {
        for (; first < reversed_arguments.size(); ++first) {
            result = result(reversed_arguments[first]);
        }
        return result;
    };

    auto prepare_trace = [&](std::size_t arity) {
        if (trace == nullptr) {
            return;
        }

        constexpr html_argument_color colors[] = {
            html_argument_color::red,
            html_argument_color::green,
            html_argument_color::blue,
            html_argument_color::dark_orange,
            html_argument_color::munsel_purple,
        };
        auto const colored_arguments =
            std::min(arity, std::size(colors));
        for (std::size_t index = 0;
             index < colored_arguments;
             ++index) {
            reversed_arguments[index] = make_quoted_html_argument(
                std::move(reversed_arguments[index]), colors[index]);
        }
        trace->before = append_arguments(head, 0);
    };

    auto finish_trace = [trace](quoted_expression result) {
        if (trace != nullptr) {
            trace->after = result;
        }
        return result;
    };

    auto const kind = quoted_access::root(head)->kind();
    switch (kind) {
    case quoted_node_kind::identity:
        if (reversed_arguments.size() >= 1) {
            prepare_trace(1);
            return finish_trace(
                append_arguments(reversed_arguments[0], 1));
        }
        break;
    case quoted_node_kind::constant:
        if (reversed_arguments.size() >= 2) {
            prepare_trace(2);
            return finish_trace(
                append_arguments(reversed_arguments[0], 2));
        }
        break;
    case quoted_node_kind::substitution:
        if (reversed_arguments.size() >= 3) {
            prepare_trace(3);
            auto const& function = reversed_arguments[0];
            auto const& argument = reversed_arguments[1];
            auto const& value = reversed_arguments[2];
            auto result = function(value)(argument(value));
            return finish_trace(
                append_arguments(std::move(result), 3));
        }
        break;
    case quoted_node_kind::fixed_point:
        if (reversed_arguments.size() >= 1) {
            prepare_trace(1);
            auto const& generator = reversed_arguments[0];
            auto recursive = make_quoted_recursive_y(generator);
            auto result = generator(std::move(recursive));
            return finish_trace(
                append_arguments(std::move(result), 1));
        }
        break;
    case quoted_node_kind::recursive_y: {
        if (!options.reduce_bare_recursive_y &&
            reversed_arguments.empty()) {
            break;
        }

        auto const& recursive =
            static_cast<quoted_recursive_y_node const&>(
                *quoted_access::root(head));
        if (trace != nullptr) {
            head = make_quoted_recursive_y(
                make_quoted_html_argument(
                    recursive.generator(), html_argument_color::red));
            trace->before = append_arguments(head, 0);
        }
        auto const& traced_recursive =
            static_cast<quoted_recursive_y_node const&>(
                *quoted_access::root(head));
        auto result = traced_recursive.generator()(head);
        return finish_trace(
            append_arguments(std::move(result), 0));
    }
    case quoted_node_kind::basis: {
        auto const& basis = static_cast<quoted_basis_node_base const&>(
            *quoted_access::root(head));
        if (reversed_arguments.size() >= basis.arity()) {
            prepare_trace(basis.arity());
            if (options.basis_step || basis.arity() == 0) {
                return finish_trace(
                    append_arguments(basis.body(), 0));
            }

            auto result = basis.body();
            for (std::size_t index = 0; index < basis.arity(); ++index) {
                result = result(make_quoted_basis_argument(
                    reversed_arguments[index]));
            }

            constexpr reduction_options basis_reduction{
                .basis_step = false,
                .reduce_bare_recursive_y = false,
            };
            while (auto reduced =
                       reduce_next_redex(result, basis_reduction)) {
                result = std::move(*reduced);
            }

            return finish_trace(
                append_arguments(
                    restore_basis_arguments(result), basis.arity()));
        }
        break;
    }
    default:
        break;
    }

    return expression;
}

[[nodiscard]] inline std::optional<quoted_expression>
reduce_next_redex(quoted_expression const& expression,
                  reduction_options options,
                  reduction_trace* trace) {
    auto reduced = reduce_at_head(expression, options, trace);
    if (quoted_access::root(reduced) != quoted_access::root(expression)) {
        return reduced;
    }

    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::application) {
        return std::nullopt;
    }

    auto const& application =
        static_cast<quoted_application_node const&>(*root);
    if (auto reduced_function =
            reduce_next_redex(application.function(), options, trace)) {
        auto result = make_quoted_application(
            std::move(*reduced_function), application.argument());
        if (trace != nullptr) {
            trace->before = make_quoted_application(
                std::move(*trace->before), application.argument());
            trace->after = result;
        }
        return result;
    }
    if (auto reduced_argument =
            reduce_next_redex(application.argument(), options, trace)) {
        auto result = make_quoted_application(
            application.function(), std::move(*reduced_argument));
        if (trace != nullptr) {
            trace->before = make_quoted_application(
                application.function(), std::move(*trace->before));
            trace->after = result;
        }
        return result;
    }

    return std::nullopt;
}

} // namespace detail

[[nodiscard]] inline quoted_expression
single_step(quoted_expression expression, bool basis_step = false) {
    if (auto reduced = detail::reduce_next_redex(
            expression,
            detail::reduction_options{.basis_step = basis_step})) {
        return std::move(*reduced);
    }

    return expression;
}

[[nodiscard]] inline quoted_expression color_step(
    quoted_expression expression,
    std::ostream& output = std::cout,
    bool basis_step = false) {
    detail::reduction_trace trace;
    auto reduced = detail::reduce_next_redex(
        expression,
        detail::reduction_options{.basis_step = basis_step},
        &trace);
    auto const& before =
        trace.before ? *trace.before : expression;
    auto const& after =
        trace.after ? *trace.after
                    : (reduced ? *reduced : expression);

    detail::print_layout(output, "  ");
    detail::print_quoted_html(output, before);
    detail::print_layout(output, "\n");
    output.flush();

    detail::print_layout(output, "->");
    detail::print_quoted_html(output, after);
    detail::print_layout(output, "\n");
    output.flush();

    if (reduced) {
        return detail::strip_html_argument_colors(after);
    }
    return expression;
}

[[nodiscard]] inline quoted_expression color_step(
    quoted_expression expression,
    bool basis_step) {
    return color_step(
        std::move(expression), std::cout, basis_step);
}

namespace detail {

inline std::atomic_flag evaluation_not_interrupted = ATOMIC_FLAG_INIT;

inline void evaluation_sigint_handler(int) noexcept {
    evaluation_not_interrupted.clear(std::memory_order_relaxed);
}

class scoped_evaluation_sigint_handler {
public:
    scoped_evaluation_sigint_handler() {
        evaluation_not_interrupted.test_and_set(std::memory_order_relaxed);
        previous_ = std::signal(SIGINT, evaluation_sigint_handler);
        if (previous_ == SIG_ERR) {
            throw std::runtime_error("could not install SIGINT handler");
        }
    }

    scoped_evaluation_sigint_handler(
        scoped_evaluation_sigint_handler const&) = delete;
    scoped_evaluation_sigint_handler& operator=(
        scoped_evaluation_sigint_handler const&) = delete;

    ~scoped_evaluation_sigint_handler() {
        std::signal(SIGINT, previous_);
        evaluation_not_interrupted.test_and_set(std::memory_order_relaxed);
    }

private:
    using signal_handler = void (*)(int);
    signal_handler previous_ = SIG_DFL;
};

[[nodiscard]] inline bool consume_evaluation_interrupt() noexcept {
    return !evaluation_not_interrupted.test_and_set(
        std::memory_order_relaxed);
}

[[nodiscard]] inline bool wait_for_evaluation_resume(
    std::istream& input,
    std::ostream& output) {
    print_layout(
        output,
        "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    output.flush();

    std::string command;
    while (std::getline(input, command)) {
        if (command.empty()) {
            return true;
        }
        if (command == "q" || command == "Q") {
            return false;
        }
    }

    return false;
}

enum class evaluation_interrupt_result {
    not_interrupted,
    resumed,
    quit
};

} // namespace detail

inline void eval(
    quoted_expression expression,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    detail::scoped_evaluation_sigint_handler sigint_handler;
    auto print_expression = [&output](quoted_expression const& current) {
        current.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
    };
    auto wait_after_interrupt = [&](quoted_expression const& current) {
        if (!detail::consume_evaluation_interrupt()) {
            return detail::evaluation_interrupt_result::not_interrupted;
        }

        print_expression(current);
        return detail::wait_for_evaluation_resume(input, output)
                   ? detail::evaluation_interrupt_result::resumed
                   : detail::evaluation_interrupt_result::quit;
    };

    bool expression_was_printed = false;
    for (;;) {
        auto const before_step = wait_after_interrupt(expression);
        if (before_step == detail::evaluation_interrupt_result::quit) {
            return;
        }
        if (before_step == detail::evaluation_interrupt_result::resumed) {
            expression_was_printed = true;
        }

        auto next = single_step(expression, basis_step);
        auto const no_reduction =
            detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression);
        auto const after_step = wait_after_interrupt(next);
        if (after_step == detail::evaluation_interrupt_result::quit) {
            return;
        }
        auto const next_was_printed =
            after_step == detail::evaluation_interrupt_result::resumed;

        if (no_reduction) {
            if (!expression_was_printed && !next_was_printed) {
                print_expression(expression);
            }
            return;
        }

        expression = std::move(next);
        expression_was_printed = next_was_printed;
    }
}

inline void single_step_loop(
    quoted_expression expression,
    std::istream& input = std::cin,
    std::ostream& output = std::cout,
    bool basis_step = false) {
    detail::print_layout(
        output,
        "Press Enter for one reduction step; type q then Enter to quit.\n");
    expression.print_to(output);
    detail::print_layout(output, "\n");
    output.flush();

    std::string command;
    while (std::getline(input, command)) {
        if (command == "q" || command == "Q") {
            return;
        }
        if (!command.empty()) {
            continue;
        }

        auto next = single_step(expression, basis_step);
        if (detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression)) {
            return;
        }

        expression = std::move(next);
        expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
    }
}

inline void single_step_run(
    quoted_expression expression,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false);

namespace detail {

[[nodiscard]] inline bool wait_after_single_step_run_interrupt(
    std::istream& input,
    std::ostream& output) {
    if (!consume_evaluation_interrupt()) {
        return true;
    }
    return wait_for_evaluation_resume(input, output);
}

} // namespace detail

inline void single_step_run(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step) {
    detail::scoped_evaluation_sigint_handler sigint_handler;

    output.flush();

    for (;;) {
        if (!detail::wait_after_single_step_run_interrupt(input, output)) {
            return;
        }

        auto next = single_step(expression, basis_step);
        if (!detail::wait_after_single_step_run_interrupt(input, output)) {
            return;
        }

        if (detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression)) {
            return;
        }

        expression = std::move(next);
        expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
    }
}

namespace detail {

class registered_parser_basis {
public:
    explicit registered_parser_basis(std::string name)
        : name_(std::move(name)) {}

    virtual ~registered_parser_basis() = default;

    [[nodiscard]] std::string const& name() const noexcept {
        return name_;
    }

    [[nodiscard]] virtual quoted_expression expression() const = 0;

private:
    std::string name_;
};

template <class Basis>
class registered_parser_basis_model final : public registered_parser_basis {
public:
    registered_parser_basis_model(std::string name, Basis const& basis)
        : registered_parser_basis(std::move(name)),
          basis_(std::make_shared<Basis>(basis)) {}

    [[nodiscard]] quoted_expression expression() const override {
        std::call_once(quote_once_, [this] {
            quoted_.emplace(make_quoted_native(
                std::as_const(*basis_),
                std::shared_ptr<void const>(basis_)));
        });
        return *quoted_;
    }

private:
    std::shared_ptr<Basis> basis_;
    mutable std::once_flag quote_once_;
    mutable std::optional<quoted_expression> quoted_;
};

using registered_parser_basis_ptr =
    std::shared_ptr<registered_parser_basis const>;

struct parser_basis_name_hash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(
        std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};

using registered_parser_basis_table = std::unordered_map<
    std::string,
    registered_parser_basis_ptr,
    parser_basis_name_hash,
    std::equal_to<>>;

[[nodiscard]] inline std::mutex& parser_basis_registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

[[nodiscard]] inline registered_parser_basis_table&
parser_basis_registry() {
    static registered_parser_basis_table entries;
    return entries;
}

[[nodiscard]] inline bool is_primitive_name(
    std::string_view name) noexcept {
    return name == "S" || name == "K" || name == "I" || name == "Y";
}

template <class Basis>
void register_parser_basis(std::string_view name, Basis const& basis) {
    if (is_primitive_name(name)) {
        return;
    }

    auto registration =
        std::make_shared<registered_parser_basis_model<Basis>>(
            std::string(name), basis);

    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    if (!entries.contains(name)) {
        entries.emplace(std::string(name), std::move(registration));
    }
}

[[nodiscard]] inline registered_parser_basis_table
registered_parser_bases_snapshot() {
    std::lock_guard lock(parser_basis_registry_mutex());
    return parser_basis_registry();
}

} // namespace detail

template <class Expression>
[[nodiscard]] auto basis(
    std::string_view name,
    std::size_t arity,
    Expression&& expression)
    -> deferred_basis_expression<detail::stored_operand_t<Expression>> {
    using basis_type =
        deferred_basis_expression<detail::stored_operand_t<Expression>>;

    detail::basis_label owned_name(name);
    auto result = basis_type(
        owned_name,
        arity,
        detail::store_operand(std::forward<Expression>(expression)));
    detail::register_parser_basis(owned_name.view(), result);
    return result;
}

inline constexpr detail::deferred_combinator<identity> I{};
inline constexpr detail::deferred_combinator<constant> K{};
inline constexpr detail::deferred_combinator<substitution> S{};
inline constexpr fixed_point_combinator Y{};
inline constexpr defer_computation defer{};
inline constexpr force_value force{};

[[nodiscard]] inline std::string input_escape(std::string_view input) {
    std::string result;
    auto escaped_size = input.size();
    auto const maximum_size = result.max_size();

    if (escaped_size > maximum_size) {
        throw std::length_error(
            "combdsl::input_escape result is too large");
    }

    for (char const byte : input) {
        if (byte == '\\' || byte == '"') {
            if (escaped_size == maximum_size) {
                throw std::length_error(
                    "combdsl::input_escape result is too large");
            }
            ++escaped_size;
        }
    }

    result.reserve(escaped_size);
    for (char const byte : input) {
        if (byte == '\\' || byte == '"') {
            result.push_back('\\');
        }
        result.push_back(byte);
    }
    return result;
}

class parse_error : public std::invalid_argument {
public:
    parse_error(std::size_t position, std::string_view message)
        : std::invalid_argument(make_message(position, message)),
          position_(position) {}

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

private:
    [[nodiscard]] static std::string make_message(
        std::size_t position,
        std::string_view message) {
        auto result = std::string("parse error at position ");
        result += std::to_string(position);
        result += ": ";
        result += message;
        return result;
    }

    std::size_t position_;
};

namespace detail {

struct parsed_input {
    quoted_expression expression;
    bool is_definition;
};

class quoted_expression_parser {
public:
    explicit quoted_expression_parser(std::string_view source)
        : source_(source),
          registered_bases_(registered_parser_bases_snapshot()) {}

    [[nodiscard]] parsed_input parse_input() {
        skip_whitespace();
        if (at_end()) {
            fail("expected an expression");
        }
        if (current() == ')') {
            fail("unexpected ')'");
        }

        auto const is_definition = begins_set_definition();
        auto result = is_definition
            ? parse_set_definition()
            : parse_expression();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }
        return {std::move(result), is_definition};
    }

private:
    [[nodiscard]] bool begins_set_definition() const noexcept {
        constexpr std::string_view keyword = "set";
        auto const remaining = source_.substr(position_);
        return remaining.starts_with(keyword) &&
               remaining.size() > keyword.size() &&
               is_whitespace(remaining[keyword.size()]);
    }

    [[nodiscard]] quoted_expression parse_set_definition() {
        constexpr std::size_t keyword_size = 3;
        position_ += keyword_size;
        skip_whitespace();

        auto const name_position = position_;
        while (!at_end() &&
               !is_basis_token_delimiter(position_) &&
               current() != '=') {
            ++position_;
        }
        if (position_ == name_position) {
            fail("expected a basis name");
        }

        auto const name_text =
            source_.substr(name_position, position_ - name_position);
        auto name = validated_set_basis_name(name_text, name_position);

        skip_whitespace();
        if (at_end() || current() != '=') {
            fail("expected '='");
        }
        ++position_;

        auto body = parse_expression();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }

        auto result = make_quoted_basis_snapshot(
            name, 0, std::move(body));
        register_parser_basis(name.view(), result);
        return result;
    }

    [[nodiscard]] basis_label validated_set_basis_name(
        std::string_view name,
        std::size_t name_position) const {
        try {
            return basis_label(name);
        } catch (std::length_error const& error) {
            throw parse_error(name_position + 7, error.what());
        } catch (std::invalid_argument const& error) {
            throw parse_error(name_position, error.what());
        }
    }

    [[nodiscard]] quoted_expression parse_expression() {
        std::optional<quoted_expression> result;
        skip_whitespace();

        while (!at_end() && current() != ')') {
            auto atom = parse_atom();
            if (result.has_value()) {
                auto application = (*result)(std::move(atom));
                result = std::move(application);
            } else {
                result = std::move(atom);
            }
            skip_whitespace();
        }

        if (!result.has_value()) {
            fail("expected an expression");
        }
        return std::move(*result);
    }

    [[nodiscard]] quoted_expression parse_atom() {
        if (current() == '(') {
            ++position_;
            auto nested = parse_expression();
            if (at_end()) {
                fail("expected ')'");
            }
            ++position_;
            return nested;
        }

        if (current() == '\\') {
            return parse_escaped_atom();
        }

        if (auto named_basis = parse_named_basis_token()) {
            return std::move(*named_basis);
        }

        auto const name = current();
        switch (name) {
        case 'S':
            ++position_;
            return quote(S);
        case 'K':
            ++position_;
            return quote(K);
        case 'I':
            ++position_;
            return quote(I);
        case 'Y':
            ++position_;
            return quote(Y);
        default:
            if (auto named_basis =
                    parse_single_character_basis()) {
                return std::move(*named_basis);
            }
            if (name >= 'a' && name <= 'z') {
                ++position_;
                return quote(symbol(name));
            }
            fail("unknown operand");
        }
    }

    [[nodiscard]] quoted_expression parse_escaped_atom() {
        if (!has_next()) {
            position_ = source_.size();
            fail("expected an escaped backslash or word delimiter");
        }

        auto const escaped = source_[position_ + 1];
        if (escaped == '"') {
            return parse_word_string();
        }
        if (escaped == '\\') {
            position_ += 2;
            return quote(std::string_view("\\", 1));
        }

        fail("expected an escaped backslash or word delimiter");
    }

    [[nodiscard]] quoted_expression parse_word_string() {
        position_ += 2;
        std::string result;

        while (!at_end()) {
            if (current() != '\\') {
                result.push_back(current());
                ++position_;
                continue;
            }

            if (!has_next()) {
                position_ = source_.size();
                fail("expected an escaped backslash or closing word delimiter");
            }

            auto const escaped = source_[position_ + 1];
            if (escaped == '\\') {
                result.push_back('\\');
                position_ += 2;
                continue;
            }
            if (escaped == '"') {
                if (result.empty()) {
                    fail("word strings cannot be empty");
                }
                position_ += 2;
                return quote(std::move(result));
            }

            fail("expected an escaped backslash or closing word delimiter");
        }

        fail("expected a closing word delimiter");
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_named_basis_token() {
        auto const name = current_basis_token();
        auto const match = registered_bases_.find(name);
        if (match == registered_bases_.end()) {
            return std::nullopt;
        }

        position_ += name.size();
        return match->second->expression();
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_single_character_basis() {
        auto const name = source_.substr(position_, 1);
        auto const match = registered_bases_.find(name);
        if (match == registered_bases_.end()) {
            return std::nullopt;
        }

        if (begins_with_unseparated_multicharacter_basis(
                current_basis_token())) {
            fail("unknown operand");
        }

        ++position_;
        return match->second->expression();
    }

    [[nodiscard]] std::string_view current_basis_token() const noexcept {
        auto end = position_;
        while (end < source_.size() &&
               !is_basis_token_delimiter(end)) {
            ++end;
        }
        return source_.substr(position_, end - position_);
    }

    [[nodiscard]] bool begins_with_unseparated_multicharacter_basis(
        std::string_view token) const {
        constexpr std::size_t maximum_basis_name_size = 7;
        for (std::size_t length = 2;
             length < token.size() &&
             length <= maximum_basis_name_size;
             ++length) {
            if (registered_bases_.contains(token.substr(0, length))) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool is_basis_token_delimiter(
        std::size_t position) const noexcept {
        auto const value = source_[position];
        return is_whitespace(value) || value == '(' || value == ')' ||
               (value == '\\' && position + 1 < source_.size() &&
                source_[position + 1] == '"');
    }

    void skip_whitespace() noexcept {
        while (!at_end() && is_whitespace(current())) {
            ++position_;
        }
    }

    [[nodiscard]] static bool is_whitespace(char value) noexcept {
        return value == ' ' || value == '\t' || value == '\n' ||
               value == '\r' || value == '\f' || value == '\v';
    }

    [[nodiscard]] bool at_end() const noexcept {
        return position_ == source_.size();
    }

    [[nodiscard]] char current() const noexcept {
        return source_[position_];
    }

    [[nodiscard]] bool has_next() const noexcept {
        return source_.size() - position_ >= 2;
    }

    [[noreturn]] void fail(std::string_view message) const {
        throw parse_error(position_, message);
    }

    std::string_view source_;
    std::size_t position_ = 0;
    registered_parser_basis_table registered_bases_;
};

[[nodiscard]] inline parsed_input parse_input(std::string_view source) {
    return quoted_expression_parser(source).parse_input();
}

} // namespace detail

[[nodiscard]] inline quoted_expression parse(std::string_view source) {
    return std::move(detail::parse_input(source).expression);
}

inline void parse_eval(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    eval(std::move(parsed.expression), output, input, basis_step);
}

inline void read_parse_eval(
    std::istream& input = std::cin,
    std::ostream& output = std::cout,
    bool basis_step = false) {
    std::string source;
    if (std::getline(input, source)) {
        parse_eval(source, output, input, basis_step);
    }
}

inline void parse_and_step(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    single_step_run(
        std::move(parsed.expression), output, input, basis_step);
}

#define BASIS(name, arity, expression) \
    inline const auto name = ::combdsl::basis(#name, (arity), (expression))

BASIS(M, 1, S(I)(I));
BASIS(W, 2, S(S)(S(K)));
BASIS(B, 3, S(K(S))(K));
BASIS(O, 2, S(I));
BASIS(T, 2, S(K(S(I)))(K));
BASIS(N, 2, S(S)(K));
BASIS(R, 3, S(K(S(S)))(S(K(K))(K)));
BASIS(C, 3, S(S(K(B))(S))(K(K)));
BASIS(P, 3, S(K(S(B)))(K));
BASIS(V, 3, S(S(K(S))(S(K(K))(S(K(S))(T))))(K(K)));
BASIS(D, 4, S(K(S(K(S))))(S(K(K))));
BASIS(L, 2, S(B)(K(M)));
BASIS(Z, 2, S(B)(I));
BASIS(A, 2, S(B)(T));
BASIS(Cstar, 4, S(K(C)));
BASIS(Vstar, 4, S(K(Cstar))(C));
BASIS(V4, 4, S(K(Vstar))(T));
BASIS(G, 1, S(S(S(S(S(I)(K(S)))(K(T)))(K(K)))(K(K(K))))(K(S(K))));
BASIS(
    Hprime,
    3,
    S(K(W))(S(K(S(K(C))))(S(K(S(K(S(G)))))(S(S(K(B))(B))(K(T))))));
BASIS(H, 1, Y(Hprime));

#define SYMBOL(lower_letter) \
    inline constexpr auto lower_letter = \
        ::combdsl::symbol((#lower_letter)[0])

SYMBOL(a);
SYMBOL(b);
SYMBOL(c);
SYMBOL(d);
SYMBOL(e);
SYMBOL(f);
SYMBOL(g);
SYMBOL(h);
SYMBOL(i);
SYMBOL(j);
SYMBOL(k);
SYMBOL(l);
SYMBOL(m);
SYMBOL(n);
SYMBOL(o);
SYMBOL(p);
SYMBOL(q);
SYMBOL(r);
SYMBOL(s);
SYMBOL(t);
SYMBOL(u);
SYMBOL(v);
SYMBOL(w);
SYMBOL(x);
SYMBOL(y);
SYMBOL(z);

} // namespace combdsl
