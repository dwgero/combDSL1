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
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <streambuf>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace combdsl {

struct constant;
class quoted_expression;
class quoted_atomic;

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
        : basis_label(validate(name), std::make_index_sequence<16>{}) {}

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
        while (length < 15 && characters_[length] != '\0') {
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
            if (++length > 15) {
                throw std::length_error(
                    "combdsl::basis names are limited to 15 characters");
            }
        }

        return name.substr(0, length);
    }

    template <std::size_t... Indexes>
    constexpr basis_label(
        std::string_view name, std::index_sequence<Indexes...>)
        : characters_{
              (Indexes < name.size() ? name[Indexes] : '\0')...} {}

    const char characters_[16];
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
        print_token(output, '(', printed_token::left_parenthesis);
        value.print_to(output);
        print_token(output, ')', printed_token::right_parenthesis);
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

    [[nodiscard]] std::string_view value() const noexcept {
        return value_;
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

    [[nodiscard]] constexpr std::string_view name() const noexcept {
        return std::string_view(name_, length_);
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
    rec_func,
    identity,
    constant,
    substitution,
    fixed_point,
    application,
    pending_sk,
    recursive_y,
    basis_argument,
    colored_argument,
    basis
};

enum class quoted_atomic_kind {
    none,
    symbol,
    symbolic_string,
    rec_func
};

class quoted_node {
public:
    virtual ~quoted_node() = default;

    [[nodiscard]] virtual quoted_node_kind kind() const noexcept = 0;
    [[nodiscard]] virtual bool is_application() const noexcept { return false; }
    [[nodiscard]] virtual quoted_atomic_kind
    atomic_kind() const noexcept {
        return quoted_atomic_kind::none;
    }
    [[nodiscard]] virtual std::string_view
    atomic_name() const noexcept {
        return {};
    }
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

    [[nodiscard]] quoted_atomic_kind
    atomic_kind() const noexcept override {
        if constexpr (std::same_as<Value, symbol_expression>) {
            return quoted_atomic_kind::symbol;
        } else if constexpr (
            std::same_as<Value, symbolic_string_expression>) {
            return quoted_atomic_kind::symbolic_string;
        } else {
            return quoted_atomic_kind::none;
        }
    }

    [[nodiscard]] std::string_view
    atomic_name() const noexcept override {
        if constexpr (std::same_as<Value, symbol_expression>) {
            return value_->name();
        } else if constexpr (
            std::same_as<Value, symbolic_string_expression>) {
            return value_->value();
        } else {
            return {};
        }
    }

    void print_to(std::ostream& output) const override {
        print_result(output, *value_);
    }

private:
    std::shared_ptr<Value const> value_;
};

class quoted_rec_func_node final : public quoted_node {
public:
    explicit quoted_rec_func_node(basis_label name)
        : name_(std::move(name)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::rec_func;
    }

    [[nodiscard]] quoted_atomic_kind
    atomic_kind() const noexcept override {
        return quoted_atomic_kind::rec_func;
    }

    [[nodiscard]] std::string_view
    atomic_name() const noexcept override {
        return name_.view();
    }

    void print_to(std::ostream& output) const override {
        name_.print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const override {
        name_.print_as_operand_to(output);
    }

private:
    basis_label name_;
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

class quoted_pending_sk_node final : public quoted_node {
public:
    explicit quoted_pending_sk_node(
        quoted_expression application)
        : application_(std::move(application)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::pending_sk;
    }

    void print_to(std::ostream& output) const override {
        application_.print_to(output);
    }

    void print_as_operand_to(std::ostream& output) const override {
        application_.print_as_operand_to(output);
    }

    [[nodiscard]] quoted_expression const&
    application() const noexcept {
        return application_;
    }

private:
    quoted_expression application_;
};

[[nodiscard]] inline bool
is_quoted_sk(quoted_expression const& expression) noexcept {
    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::application) {
        return false;
    }

    auto const& application =
        static_cast<quoted_application_node const&>(*root);
    return quoted_access::root(application.function())->kind() ==
               quoted_node_kind::substitution &&
           quoted_access::root(application.argument())->kind() ==
               quoted_node_kind::constant;
}

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

    return is_quoted_sk(outer.function());
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

enum class argument_color {
    red,
    green,
    blue,
    dark_orange,
    munsell_purple
};

class argument_color_renderer {
public:
    virtual ~argument_color_renderer() = default;

    virtual void begin_argument_color(
        std::ostream& output,
        argument_color color) = 0;
    virtual void end_argument_color(std::ostream& output) = 0;
};

[[nodiscard]] inline int argument_color_renderer_index() {
    static int const index = std::ios_base::xalloc();
    return index;
}

class quoted_colored_argument_node final : public quoted_node {
public:
    quoted_colored_argument_node(
        quoted_expression argument,
        argument_color color)
        : argument_(std::move(argument)), color_(color) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::colored_argument;
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
        auto print_argument = [&] {
            if (as_operand) {
                argument_.print_as_operand_to(output);
            } else {
                argument_.print_to(output);
            }
        };

        auto* renderer = static_cast<argument_color_renderer*>(
            output.pword(argument_color_renderer_index()));
        if (renderer != nullptr) {
            renderer->begin_argument_color(output, color_);
            try {
                print_argument();
            } catch (...) {
                try {
                    renderer->end_argument_color(output);
                } catch (...) {
                }
                throw;
            }
            renderer->end_argument_color(output);
            return;
        }

        print_html_markup(output, opening_markup());
        print_argument();
        print_html_markup(output, "</span>");
    }

    [[nodiscard]] std::string_view opening_markup() const noexcept {
        switch (color_) {
        case argument_color::red:
            return "<span class=\"wor\">";
        case argument_color::green:
            return "<span class=\"wog\">";
        case argument_color::blue:
            return "<span class=\"wob\">";
        case argument_color::dark_orange:
            return "<span class=\"woo\">";
        case argument_color::munsell_purple:
            return "<span class=\"wop\">";
        }
        return {};
    }

    quoted_expression argument_;
    argument_color color_;
};

class quoted_basis_node_base : public quoted_node {
public:
    [[nodiscard]] quoted_node_kind kind() const noexcept final {
        return quoted_node_kind::basis;
    }

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
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

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_.view();
    }

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

[[nodiscard]] inline bool
is_quoted_atomic(quoted_expression const& expression) noexcept {
    auto const& root = quoted_access::root(expression);
    return root->atomic_kind() != quoted_atomic_kind::none;
}

[[nodiscard]] inline bool
same_quoted_atom(quoted_expression const& left,
                 quoted_expression const& right) {
    auto const& left_root = quoted_access::root(left);
    auto const& right_root = quoted_access::root(right);
    auto const left_kind = left_root->atomic_kind();
    return left_kind != quoted_atomic_kind::none &&
           left_kind == right_root->atomic_kind() &&
           left_root->atomic_name() == right_root->atomic_name();
}

[[nodiscard]] inline bool
contains_quoted_atom(quoted_expression const& atom,
                     quoted_expression const& expression) {
    if (same_quoted_atom(atom, expression)) {
        return true;
    }

    auto const& root = quoted_access::root(expression);
    switch (root->kind()) {
    case quoted_node_kind::application: {
        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        return contains_quoted_atom(atom, application.function()) ||
               contains_quoted_atom(atom, application.argument());
    }
    case quoted_node_kind::pending_sk:
        return contains_quoted_atom(
            atom,
            static_cast<quoted_pending_sk_node const&>(*root)
                .application());
    case quoted_node_kind::recursive_y:
        return contains_quoted_atom(
            atom,
            static_cast<quoted_recursive_y_node const&>(*root).generator());
    case quoted_node_kind::basis_argument:
        return contains_quoted_atom(
            atom,
            static_cast<quoted_basis_argument_node const&>(*root).argument());
    case quoted_node_kind::colored_argument:
        return contains_quoted_atom(
            atom,
            static_cast<quoted_colored_argument_node const&>(*root).argument());
    default:
        return false;
    }
}

[[nodiscard]] inline quoted_expression
make_quoted_primitive(quoted_node_kind kind) {
    return quoted_access::make(std::make_shared<quoted_primitive_node>(kind));
}

[[nodiscard]] inline quoted_expression
make_quoted_rec_func(basis_label name) {
    return quoted_access::make(
        std::make_shared<quoted_rec_func_node>(std::move(name)));
}

[[nodiscard]] inline quoted_expression
make_quoted_application(quoted_expression function,
                        quoted_expression argument) {
    return quoted_access::make(std::make_shared<quoted_application_node>(
        std::move(function), std::move(argument)));
}

[[nodiscard]] inline quoted_expression
make_quoted_pending_sk(quoted_expression application) {
    return quoted_access::make(
        std::make_shared<quoted_pending_sk_node>(
            std::move(application)));
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

[[nodiscard]] inline quoted_expression make_quoted_colored_argument(
    quoted_expression argument,
    argument_color color) {
    return quoted_access::make(
        std::make_shared<quoted_colored_argument_node>(
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

class quoted_atomic {
public:
    quoted_atomic(quoted_atomic const&) = default;
    quoted_atomic(quoted_atomic&&) noexcept = default;
    quoted_atomic& operator=(quoted_atomic const&) = default;
    quoted_atomic& operator=(quoted_atomic&&) noexcept = default;

    explicit quoted_atomic(quoted_expression expression)
        : expression_(std::move(expression)) {
        if (!detail::is_quoted_atomic(expression_)) {
            throw std::invalid_argument(
                "combdsl::quoted_atomic requires a quoted symbol or "
                "symbolic string or recursive function");
        }
    }

    template <class Value>
        requires (!std::same_as<
                  std::remove_cvref_t<Value>, quoted_atomic>)
    explicit quoted_atomic(Value&& value)
        : quoted_atomic(quote(std::forward<Value>(value))) {}

    [[nodiscard]] quoted_expression const& expression() const noexcept {
        return expression_;
    }

private:
    quoted_expression expression_;
};

inline void quoted_expression::print_to(std::ostream& output) const {
    detail::print_scope scope(output);
    root_->print_to(output);
}

inline void quoted_expression::print_as_operand_to(std::ostream& output) const {
    detail::print_scope scope(output);

    if (root_->is_application()) {
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

[[nodiscard]] inline quoted_expression
takeout(quoted_atomic qa, quoted_expression qe);

namespace detail {

struct reduction_options {
    bool basis_step = false;
    bool reduce_recursive_y = true;
    bool reduce_partial_k_argument = true;
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
    case quoted_node_kind::pending_sk:
        return make_quoted_pending_sk(
            restore_basis_arguments(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
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
strip_argument_colors(quoted_expression const& expression) {
    auto const& root = quoted_access::root(expression);
    switch (root->kind()) {
    case quoted_node_kind::colored_argument:
        return strip_argument_colors(
            static_cast<quoted_colored_argument_node const&>(*root)
                .argument());
    case quoted_node_kind::application: {
        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        return make_quoted_application(
            strip_argument_colors(application.function()),
            strip_argument_colors(application.argument()));
    }
    case quoted_node_kind::pending_sk:
        return make_quoted_pending_sk(
            strip_argument_colors(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
    case quoted_node_kind::recursive_y: {
        auto const& recursive =
            static_cast<quoted_recursive_y_node const&>(*root);
        return make_quoted_recursive_y(
            strip_argument_colors(recursive.generator()));
    }
    case quoted_node_kind::basis_argument:
        return make_quoted_basis_argument(
            strip_argument_colors(
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

struct pending_sk_reduction {
    quoted_expression result;
    quoted_expression before;
    quoted_expression after;
};

[[nodiscard]] inline bool
is_partially_applied_k(quoted_expression const& expression) {
    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::application) {
        return false;
    }

    auto const& application =
        static_cast<quoted_application_node const&>(*root);
    return quoted_access::root(application.function())->kind() ==
           quoted_node_kind::constant;
}

[[nodiscard]] inline std::optional<pending_sk_reduction>
reduce_pending_sk_applications(
    quoted_expression const& expression,
    std::size_t& replacements,
    bool reduce_partial_k_argument) {
    struct traversal_frame {
        quoted_expression expression;
        bool children_visited = false;
    };

    std::vector<traversal_frame> traversal;
    std::vector<std::optional<pending_sk_reduction>> results;
    traversal.push_back({expression});

    while (!traversal.empty()) {
        auto frame = std::move(traversal.back());
        traversal.pop_back();
        auto const& root = quoted_access::root(frame.expression);

        if (root->kind() == quoted_node_kind::pending_sk) {
            auto const& pending =
                static_cast<quoted_pending_sk_node const&>(*root);
            auto const identity =
                make_quoted_primitive(quoted_node_kind::identity);
            auto const color =
                replacements++ == 0
                    ? argument_color::red
                    : argument_color::green;
            results.emplace_back(pending_sk_reduction{
                identity,
                make_quoted_colored_argument(
                    pending.application(), color),
                make_quoted_colored_argument(identity, color)});
            continue;
        }

        if (root->kind() != quoted_node_kind::application) {
            results.emplace_back(std::nullopt);
            continue;
        }

        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        if (!frame.children_visited) {
            if (!reduce_partial_k_argument &&
                is_partially_applied_k(frame.expression)) {
                results.emplace_back(std::nullopt);
                continue;
            }
            frame.children_visited = true;
            traversal.push_back(std::move(frame));
            traversal.push_back({application.argument()});
            traversal.push_back({application.function()});
            continue;
        }

        auto reduced_argument = std::move(results.back());
        results.pop_back();
        auto reduced_function = std::move(results.back());
        results.pop_back();
        if (!reduced_function && !reduced_argument) {
            results.emplace_back(std::nullopt);
            continue;
        }

        results.emplace_back(pending_sk_reduction{
            make_quoted_application(
                reduced_function
                    ? reduced_function->result
                    : application.function(),
                reduced_argument
                    ? reduced_argument->result
                    : application.argument()),
            make_quoted_application(
                reduced_function
                    ? reduced_function->before
                    : application.function(),
                reduced_argument
                    ? reduced_argument->before
                    : application.argument()),
            make_quoted_application(
                reduced_function
                    ? reduced_function->after
                    : application.function(),
                reduced_argument
                    ? reduced_argument->after
                    : application.argument())});
    }

    return std::move(results.back());
}

[[nodiscard]] inline std::optional<quoted_expression>
reduce_sk_application_at_head(
    quoted_expression const& expression,
    reduction_trace* trace) {
    std::vector<quoted_expression> arguments;
    auto head = expression;

    while (quoted_access::root(head)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(head));
        arguments.push_back(application.argument());
        head = application.function();
    }

    std::reverse(arguments.begin(), arguments.end());
    if (quoted_access::root(head)->kind() !=
            quoted_node_kind::substitution ||
        arguments.size() < 2 ||
        quoted_access::root(arguments[0])->kind() !=
            quoted_node_kind::constant) {
        return std::nullopt;
    }

    auto append_arguments = [&arguments](
                                quoted_expression result,
                                std::size_t first) {
        for (; first < arguments.size(); ++first) {
            result = result(arguments[first]);
        }
        return result;
    };

    auto const identity =
        make_quoted_primitive(quoted_node_kind::identity);
    auto result = identity;
    std::size_t first_trailing_argument = 2;
    auto const reduce_second_sk =
        arguments.size() >= 3 &&
        is_quoted_sk_application(arguments[2]);

    if (reduce_second_sk) {
        result = result(identity);
        first_trailing_argument = 3;
    }

    if (trace != nullptr) {
        auto sk_application = make_quoted_application(
            make_quoted_application(head, arguments[0]),
            arguments[1]);
        auto before = make_quoted_colored_argument(
            std::move(sk_application),
            argument_color::red);
        auto after = make_quoted_colored_argument(
            identity, argument_color::red);

        if (reduce_second_sk) {
            before = before(make_quoted_colored_argument(
                arguments[2], argument_color::green));
            after = after(make_quoted_colored_argument(
                identity, argument_color::green));
        }

        trace->before = append_arguments(
            std::move(before), first_trailing_argument);
        auto traced_after = append_arguments(
            std::move(after), first_trailing_argument);
        trace->after = traced_after;
        return traced_after;
    }

    return append_arguments(
        std::move(result), first_trailing_argument);
}

[[nodiscard]] inline quoted_expression
reduce_at_head(
    quoted_expression expression,
    reduction_options options,
    reduction_trace* trace) {
    if (auto reduced =
            reduce_sk_application_at_head(expression, trace)) {
        return std::move(*reduced);
    }

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

        constexpr argument_color colors[] = {
            argument_color::red,
            argument_color::green,
            argument_color::blue,
            argument_color::dark_orange,
            argument_color::munsell_purple,
        };
        auto const colored_arguments =
            std::min(arity, std::size(colors));
        for (std::size_t index = 0;
             index < colored_arguments;
             ++index) {
            reversed_arguments[index] = make_quoted_colored_argument(
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
            auto const mark_function_sk =
                is_quoted_sk(reversed_arguments[0]);
            auto const mark_argument_sk =
                is_quoted_sk(reversed_arguments[1]);
            prepare_trace(3);
            auto const& function = reversed_arguments[0];
            auto const& argument = reversed_arguments[1];
            auto const& value = reversed_arguments[2];
            auto function_application = function(value);
            if (mark_function_sk) {
                function_application = make_quoted_pending_sk(
                    std::move(function_application));
            }
            auto argument_application = argument(value);
            if (mark_argument_sk) {
                argument_application = make_quoted_pending_sk(
                    std::move(argument_application));
            }
            auto result = function_application(
                std::move(argument_application));
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
        if (!options.reduce_recursive_y) {
            break;
        }

        auto const& recursive =
            static_cast<quoted_recursive_y_node const&>(
                *quoted_access::root(head));
        if (trace != nullptr) {
            head = make_quoted_recursive_y(
                make_quoted_colored_argument(
                    recursive.generator(), argument_color::red));
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

            reduction_options const basis_reduction{
                .basis_step = false,
                .reduce_recursive_y = false,
                .reduce_partial_k_argument =
                    options.reduce_partial_k_argument,
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
    std::size_t pending_replacements = 0;
    if (auto pending = reduce_pending_sk_applications(
            expression,
            pending_replacements,
            options.reduce_partial_k_argument)) {
        if (trace != nullptr) {
            trace->before = pending->before;
            trace->after = pending->after;
            return pending->after;
        }
        return pending->result;
    }

    struct path_frame {
        quoted_expression function;
        quoted_expression argument;
        bool visiting_argument = false;
    };

    std::vector<path_frame> path;
    auto current = expression;

    for (;;) {
        auto reduced = reduce_at_head(current, options, trace);
        if (quoted_access::root(reduced) !=
            quoted_access::root(current)) {
            for (auto frame = path.rbegin();
                 frame != path.rend();
                 ++frame) {
                if (frame->visiting_argument) {
                    reduced = make_quoted_application(
                        frame->function, std::move(reduced));
                    if (trace != nullptr) {
                        trace->before = make_quoted_application(
                            frame->function,
                            std::move(*trace->before));
                    }
                } else {
                    reduced = make_quoted_application(
                        std::move(reduced), frame->argument);
                    if (trace != nullptr) {
                        trace->before = make_quoted_application(
                            std::move(*trace->before),
                            frame->argument);
                    }
                }
                if (trace != nullptr) {
                    trace->after = reduced;
                }
            }
            return reduced;
        }

        auto const& root = quoted_access::root(current);
        if (root->kind() == quoted_node_kind::application) {
            if (options.reduce_partial_k_argument ||
                !is_partially_applied_k(current)) {
                auto const& application =
                    static_cast<quoted_application_node const&>(*root);
                path.push_back({
                    application.function(),
                    application.argument(),
                });
                current = application.function();
                continue;
            }
        }

        bool found_argument = false;
        while (!path.empty()) {
            auto& frame = path.back();
            if (!frame.visiting_argument) {
                frame.visiting_argument = true;
                current = frame.argument;
                found_argument = true;
                break;
            }
            path.pop_back();
        }
        if (!found_argument) {
            return std::nullopt;
        }
    }
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

[[nodiscard]] inline quoted_expression color_step_html(
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
        return detail::strip_argument_colors(after);
    }
    return expression;
}

[[nodiscard]] inline quoted_expression color_step_html(
    quoted_expression expression,
    bool basis_step) {
    return color_step_html(
        std::move(expression), std::cout, basis_step);
}

using evaluation_progress_callback =
    std::function<void(std::size_t)>;

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

class evaluation_progress_reporter {
public:
    explicit evaluation_progress_reporter(
        evaluation_progress_callback const& callback)
        : callback_(callback) {}

    void completed_reduction() {
        ++reductions_;
        if (!callback_) {
            return;
        }

        callback_(reductions_);
    }

private:
    evaluation_progress_callback const& callback_;
    std::size_t reductions_ = 0;
};

} // namespace detail

inline void eval(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    detail::scoped_evaluation_sigint_handler sigint_handler;
    detail::evaluation_progress_reporter progress(progress_callback);
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

        auto next = expression;
        if (auto reduced = detail::reduce_next_redex(
                expression,
                detail::reduction_options{
                    .basis_step = basis_step,
                    .reduce_partial_k_argument = false,
                })) {
            next = std::move(*reduced);
        }
        auto const no_reduction =
            detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression);
        if (!no_reduction) {
            progress.completed_reduction();
        }
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

inline void eval(
    quoted_expression expression,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    eval(
        std::move(expression),
        output,
        input,
        basis_step,
        evaluation_progress_callback{});
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

inline void single_step_run(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback);

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
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    detail::scoped_evaluation_sigint_handler sigint_handler;
    detail::evaluation_progress_reporter progress(progress_callback);

    output.flush();

    for (;;) {
        if (!detail::wait_after_single_step_run_interrupt(input, output)) {
            return;
        }

        auto next = single_step(expression, basis_step);
        auto const no_reduction =
            detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression);
        if (!no_reduction) {
            progress.completed_reduction();
        }
        if (!detail::wait_after_single_step_run_interrupt(input, output)) {
            return;
        }

        if (no_reduction) {
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
    std::ostream& output,
    std::istream& input,
    bool basis_step) {
    single_step_run(
        std::move(expression),
        output,
        input,
        basis_step,
        evaluation_progress_callback{});
}

namespace detail {

class registered_parser_basis {
public:
    registered_parser_basis(std::string name, bool predefined)
        : name_(std::move(name)), predefined_(predefined) {}

    virtual ~registered_parser_basis() = default;

    [[nodiscard]] std::string const& name() const noexcept {
        return name_;
    }

    [[nodiscard]] bool predefined() const noexcept {
        return predefined_;
    }

    [[nodiscard]] virtual quoted_expression expression() const = 0;

private:
    std::string name_;
    bool predefined_;
};

template <class Basis>
class registered_parser_basis_model final : public registered_parser_basis {
public:
    registered_parser_basis_model(
        std::string name,
        Basis const& basis,
        bool predefined)
        : registered_parser_basis(std::move(name), predefined),
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

[[nodiscard]] inline std::vector<std::string>&
parser_definition_registry() {
    static std::vector<std::string> definitions;
    return definitions;
}

[[nodiscard]] inline bool is_primitive_name(
    std::string_view name) noexcept {
    return name == "S" || name == "K" || name == "I" || name == "Y";
}

enum class parser_definition_change {
    inserted,
    unchanged,
    replaced,
    rejected_predefined
};

struct parser_definition_inspection {
    parser_definition_change change;
    std::string replaced_definition;
};

[[nodiscard]] inline bool same_parser_definition_expression(
    quoted_expression const& left,
    quoted_expression const& right) {
    using quoted_root = std::shared_ptr<quoted_node const>;
    using quoted_root_pair = std::pair<quoted_root, quoted_root>;

    struct root_pair_hash {
        [[nodiscard]] std::size_t operator()(
            quoted_root_pair const& value) const noexcept {
            auto const left_hash =
                std::hash<quoted_node const*>{}(value.first.get());
            auto const right_hash =
                std::hash<quoted_node const*>{}(value.second.get());
            return left_hash ^
                   (right_hash +
                    static_cast<std::size_t>(
                        0x9e3779b97f4a7c15ULL) +
                    (left_hash << 6) +
                    (left_hash >> 2));
        }
    };

    struct root_pair_equal {
        [[nodiscard]] bool operator()(
            quoted_root_pair const& left_pair,
            quoted_root_pair const& right_pair) const noexcept {
            return left_pair.first == right_pair.first &&
                   left_pair.second == right_pair.second;
        }
    };

    std::vector<quoted_root_pair> pending;
    pending.emplace_back(
        quoted_access::root(left),
        quoted_access::root(right));
    std::unordered_set<
        quoted_root_pair,
        root_pair_hash,
        root_pair_equal> compared;

    auto add_pair = [&pending](
        quoted_expression const& left_expression,
        quoted_expression const& right_expression) {
        pending.emplace_back(
            quoted_access::root(left_expression),
            quoted_access::root(right_expression));
    };

    while (!pending.empty()) {
        auto [left_root, right_root] =
            std::move(pending.back());
        pending.pop_back();
        if (left_root == right_root) {
            continue;
        }
        if (!compared.emplace(left_root, right_root).second) {
            continue;
        }
        if (left_root->kind() != right_root->kind()) {
            return false;
        }

        auto const left_atomic = left_root->atomic_kind();
        auto const right_atomic = right_root->atomic_kind();
        if (left_atomic != quoted_atomic_kind::none ||
            right_atomic != quoted_atomic_kind::none) {
            if (left_atomic == quoted_atomic_kind::none ||
                left_atomic != right_atomic ||
                left_root->atomic_name() !=
                    right_root->atomic_name()) {
                return false;
            }
            continue;
        }

        switch (left_root->kind()) {
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
            break;
        case quoted_node_kind::application: {
            auto const& left_application =
                static_cast<quoted_application_node const&>(
                    *left_root);
            auto const& right_application =
                static_cast<quoted_application_node const&>(
                    *right_root);
            add_pair(
                left_application.function(),
                right_application.function());
            add_pair(
                left_application.argument(),
                right_application.argument());
            break;
        }
        case quoted_node_kind::pending_sk:
            add_pair(
                static_cast<quoted_pending_sk_node const&>(
                    *left_root).application(),
                static_cast<quoted_pending_sk_node const&>(
                    *right_root).application());
            break;
        case quoted_node_kind::recursive_y:
            add_pair(
                static_cast<quoted_recursive_y_node const&>(
                    *left_root).generator(),
                static_cast<quoted_recursive_y_node const&>(
                    *right_root).generator());
            break;
        case quoted_node_kind::basis_argument:
            add_pair(
                static_cast<quoted_basis_argument_node const&>(
                    *left_root).argument(),
                static_cast<quoted_basis_argument_node const&>(
                    *right_root).argument());
            break;
        case quoted_node_kind::basis: {
            auto const& left_basis =
                static_cast<quoted_basis_node_base const&>(
                    *left_root);
            auto const& right_basis =
                static_cast<quoted_basis_node_base const&>(
                    *right_root);
            if (left_basis.name() != right_basis.name() ||
                left_basis.arity() != right_basis.arity()) {
                return false;
            }
            add_pair(left_basis.body(), right_basis.body());
            break;
        }
        case quoted_node_kind::opaque:
        case quoted_node_kind::rec_func:
        case quoted_node_kind::colored_argument:
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool same_parser_basis_definition(
    quoted_expression const& left,
    quoted_expression const& right) {
    auto const& left_root = quoted_access::root(left);
    auto const& right_root = quoted_access::root(right);
    if (left_root->kind() != quoted_node_kind::basis ||
        right_root->kind() != quoted_node_kind::basis) {
        return false;
    }

    auto const& left_basis =
        static_cast<quoted_basis_node_base const&>(*left_root);
    auto const& right_basis =
        static_cast<quoted_basis_node_base const&>(*right_root);
    return left_basis.name() == right_basis.name() &&
           left_basis.arity() == right_basis.arity() &&
           same_parser_definition_expression(
               left_basis.body(), right_basis.body());
}

[[nodiscard]] inline std::string format_parser_basis_definition(
    quoted_expression const& expression) {
    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::basis) {
        throw std::logic_error(
            "combdsl::registered parser basis is not a basis");
    }

    auto const& basis =
        static_cast<quoted_basis_node_base const&>(*root);
    std::ostringstream output;
    output << basis.name() << '=' << basis.arity() << ' ';
    basis.body().print_to(output);
    return output.str();
}

template <class Basis>
void register_parser_basis(std::string_view name, Basis const& basis) {
    if (is_primitive_name(name)) {
        return;
    }

    auto registration =
        std::make_shared<registered_parser_basis_model<Basis>>(
            std::string(name), basis, true);

    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    auto const existing = entries.find(name);
    if (existing == entries.end()) {
        entries.emplace(std::string(name), std::move(registration));
        return;
    }
    if (existing->second->predefined()) {
        return;
    }

    auto message =
        std::string("combdsl::basis name is already user-defined: ");
    message += name;
    throw std::invalid_argument(message);
}

[[nodiscard]] inline parser_definition_inspection
inspect_parser_definition_basis(
    std::string_view name,
    quoted_expression const& basis) {
    if (is_primitive_name(name)) {
        return {
            parser_definition_change::rejected_predefined, {}};
    }

    registered_parser_basis_ptr existing;
    {
        std::lock_guard lock(parser_basis_registry_mutex());
        auto const& entries = parser_basis_registry();
        auto const match = entries.find(name);
        if (match == entries.end()) {
            return {parser_definition_change::inserted, {}};
        }
        existing = match->second;
    }

    if (existing->predefined()) {
        return {
            parser_definition_change::rejected_predefined, {}};
    }
    if (same_parser_basis_definition(
            existing->expression(), basis)) {
        return {parser_definition_change::unchanged, {}};
    }
    return {
        parser_definition_change::replaced,
        format_parser_basis_definition(existing->expression())};
}

[[nodiscard]] inline parser_definition_change
register_parser_definition_basis(
    std::string_view name,
    quoted_expression const& basis,
    std::string user_source) {
    if (is_primitive_name(name)) {
        return parser_definition_change::rejected_predefined;
    }

    auto registration =
        std::make_shared<
            registered_parser_basis_model<quoted_expression>>(
            std::string(name), basis, false);

    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    auto const existing = entries.find(name);
    if (existing != entries.end()) {
        if (existing->second->predefined()) {
            return parser_definition_change::rejected_predefined;
        }
        if (same_parser_basis_definition(
                existing->second->expression(), basis)) {
            return parser_definition_change::unchanged;
        }

        parser_definition_registry().push_back(
            std::move(user_source));
        existing->second = std::move(registration);
        return parser_definition_change::replaced;
    }

    auto [entry, inserted] =
        entries.emplace(std::string(name), std::move(registration));
    if (!inserted) {
        throw std::logic_error(
            "combdsl::parser basis insertion unexpectedly failed");
    }
    try {
        parser_definition_registry().push_back(
            std::move(user_source));
    } catch (...) {
        entries.erase(entry);
        throw;
    }
    return parser_definition_change::inserted;
}

[[nodiscard]] inline registered_parser_basis_table
registered_parser_bases_snapshot() {
    std::lock_guard lock(parser_basis_registry_mutex());
    return parser_basis_registry();
}

[[nodiscard]] inline bool same_registered_parser_bases(
    registered_parser_basis_table const& left,
    registered_parser_basis_table const& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (auto const& [name, left_basis] : left) {
        auto const match = right.find(name);
        if (match == right.end()) {
            return false;
        }

        auto const& right_basis = match->second;
        if (left_basis == right_basis) {
            continue;
        }
        if (left_basis->predefined() !=
            right_basis->predefined()) {
            return false;
        }
        if (!same_parser_basis_definition(
                left_basis->expression(),
                right_basis->expression())) {
            return false;
        }
    }
    return true;
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

[[nodiscard]] inline std::string set_list() {
    std::lock_guard lock(detail::parser_basis_registry_mutex());
    auto const& definitions = detail::parser_definition_registry();

    std::string result;
    for (auto const& definition : definitions) {
        if (!result.empty()) {
            result.push_back('\n');
        }
        result += definition;
    }
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
          position_(position),
          detail_(message) {}

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

    [[nodiscard]] std::string_view detail() const noexcept {
        return detail_;
    }

private:
    [[nodiscard]] static std::string make_message(
        std::size_t position,
        std::string_view message) {
        auto result = std::string("Parse error at position ");
        auto const displayed_position =
            position == std::numeric_limits<std::size_t>::max()
                ? position
                : position + 1;
        result += std::to_string(displayed_position);
        result += ": ";
        result += message;
        return result;
    }

    std::size_t position_;
    std::string detail_;
};

namespace detail {

enum class parser_definition_mode {
    register_definitions,
    inspect_definitions
};

struct parsed_input {
    quoted_expression expression;
    bool is_definition;
    bool is_display_only;
    std::string replaced_definition;
};

class quoted_expression_parser {
public:
    explicit quoted_expression_parser(
        std::string_view source,
        parser_definition_mode definition_mode)
        : source_(source),
          registered_bases_(registered_parser_bases_snapshot()),
          definition_mode_(definition_mode) {}

    [[nodiscard]] parsed_input parse_input() {
        skip_whitespace();
        if (at_end()) {
            fail("expected an expression");
        }
        if (current() == ')') {
            fail("unexpected ')'");
        }

        auto const is_set_definition =
            begins_command("set");
        auto const is_define_definition =
            begins_command("define");
        auto const is_show_command =
            begins_command("show");
        auto const is_definition =
            is_set_definition || is_define_definition;
        auto result = is_set_definition
            ? parse_set_definition()
            : is_define_definition
                ? parse_define_definition()
                : is_show_command
                    ? parse_show_command()
                    : parse_expression();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }
        return {
            std::move(result),
            is_definition,
            is_show_command,
            std::move(replaced_definition_)};
    }

private:
    [[nodiscard]] bool begins_command(
        std::string_view keyword) const noexcept {
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
        auto name = parse_definition_basis_name();

        skip_whitespace();
        if (at_end() || current() != '=') {
            fail("expected '='");
        }
        ++position_;

        auto const arity = parse_optional_set_arity();
        auto const body_position = position_;
        auto body = parse_expression();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }

        auto user_source = canonical_set_definition(
            name.view(), arity, source_.substr(body_position));
        auto result = make_quoted_basis_snapshot(
            name, arity, std::move(body));
        finish_definition(
            name.view(),
            name_position,
            result,
            std::move(user_source));
        return result;
    }

    [[nodiscard]] quoted_expression parse_show_command() {
        constexpr std::size_t keyword_size = 4;
        position_ += keyword_size;
        skip_whitespace();

        auto const name_position = position_;
        auto name_end = source_.size();
        while (name_end > name_position &&
               is_whitespace(source_[name_end - 1])) {
            --name_end;
        }
        if (name_end == name_position) {
            fail("expected a name");
        }

        auto const name =
            source_.substr(name_position, name_end - name_position);
        position_ = source_.size();

        if (is_primitive_name(name)) {
            auto message = std::string(name);
            message += " is a fundamental name";
            return quote(std::move(message));
        }

        auto const match = registered_bases_.find(name);
        if (match == registered_bases_.end()) {
            auto message = unescape_input(name);
            message += " is not a defined name";
            throw parse_error(name_position, message);
        }

        auto result = match->second->expression();
        auto const& root = quoted_access::root(result);
        if (root->kind() != quoted_node_kind::basis) {
            throw std::logic_error(
                "combdsl::registered parser basis is not a basis");
        }
        return static_cast<quoted_basis_node_base const&>(*root)
            .body();
    }

    [[nodiscard]] quoted_expression parse_define_definition() {
        constexpr std::size_t keyword_size = 6;
        position_ += keyword_size;
        skip_whitespace();

        auto const name_position = position_;
        auto [name, symbols] = parse_define_signature();
        auto const body_position = position_;
        auto recursive_function = make_quoted_rec_func(name);
        recursive_function_.emplace(recursive_function);
        auto body = parse_expression();
        recursive_function_.reset();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }

        auto user_source = canonical_define_definition(
            name.view(), symbols, source_.substr(body_position));
        for (auto symbol_position = symbols.rbegin();
             symbol_position != symbols.rend();
             ++symbol_position) {
            body = takeout(
                quoted_atomic{symbol(*symbol_position)},
                std::move(body));
        }
        if (contains_quoted_atom(recursive_function, body)) {
            body = takeout(
                quoted_atomic{recursive_function},
                std::move(body));
            body = quote(Y)(
                optimize_final_takeout(std::move(body)));
        } else {
            body = optimize_final_takeout(std::move(body));
        }

        auto result = make_quoted_basis_snapshot(
            name, symbols.size(), std::move(body));
        finish_definition(
            name.view(),
            name_position,
            result,
            std::move(user_source));
        return result;
    }

    void finish_definition(
        std::string_view name,
        std::size_t name_position,
        quoted_expression const& result,
        std::string user_source) {
        parser_definition_change change;
        if (definition_mode_ ==
            parser_definition_mode::inspect_definitions) {
            auto inspection =
                inspect_parser_definition_basis(name, result);
            change = inspection.change;
            replaced_definition_ =
                std::move(inspection.replaced_definition);
        } else {
            change = register_parser_definition_basis(
                name, result, std::move(user_source));
        }

        if (change ==
            parser_definition_change::rejected_predefined) {
            auto message = std::string(name);
            message +=
                " is a pre-defined basis and cannot be redefined";
            throw parse_error(name_position, message);
        }
    }

    [[nodiscard]] basis_label parse_definition_basis_name() {
        auto const [name_text, name_position] =
            parse_definition_basis_name_token();
        return validated_definition_basis_name(
            name_text, name_position);
    }

    [[nodiscard]] std::pair<std::string_view, std::size_t>
    parse_definition_basis_name_token() {
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
        return {name_text, name_position};
    }

    [[nodiscard]] std::pair<basis_label, std::string>
    parse_define_signature() {
        auto const [token, name_position] =
            parse_definition_basis_name_token();
        if (auto symbols =
                parse_adjacent_definition_symbols(token)) {
            auto const name_size = token.size() - symbols->size();
            auto name = validated_definition_basis_name(
                token.substr(0, name_size), name_position);
            return {std::move(name), std::move(*symbols)};
        }

        auto name = validated_definition_basis_name(
            token, name_position);
        auto symbols = parse_definition_symbols();
        return {std::move(name), std::move(symbols)};
    }

    [[nodiscard]] std::string parse_definition_symbols() {
        skip_whitespace();
        std::string symbols;

        while (!at_end()) {
            if (current() == '=') {
                if (symbols.empty()) {
                    fail("expected at least one symbol");
                }
                ++position_;
                return symbols;
            }
            if (is_whitespace(current())) {
                ++position_;
                continue;
            }
            if (current() < 'a' || current() > 'z') {
                fail("expected a lowercase symbol or '='");
            }
            symbols.push_back(current());
            ++position_;
        }

        if (symbols.empty()) {
            fail("expected at least one symbol");
        }
        fail("expected '='");
    }

    [[nodiscard]] std::optional<std::string>
    parse_adjacent_definition_symbols(std::string_view token) {
        if (token.size() < 2) {
            return std::nullopt;
        }

        std::size_t name_size = 0;
        auto const maximum_name_size =
            std::min<std::size_t>(4, token.size() - 1);
        for (std::size_t length = 1;
             length <= maximum_name_size;
             ++length) {
            if (is_single_utf8_char(token.substr(0, length))) {
                name_size = length;
                break;
            }
        }
        if (name_size == 0) {
            return std::nullopt;
        }

        auto const symbols = token.substr(name_size);
        for (auto symbol : symbols) {
            if (symbol < 'a' || symbol > 'z') {
                return std::nullopt;
            }
        }

        auto equals_position = position_;
        while (equals_position < source_.size() &&
               is_whitespace(source_[equals_position])) {
            ++equals_position;
        }
        if (equals_position == source_.size() ||
            source_[equals_position] != '=') {
            return std::nullopt;
        }

        position_ = equals_position + 1;
        return std::string(symbols);
    }

    [[nodiscard]] static bool is_named_basis(
        quoted_expression const& expression,
        std::string_view name) noexcept {
        auto const& root = quoted_access::root(expression);
        return root->kind() == quoted_node_kind::basis &&
               static_cast<quoted_basis_node_base const&>(*root)
                       .name() == name;
    }

    [[nodiscard]] static quoted_application_node const*
    as_application(quoted_expression const& expression) noexcept {
        auto const& root = quoted_access::root(expression);
        if (root->kind() != quoted_node_kind::application) {
            return nullptr;
        }
        return std::addressof(
            static_cast<quoted_application_node const&>(*root));
    }

    [[nodiscard]] static bool is_bluebird_cardinal_thrush(
        quoted_expression const& expression) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), "T")) {
            return false;
        }

        auto const* inner = as_application(outer->function());
        return inner != nullptr &&
               is_named_basis(inner->function(), "B") &&
               is_named_basis(inner->argument(), "C");
    }

    [[nodiscard]] static bool is_double_bluebird(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "B");
    }

    [[nodiscard]] quoted_expression registered_basis_expression(
        std::string_view name) const {
        auto const match = registered_bases_.find(name);
        if (match == registered_bases_.end()) {
            throw std::logic_error(
                "combdsl::define optimization basis is not registered");
        }
        return match->second->expression();
    }

    [[nodiscard]] quoted_expression optimize_final_takeout(
        quoted_expression expression) const {
        auto const* application = as_application(expression);
        if (application == nullptr) {
            return expression;
        }

        auto function =
            optimize_final_takeout(application->function());
        auto argument =
            optimize_final_takeout(application->argument());
        if (quoted_access::root(function) !=
                quoted_access::root(application->function()) ||
            quoted_access::root(argument) !=
                quoted_access::root(application->argument())) {
            expression = make_quoted_application(
                std::move(function), std::move(argument));
        }

        if (is_bluebird_cardinal_thrush(expression)) {
            return registered_basis_expression("V");
        }
        if (is_double_bluebird(expression)) {
            return registered_basis_expression("D");
        }
        return expression;
    }

    [[nodiscard]] static std::string unescape_input(
        std::string_view source) {
        std::string result;
        result.reserve(source.size());

        for (std::size_t index = 0; index < source.size();) {
            if (source[index] == '\\' &&
                index + 1 < source.size() &&
                (source[index + 1] == '\\' ||
                 source[index + 1] == '"')) {
                result.push_back(source[index + 1]);
                index += 2;
            } else {
                result.push_back(source[index]);
                ++index;
            }
        }
        return result;
    }

    [[nodiscard]] static std::string append_canonical_body(
        std::string result,
        std::string_view body) {
        bool inside_word = false;
        bool pending_space = true;
        for (std::size_t index = 0; index < body.size();) {
            if (body[index] == '\\' &&
                index + 1 < body.size() &&
                body[index + 1] == '"') {
                if (!inside_word && pending_space) {
                    result.push_back(' ');
                    pending_space = false;
                }
                result += "\\\"";
                inside_word = !inside_word;
                index += 2;
                continue;
            }

            if (!inside_word && is_whitespace(body[index])) {
                pending_space = true;
                ++index;
                continue;
            }

            if (pending_space) {
                result.push_back(' ');
                pending_space = false;
            }
            result.push_back(body[index]);
            ++index;
        }
        return unescape_input(result);
    }

    [[nodiscard]] static std::string canonical_set_definition(
        std::string_view name,
        std::size_t arity,
        std::string_view body) {
        std::string result = "set ";
        result += name;
        result += " = ";
        result += std::to_string(arity);
        return append_canonical_body(std::move(result), body);
    }

    [[nodiscard]] static std::string canonical_define_definition(
        std::string_view name,
        std::string_view symbols,
        std::string_view body) {
        std::string result = "define ";
        result += name;
        result.push_back(' ');
        result += symbols;
        result += " =";
        return append_canonical_body(std::move(result), body);
    }

    [[nodiscard]] basis_label validated_definition_basis_name(
        std::string_view name,
        std::size_t name_position) const {
        try {
            return basis_label(name);
        } catch (std::length_error const& error) {
            throw parse_error(name_position + 15, error.what());
        } catch (std::invalid_argument const& error) {
            throw parse_error(name_position, error.what());
        }
    }

    [[nodiscard]] std::size_t parse_optional_set_arity() {
        skip_whitespace();

        auto const arity_position = position_;
        auto arity_end = position_;
        while (arity_end < source_.size() &&
               source_[arity_end] >= '0' &&
               source_[arity_end] <= '9') {
            ++arity_end;
        }

        if (arity_end == arity_position ||
            (arity_end < source_.size() &&
             !is_whitespace(source_[arity_end]))) {
            return 0;
        }

        std::size_t arity = 0;
        for (auto digit_position = arity_position;
             digit_position < arity_end;
             ++digit_position) {
            auto const digit = static_cast<std::size_t>(
                source_[digit_position] - '0');
            if (arity >
                (std::numeric_limits<std::size_t>::max() - digit) / 10) {
                throw parse_error(
                    arity_position, "basis arity is out of range");
            }
            arity = arity * 10 + digit;
        }

        position_ = arity_end;
        skip_whitespace();
        return arity;
    }

    [[nodiscard]] quoted_expression parse_expression() {
        std::optional<quoted_expression> result;
        bool previous_atom_requires_token_separator = false;
        skip_whitespace();

        while (!at_end() && current() != ')') {
            reject_spaced_unregistered_lowercase_name(
                result.has_value(),
                previous_atom_requires_token_separator);
            auto const atom_requires_token_separator =
                current_atom_requires_token_separator();
            auto atom = parse_atom();
            if (result.has_value()) {
                auto application = (*result)(std::move(atom));
                result = std::move(application);
            } else {
                result = std::move(atom);
            }
            previous_atom_requires_token_separator =
                atom_requires_token_separator;
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

        if (auto recursive_function =
                parse_exact_recursive_function_token()) {
            return std::move(*recursive_function);
        }

        if (auto named_basis = parse_named_basis_token()) {
            return std::move(*named_basis);
        }

        if (begins_with_unseparated_recursive_function(
                current_basis_token())) {
            fail("unknown operand");
        }

        if (auto recursive_function =
                parse_single_character_recursive_function()) {
            return std::move(*recursive_function);
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
    parse_exact_recursive_function_token() {
        if (!recursive_function_) {
            return std::nullopt;
        }

        auto const name =
            quoted_access::root(*recursive_function_)->atomic_name();
        if (current_basis_token() != name) {
            return std::nullopt;
        }

        position_ += name.size();
        return *recursive_function_;
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

    [[nodiscard]] bool begins_with_unseparated_recursive_function(
        std::string_view token) const noexcept {
        if (!recursive_function_) {
            return false;
        }

        auto const name =
            quoted_access::root(*recursive_function_)->atomic_name();
        return name.size() > 1 &&
               token.size() > name.size() &&
               token.starts_with(name);
    }

    void reject_spaced_unregistered_lowercase_name(
        bool has_previous_atom,
        bool previous_atom_requires_token_separator) const {
        auto const name = current_basis_token();
        if (name.size() < 2 ||
            !is_lowercase_name(name) ||
            !is_basis_token_start() ||
            registered_bases_.contains(name) ||
            is_recursive_function_name(name)) {
            return;
        }

        auto const separated_from_previous =
            has_previous_atom &&
            !previous_atom_requires_token_separator &&
            position_ != 0 &&
            is_whitespace(source_[position_ - 1]);
        if (separated_from_previous ||
            is_separated_from_next_atom(name.size())) {
            fail("unknown operand");
        }
    }

    [[nodiscard]] bool current_atom_requires_token_separator()
        const noexcept {
        if (current() == '\\') {
            return true;
        }

        auto const name = current_basis_token();
        return name.size() > 1 &&
               (registered_bases_.contains(name) ||
                is_recursive_function_name(name));
    }

    [[nodiscard]] static bool is_lowercase_name(
        std::string_view name) noexcept {
        return std::all_of(
            name.begin(), name.end(), [](char character) {
                return character >= 'a' && character <= 'z';
            });
    }

    [[nodiscard]] bool is_basis_token_start() const noexcept {
        if (position_ == 0 ||
            is_basis_token_delimiter(position_ - 1)) {
            return true;
        }

        return position_ >= 2 &&
               source_[position_ - 2] == '\\' &&
               (source_[position_ - 1] == '\\' ||
                source_[position_ - 1] == '"');
    }

    [[nodiscard]] bool is_recursive_function_name(
        std::string_view name) const noexcept {
        if (!recursive_function_) {
            return false;
        }

        auto const recursive_name =
            quoted_access::root(*recursive_function_)->atomic_name();
        return recursive_name == name;
    }

    [[nodiscard]] bool is_separated_from_next_atom(
        std::size_t name_size) const noexcept {
        auto next = position_ + name_size;
        if (next == source_.size() ||
            !is_whitespace(source_[next])) {
            return false;
        }

        while (next < source_.size() &&
               is_whitespace(source_[next])) {
            ++next;
        }
        return next < source_.size() && source_[next] != ')';
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_single_character_recursive_function() {
        if (!recursive_function_) {
            return std::nullopt;
        }

        auto const name =
            quoted_access::root(*recursive_function_)->atomic_name();
        if (name.size() != 1 || current() != name.front()) {
            return std::nullopt;
        }

        ++position_;
        return *recursive_function_;
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
        constexpr std::size_t maximum_basis_name_size = 15;
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
    std::optional<quoted_expression> recursive_function_;
    parser_definition_mode definition_mode_;
    std::string replaced_definition_;
};

[[nodiscard]] inline parsed_input parse_input(
    std::string_view source,
    parser_definition_mode definition_mode =
        parser_definition_mode::register_definitions) {
    return quoted_expression_parser(
        source, definition_mode).parse_input();
}

} // namespace detail

[[nodiscard]] inline quoted_expression parse(std::string_view source) {
    return std::move(detail::parse_input(source).expression);
}

inline void parse_eval(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return;
    }
    eval(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        progress_callback);
}

inline void parse_eval(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    parse_eval(
        source,
        output,
        input,
        basis_step,
        evaluation_progress_callback{});
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
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return;
    }
    single_step_run(
        std::move(parsed.expression), output, input, basis_step);
}

inline void parse_and_key_step(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return;
    }
    single_step_loop(
        std::move(parsed.expression), input, output, basis_step);
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

[[nodiscard]] inline quoted_expression
takeout(quoted_atomic qa, quoted_expression qe) {
    if (detail::same_quoted_atom(qa.expression(), qe)) {
        return detail::make_quoted_primitive(
            detail::quoted_node_kind::identity);
    }

    if (!detail::contains_quoted_atom(qa.expression(), qe)) {
        return detail::make_quoted_application(
            detail::make_quoted_primitive(
                detail::quoted_node_kind::constant),
            std::move(qe));
    }

    auto const& root = detail::quoted_access::root(qe);
    if (root->kind() == detail::quoted_node_kind::application) {
        auto const& application =
            static_cast<detail::quoted_application_node const&>(*root);
        auto const& qfun = application.function();
        auto const& qarg = application.argument();
        auto const qfun_is_qa =
            detail::same_quoted_atom(qa.expression(), qfun);
        auto const qarg_is_qa =
            detail::same_quoted_atom(qa.expression(), qarg);
        auto const qarg_contains_qa =
            detail::contains_quoted_atom(qa.expression(), qarg);
        auto const qfun_contains_qa =
            detail::contains_quoted_atom(qa.expression(), qfun);
        if (qfun_is_qa && qarg_is_qa) {
            return quote(M);
        }
        if (qfun_is_qa && !qarg_contains_qa) {
            return quote(T)(qarg);
        }
        if (qarg_is_qa && !qfun_contains_qa) {
            return qfun;
        }
        if (qfun_is_qa && qarg_contains_qa) {
            return quote(O)(takeout(qa, qarg));
        }
        if (qarg_is_qa && qfun_contains_qa) {
            return quote(W)(takeout(qa, qfun));
        }
        if (qfun_contains_qa && !qarg_contains_qa) {
            return quote(C)(takeout(qa, qfun))(qarg);
        }
        if (qarg_contains_qa && !qfun_contains_qa) {
            return quote(B)(qfun)(takeout(qa, qarg));
        }
        if (qfun_contains_qa && qarg_contains_qa) {
            return quote(S)(takeout(qa, qfun))(takeout(qa, qarg));
        }
    }

    throw std::logic_error(
        "combdsl::takeout has no matching case");
}

} // namespace combdsl
