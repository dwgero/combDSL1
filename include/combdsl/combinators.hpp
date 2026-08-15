/*
 * C++ Combinator DSL
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
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <csignal>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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

#if !defined(__EMSCRIPTEN__)
#include <semaphore>
#include <thread>
#endif

namespace combdsl {

struct constant;
class quoted_expression;
class quoted_atomic;

template <class Expression>
class deferred_basis_expression;

template <class Expression, class... Arguments>
class basis_application_expression;

namespace detail {

[[nodiscard]] inline std::recursive_mutex&
parser_definition_transaction_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

template <class Value>
class deferred_combinator;

template <class Function, class Argument>
class substitution_partial2;

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
    symbol,
    left_parenthesis,
    right_parenthesis,
    numeric,
    multicharacter_basis,
    compact_multicharacter_basis,
    digit_terminated_basis,
    nonalphanumeric_terminated_basis
};

[[nodiscard]] constexpr bool is_parenthesis(printed_token token) noexcept {
    return token == printed_token::left_parenthesis ||
           token == printed_token::right_parenthesis;
}

[[nodiscard]] constexpr bool is_multicharacter_basis(
    printed_token token) noexcept {
    return token == printed_token::multicharacter_basis ||
           token == printed_token::compact_multicharacter_basis ||
           token == printed_token::digit_terminated_basis ||
           token == printed_token::nonalphanumeric_terminated_basis;
}

[[nodiscard]] constexpr bool is_ascii_alphanumeric(char value) noexcept {
    return (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

[[nodiscard]] constexpr bool ends_with_non_alphanumeric(
    std::string_view text) noexcept {
    return !text.empty() && !is_ascii_alphanumeric(text.back());
}

[[nodiscard]] constexpr bool ends_with_ascii_digit(
    std::string_view text) noexcept {
    return !text.empty() && text.back() >= '0' && text.back() <= '9';
}

[[nodiscard]] constexpr bool ends_with_lowercase_ascii_letter(
    std::string_view text) noexcept {
    return !text.empty() &&
           text.back() >= 'a' &&
           text.back() <= 'z';
}

[[nodiscard]] constexpr printed_token basis_printed_token(
    std::string_view name) noexcept {
    if (ends_with_non_alphanumeric(name)) {
        return printed_token::nonalphanumeric_terminated_basis;
    }
    if (ends_with_ascii_digit(name)) {
        return printed_token::digit_terminated_basis;
    }
    if (name.size() <= 1) {
        return printed_token::other;
    }
    return ends_with_lowercase_ascii_letter(name)
        ? printed_token::multicharacter_basis
        : printed_token::compact_multicharacter_basis;
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
    auto const numeric_requires_separator =
        ((previous == printed_token::numeric &&
          !is_parenthesis(token)) ||
         (token == printed_token::numeric &&
          previous != printed_token::none &&
          !is_parenthesis(previous)));
    auto const digit_basis_requires_trailing_separator =
        previous == printed_token::digit_terminated_basis &&
        !is_parenthesis(token) &&
        token != printed_token::symbol;
    auto const follows_multicharacter_basis =
        digit_basis_requires_trailing_separator ||
        (is_multicharacter_basis(previous) &&
         !is_parenthesis(token) &&
         token != printed_token::digit_terminated_basis &&
         token != printed_token::nonalphanumeric_terminated_basis &&
         previous != printed_token::nonalphanumeric_terminated_basis &&
         !((previous == printed_token::compact_multicharacter_basis ||
            previous == printed_token::digit_terminated_basis) &&
           token == printed_token::symbol));
    auto const is_unseparated_multicharacter_basis =
        is_multicharacter_basis(token) &&
        token != printed_token::digit_terminated_basis &&
        token != printed_token::nonalphanumeric_terminated_basis &&
        previous != printed_token::nonalphanumeric_terminated_basis &&
        previous != printed_token::none &&
        !is_parenthesis(previous);

    if (numeric_requires_separator ||
        follows_multicharacter_basis ||
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

inline void print_numeric_token(
    std::ostream& output, std::int64_t value) {
    std::array<char, 32> text{};
    auto const result = std::to_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        output.setstate(std::ios_base::failbit);
        return;
    }
    print_token(
        output,
        std::string_view(
            text.data(), static_cast<std::size_t>(result.ptr - text.data())),
        printed_token::numeric);
}

inline void print_numeric_token(std::ostream& output, double value) {
    if (!std::isfinite(value)) {
        std::ostringstream text;
        text << '<' << value << '>';
        print_token(output, text.str());
        return;
    }

    std::array<char, 64> text{};
    auto result = std::to_chars(
        text.data(),
        text.data() + text.size() - 2,
        value,
        std::chars_format::general);
    if (result.ec != std::errc{}) {
        output.setstate(std::ios_base::failbit);
        return;
    }

    auto const has_floating_marker = std::find_if(
        text.data(), result.ptr, [](char character) {
            return character == '.' || character == 'e' || character == 'E';
        }) != result.ptr;
    if (!has_floating_marker) {
        *result.ptr++ = '.';
        *result.ptr++ = '0';
    }
    print_token(
        output,
        std::string_view(
            text.data(), static_cast<std::size_t>(result.ptr - text.data())),
        printed_token::numeric);
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
        print_token(output, name, basis_printed_token(name));
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

        if (name[0] == '?') {
            throw std::invalid_argument(
                "combdsl::basis names cannot begin with ?");
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

        if (name[length - 1] == '@') {
            throw std::invalid_argument(
                "combdsl::basis names cannot end with @");
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
class constant_partial1 : public application_expression {
public:
    template <class T>
        requires std::constructible_from<Value, T>
    constexpr explicit constant_partial1(T&& value)
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
class substitution_partial2 : public application_expression {
public:
    template <class F, class G>
        requires std::constructible_from<Function, F> &&
                 std::constructible_from<Argument, G>
    constexpr substitution_partial2(F&& function, G&& argument)
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
class substitution_partial1 : public application_expression {
public:
    template <class F>
        requires std::constructible_from<Function, F>
    constexpr explicit substitution_partial1(F&& function)
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
        return substitution_partial2<Function, argument_type>(
            function_, store_operand(std::forward<Argument>(argument)));
    }

    template <class Argument>
    [[nodiscard]] constexpr auto operator()(Argument&& argument) && {
        using argument_type = stored_operand_t<Argument>;
        return substitution_partial2<Function, argument_type>(
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
            output,
            std::string_view(name_, length_),
            detail::printed_token::symbol);
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
        return detail::constant_partial1<value_type>(
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
        return detail::substitution_partial1<function_type>(
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
    [[nodiscard]] virtual bool
    contains_live_binding() const noexcept {
        return false;
    }
    [[nodiscard]] virtual quoted_atomic_kind
    atomic_kind() const noexcept {
        return quoted_atomic_kind::none;
    }
    [[nodiscard]] virtual std::string_view
    atomic_name() const noexcept {
        return {};
    }
    [[nodiscard]] virtual std::optional<std::int64_t>
    integer_value() const noexcept {
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<double>
    floating_value() const noexcept {
        return std::nullopt;
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

    [[nodiscard]] std::optional<std::int64_t>
    integer_value() const noexcept override {
        if constexpr (std::same_as<Value, std::int64_t>) {
            return *value_;
        } else {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<double>
    floating_value() const noexcept override {
        if constexpr (std::same_as<Value, double>) {
            return *value_;
        } else {
            return std::nullopt;
        }
    }

    void print_to(std::ostream& output) const override {
        if constexpr (std::same_as<Value, std::int64_t>) {
            print_numeric_token(output, *value_);
        } else if constexpr (std::same_as<Value, double>) {
            print_numeric_token(output, *value_);
        } else {
            print_result(output, *value_);
        }
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
        : function_(std::move(function)), argument_(std::move(argument)),
          contains_live_binding_(
              quoted_access::root(function_)->contains_live_binding() ||
              quoted_access::root(argument_)->contains_live_binding()) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::application;
    }

    [[nodiscard]] bool is_application() const noexcept override { return true; }

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return contains_live_binding_;
    }

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
    bool contains_live_binding_;
};

class quoted_pending_sk_node final : public quoted_node {
public:
    explicit quoted_pending_sk_node(
        quoted_expression application)
        : application_(std::move(application)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::pending_sk;
    }

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return quoted_access::root(application_)->contains_live_binding();
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

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return quoted_access::root(generator_)->contains_live_binding();
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

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return quoted_access::root(argument_)->contains_live_binding();
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

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return quoted_access::root(argument_)->contains_live_binding();
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

template <class Value>
struct may_contain_live_quoted_expression : std::false_type {};

template <>
struct may_contain_live_quoted_expression<quoted_expression>
    : std::true_type {};

template <class Function, class Argument>
struct may_contain_live_quoted_expression<
    symbolic_application<Function, Argument>>
    : std::bool_constant<
          may_contain_live_quoted_expression<
              std::remove_cvref_t<Function>>::value ||
          may_contain_live_quoted_expression<
              std::remove_cvref_t<Argument>>::value> {};

template <class Value>
struct may_contain_live_quoted_expression<constant_partial1<Value>>
    : may_contain_live_quoted_expression<std::remove_cvref_t<Value>> {};

template <class Function>
struct may_contain_live_quoted_expression<
    substitution_partial1<Function>>
    : may_contain_live_quoted_expression<
          std::remove_cvref_t<Function>> {};

template <class Function, class Argument>
struct may_contain_live_quoted_expression<
    substitution_partial2<Function, Argument>>
    : std::bool_constant<
          may_contain_live_quoted_expression<
              std::remove_cvref_t<Function>>::value ||
          may_contain_live_quoted_expression<
              std::remove_cvref_t<Argument>>::value> {};

template <class Expression>
struct may_contain_live_quoted_expression<basis_expression<Expression>>
    : may_contain_live_quoted_expression<
          std::remove_cvref_t<Expression>> {};

template <class Expression>
struct may_contain_live_quoted_expression<
    deferred_basis_expression<Expression>>
    : may_contain_live_quoted_expression<
          std::remove_cvref_t<Expression>> {};

template <class Expression, class... Arguments>
struct may_contain_live_quoted_expression<
    basis_application_expression<Expression, Arguments...>>
    : std::bool_constant<
          may_contain_live_quoted_expression<
              std::remove_cvref_t<Expression>>::value ||
          (may_contain_live_quoted_expression<
               std::remove_cvref_t<Arguments>>::value || ...)> {};

template <class Value>
inline constexpr bool may_contain_live_quoted_expression_v =
    may_contain_live_quoted_expression<
        std::remove_cvref_t<Value>>::value;

class quoted_basis_node_base : public quoted_node {
public:
    [[nodiscard]] quoted_node_kind kind() const noexcept final {
        return quoted_node_kind::basis;
    }

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view definition_name() const noexcept {
        return name();
    }
    [[nodiscard]] virtual bool is_live_binding() const noexcept {
        return false;
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

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_.view();
    }

    [[nodiscard]] std::size_t arity() const noexcept override { return arity_; }

    [[nodiscard]] bool
    contains_live_binding() const noexcept override;

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
    mutable std::once_flag contains_live_binding_once_;
    mutable bool contains_live_binding_ = false;
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
make_quoted_native(constant_partial1<Value> const& value,
                   std::shared_ptr<void const> owner);

template <class Function>
[[nodiscard]] quoted_expression
make_quoted_native(substitution_partial1<Function> const& value,
                   std::shared_ptr<void const> owner);

template <class Function, class Argument>
[[nodiscard]] quoted_expression
make_quoted_native(substitution_partial2<Function, Argument> const& value,
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
quoted_expression make_quoted_native(constant_partial1<Value> const& value,
                                     std::shared_ptr<void const> owner) {
    return make_quoted_application(
        make_quoted_primitive(quoted_node_kind::constant),
        make_quoted_native(value.value(), std::move(owner)));
}

template <class Function>
quoted_expression
make_quoted_native(substitution_partial1<Function> const& value,
                   std::shared_ptr<void const> owner) {
    return make_quoted_application(
        make_quoted_primitive(quoted_node_kind::substitution),
        make_quoted_native(value.function(), std::move(owner)));
}

template <class Function, class Argument>
quoted_expression
make_quoted_native(substitution_partial2<Function, Argument> const& value,
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

template <class Expression>
bool quoted_basis_node<Expression>::contains_live_binding()
    const noexcept {
    if constexpr (std::same_as<Expression, quoted_expression>) {
        return quoted_access::root(*expression_)
            ->contains_live_binding();
    }
    if constexpr (!may_contain_live_quoted_expression_v<Expression>) {
        return false;
    }
    try {
        std::call_once(contains_live_binding_once_, [this] {
            auto quoted_body = body();
            contains_live_binding_ =
                quoted_access::root(quoted_body)
                    ->contains_live_binding();
        });
    } catch (...) {
        return false;
    }
    return contains_live_binding_;
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

[[nodiscard]] inline quoted_expression
takeout_with_pending_atoms(
    quoted_atomic const& qa,
    quoted_expression qe,
    std::span<quoted_atomic const> pending_atoms);

struct takeout_ministep_result {
    quoted_expression result;
    std::vector<quoted_expression> stages;
};

[[nodiscard]] inline takeout_ministep_result
takeout_with_pending_atoms_ministeps(
    quoted_atomic const& qa,
    quoted_expression qe,
    std::span<quoted_atomic const> pending_atoms);

struct reduction_options {
    bool basis_step = false;
    bool reduce_recursive_y = true;
    bool reduce_partial_k_argument = true;
    bool reduce_fixed_point = true;
};

struct reduction_trace {
    std::optional<quoted_expression> before;
    std::optional<quoted_expression> after;
};

struct redex_path_frame {
    quoted_expression function;
    quoted_expression argument;
    bool visiting_argument = false;
};

struct located_redex {
    quoted_expression expression;
    std::vector<redex_path_frame> path;
    std::size_t consumed_argument_count = 0;
};

[[nodiscard]] inline std::optional<quoted_expression>
reduce_next_redex(quoted_expression const& expression,
                  reduction_options options,
                  reduction_trace* trace = nullptr);

[[nodiscard]] inline bool
has_next_redex(quoted_expression const& expression,
               reduction_options options);

[[nodiscard]] inline bool
has_redex_at_head(quoted_expression const& expression,
                  reduction_options options);

[[nodiscard]] inline std::optional<std::size_t>
head_redex_consumed_argument_count(
    quoted_expression const& expression,
    reduction_options options);

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
        if (options.reduce_fixed_point &&
            reversed_arguments.size() >= 1) {
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
            if (options.basis_step && trace != nullptr) {
                trace->before = append_arguments(
                    make_quoted_colored_argument(
                        head, argument_color::red),
                    0);
                return finish_trace(append_arguments(
                    make_quoted_colored_argument(
                        basis.body(), argument_color::red),
                    0));
            }

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

[[nodiscard]] inline std::optional<located_redex>
locate_next_head_redex(quoted_expression const& expression,
                       reduction_options options) {
    std::vector<redex_path_frame> path;
    auto current = expression;

    for (;;) {
        if (auto const consumed_argument_count =
                head_redex_consumed_argument_count(
                    current, options)) {
            return located_redex{
                current,
                std::move(path),
                *consumed_argument_count,
            };
        }

        auto const& root = quoted_access::root(current);
        if (root->kind() == quoted_node_kind::application) {
            auto head = current;
            while (quoted_access::root(head)->kind() ==
                   quoted_node_kind::application) {
                auto const& application =
                    static_cast<quoted_application_node const&>(
                        *quoted_access::root(head));
                head = application.function();
            }
            auto const head_kind =
                quoted_access::root(head)->kind();
            auto const disabled_fixed_point =
                !options.reduce_fixed_point &&
                head_kind == quoted_node_kind::fixed_point;
            auto const disabled_recursive_y =
                !options.reduce_recursive_y &&
                head_kind == quoted_node_kind::recursive_y;
            if (!disabled_fixed_point &&
                !disabled_recursive_y &&
                (options.reduce_partial_k_argument ||
                 !is_partially_applied_k(current))) {
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

[[nodiscard]] inline std::optional<quoted_expression>
reduce_next_redex(quoted_expression const& expression,
                  reduction_options options,
                  reduction_trace* trace) {
    std::unique_lock transaction_lock(
        parser_definition_transaction_mutex(), std::defer_lock);
    if (quoted_access::root(expression)->contains_live_binding()) {
        transaction_lock.lock();
    }
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

    auto selected = locate_next_head_redex(expression, options);
    if (!selected) {
        return std::nullopt;
    }

    auto reduced = reduce_at_head(
        selected->expression, options, trace);
    for (auto frame = selected->path.rbegin();
         frame != selected->path.rend(); ++frame) {
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

[[nodiscard]] inline bool
is_saturated_basis_at_head(
    quoted_expression const& expression) noexcept {
    auto head = expression;
    std::size_t argument_count = 0;
    while (quoted_access::root(head)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(head));
        ++argument_count;
        head = application.function();
    }

    auto const& root = quoted_access::root(head);
    if (root->kind() != quoted_node_kind::basis) {
        return false;
    }
    auto const& basis =
        static_cast<quoted_basis_node_base const&>(*root);
    return argument_count >= basis.arity();
}

[[nodiscard]] inline quoted_node_kind
quoted_head_kind(quoted_expression expression) noexcept {
    while (quoted_access::root(expression)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(expression));
        expression = application.function();
    }
    return quoted_access::root(expression)->kind();
}

[[nodiscard]] inline std::string
quoted_expression_key(quoted_expression const& expression) {
    std::ostringstream output;
    std::vector<std::shared_ptr<quoted_node const>> pending;
    pending.push_back(quoted_access::root(expression));

    while (!pending.empty()) {
        auto root = std::move(pending.back());
        pending.pop_back();
        output << static_cast<int>(root->kind()) << ':';

        auto const atomic_kind = root->atomic_kind();
        if (atomic_kind != quoted_atomic_kind::none) {
            auto const name = root->atomic_name();
            output << static_cast<int>(atomic_kind) << ':'
                   << name.size() << ':' << name << ';';
            continue;
        }

        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            pending.push_back(
                quoted_access::root(application.argument()));
            pending.push_back(
                quoted_access::root(application.function()));
            break;
        }
        case quoted_node_kind::pending_sk:
            pending.push_back(quoted_access::root(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
            break;
        case quoted_node_kind::recursive_y:
            pending.push_back(quoted_access::root(
                static_cast<quoted_recursive_y_node const&>(*root)
                    .generator()));
            break;
        case quoted_node_kind::basis_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::basis: {
            auto const& basis =
                static_cast<quoted_basis_node_base const&>(*root);
            output << root.get() << ':' << basis.arity() << ';';
            break;
        }
        case quoted_node_kind::opaque:
        case quoted_node_kind::colored_argument:
            output << root.get() << ';';
            break;
        case quoted_node_kind::rec_func:
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
            break;
        }
    }

    return std::move(output).str();
}

[[nodiscard]] inline std::optional<quoted_expression>
reduce_saturated_basis_at_head(
    quoted_expression expression,
    bool& unsafe,
    std::size_t& remaining_steps) {
    if (remaining_steps == 0) {
        unsafe = true;
        return std::nullopt;
    }
    --remaining_steps;

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

    auto const& basis =
        static_cast<quoted_basis_node_base const&>(
            *quoted_access::root(head));
    expression = basis.body();
    for (std::size_t index = 0;
         index < basis.arity();
         ++index) {
        expression = expression(
            make_quoted_basis_argument(arguments[index]));
    }

    std::unordered_set<std::string> seen;
    seen.emplace(quoted_expression_key(expression));

    for (;;) {
        auto reduced = reduce_next_redex(
            expression,
            reduction_options{
                .basis_step = true,
                .reduce_recursive_y = false,
                .reduce_partial_k_argument = false,
                .reduce_fixed_point = false,
            });
        if (!reduced) {
            auto result = restore_basis_arguments(expression);
            for (std::size_t index = basis.arity();
                 index < arguments.size();
                 ++index) {
                result = result(arguments[index]);
            }
            return result;
        }

        if (remaining_steps == 0) {
            unsafe = true;
            return std::nullopt;
        }
        --remaining_steps;

        expression = std::move(*reduced);
        if (!seen.emplace(
                quoted_expression_key(expression)).second) {
            unsafe = true;
            return std::nullopt;
        }
    }
}

[[nodiscard]] inline std::optional<quoted_expression>
reduce_next_saturated_basis(
    quoted_expression const& expression,
    bool& unsafe,
    std::size_t& remaining_steps) {
    struct path_frame {
        quoted_expression function;
        quoted_expression argument;
        bool visiting_argument = false;
    };

    std::vector<path_frame> path;
    auto current = expression;

    for (;;) {
        if (is_saturated_basis_at_head(current)) {
            auto reduced =
                reduce_saturated_basis_at_head(
                    current, unsafe, remaining_steps);
            if (!reduced) {
                return std::nullopt;
            }
            for (auto frame = path.rbegin();
                 frame != path.rend();
                 ++frame) {
                *reduced = frame->visiting_argument
                    ? make_quoted_application(
                          frame->function,
                          std::move(*reduced))
                    : make_quoted_application(
                          std::move(*reduced),
                          frame->argument);
            }
            return reduced;
        }

        auto const& root = quoted_access::root(current);
        auto const head_kind = quoted_head_kind(current);
        auto const opaque_head =
            head_kind == quoted_node_kind::constant ||
            head_kind == quoted_node_kind::fixed_point ||
            head_kind == quoted_node_kind::recursive_y;
        if (root->kind() == quoted_node_kind::application &&
            !opaque_head) {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            path.push_back({
                application.function(),
                application.argument(),
            });
            current = application.function();
            continue;
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

[[nodiscard]] inline bool
has_pending_sk_redex(
    quoted_expression const& expression,
    bool reduce_partial_k_argument) {
    std::vector<quoted_expression> pending{expression};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        auto const& root = quoted_access::root(current);
        if (root->kind() == quoted_node_kind::pending_sk) {
            return true;
        }
        if (root->kind() != quoted_node_kind::application ||
            (!reduce_partial_k_argument &&
             is_partially_applied_k(current))) {
            continue;
        }

        auto const& application =
            static_cast<quoted_application_node const&>(*root);
        pending.push_back(application.argument());
        pending.push_back(application.function());
    }
    return false;
}

[[nodiscard]] inline bool
has_redex_at_head(
    quoted_expression const& expression,
    reduction_options options) {
    return head_redex_consumed_argument_count(
               expression, options).has_value();
}

[[nodiscard]] inline std::optional<std::size_t>
head_redex_consumed_argument_count(
    quoted_expression const& expression,
    reduction_options options) {
    auto head = expression;
    auto first_argument = expression;
    auto second_argument = expression;
    auto third_argument = expression;
    std::size_t argument_count = 0;
    while (quoted_access::root(head)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(head));
        third_argument = std::move(second_argument);
        second_argument = std::move(first_argument);
        first_argument = application.argument();
        ++argument_count;
        head = application.function();
    }

    switch (quoted_access::root(head)->kind()) {
    case quoted_node_kind::identity:
        if (argument_count >= 1) {
            return 1;
        }
        break;
    case quoted_node_kind::constant:
        if (argument_count >= 2) {
            return 2;
        }
        break;
    case quoted_node_kind::substitution: {
        auto const reduces_partial_sk =
            argument_count >= 2 &&
            quoted_access::root(first_argument)->kind() ==
                quoted_node_kind::constant;
        if (reduces_partial_sk) {
            return argument_count >= 3 &&
                    is_quoted_sk_application(third_argument)
                ? std::size_t{3}
                : std::size_t{2};
        }
        if (argument_count >= 3) {
            return 3;
        }
        break;
    }
    case quoted_node_kind::fixed_point:
        if (options.reduce_fixed_point && argument_count >= 1) {
            return 1;
        }
        break;
    case quoted_node_kind::recursive_y:
        if (options.reduce_recursive_y) {
            return 0;
        }
        break;
    case quoted_node_kind::basis: {
        auto const arity =
            static_cast<quoted_basis_node_base const&>(
                *quoted_access::root(head)).arity();
        if (argument_count >= arity) {
            return arity;
        }
        break;
    }
    default:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] inline bool
has_next_redex(quoted_expression const& expression,
               reduction_options options) {
    std::unique_lock transaction_lock(
        parser_definition_transaction_mutex(), std::defer_lock);
    if (quoted_access::root(expression)->contains_live_binding()) {
        transaction_lock.lock();
    }
    if (has_pending_sk_redex(
            expression, options.reduce_partial_k_argument)) {
        return true;
    }
    return locate_next_head_redex(expression, options).has_value();
}

[[nodiscard]] inline std::optional<located_redex>
locate_next_parsed_redex(quoted_expression const& expression,
                         reduction_options options = {}) {
    std::unique_lock transaction_lock(
        parser_definition_transaction_mutex(), std::defer_lock);
    if (quoted_access::root(expression)->contains_live_binding()) {
        transaction_lock.lock();
    }
    auto selected = locate_next_head_redex(expression, options);
    if (!selected) {
        return std::nullopt;
    }

    std::size_t argument_count = 0;
    auto head = selected->expression;
    while (quoted_access::root(head)->kind() ==
           quoted_node_kind::application) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(head));
        ++argument_count;
        head = application.function();
    }

    while (argument_count > selected->consumed_argument_count) {
        auto const& application =
            static_cast<quoted_application_node const&>(
                *quoted_access::root(selected->expression));
        selected->expression = application.function();
        --argument_count;
    }
    return selected;
}

[[nodiscard]] inline quoted_expression
reduce_saturated_bases(quoted_expression expression) {
    constexpr std::size_t total_step_limit = 4096;
    auto const original = expression;
    std::size_t remaining_steps = total_step_limit;
    std::unordered_set<std::string> seen;
    seen.emplace(quoted_expression_key(expression));
    for (;;) {
        bool unsafe = false;
        auto reduced = reduce_next_saturated_basis(
            expression, unsafe, remaining_steps);
        if (unsafe) {
            return original;
        }
        if (!reduced) {
            return expression;
        }
        expression = std::move(*reduced);
        if (!seen.emplace(
                quoted_expression_key(expression)).second) {
            return original;
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
using evaluation_step_limit_callback =
    std::function<bool(std::size_t)>;
using evaluation_interrupt_callback =
    std::function<bool()>;

enum class evaluation_outcome {
    completed,
    cancelled,
    step_limit_reached
};

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

[[nodiscard]] inline bool evaluation_interrupt_pending() noexcept {
    return !evaluation_not_interrupted.test(
        std::memory_order_relaxed);
}

inline constexpr std::string_view evaluation_resume_prompt_suffix =
    " Press Enter to resume; type q or Q then Enter to quit.\n";

[[nodiscard]] inline bool wait_for_evaluation_resume_input(
    std::istream& input) {
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

[[nodiscard]] inline bool wait_for_evaluation_resume(
    std::istream& input,
    std::ostream& output) {
    print_layout(output, "Interrupted.");
    print_layout(output, evaluation_resume_prompt_suffix);
    output.flush();

    return wait_for_evaluation_resume_input(input);
}

enum class evaluation_interrupt_result {
    not_interrupted,
    resumed,
    quit
};

class evaluation_progress_reporter {
public:
    explicit evaluation_progress_reporter(
        evaluation_progress_callback const& callback,
        std::optional<std::size_t> step_limit = std::nullopt)
        : callback_(callback), step_limit_(step_limit) {}

    void completed_reduction() {
        ++reductions_;
        ++reductions_since_step_limit_resume_;
        if (!callback_) {
            return;
        }

        callback_(reductions_);
    }

    [[nodiscard]] bool step_limit_reached() const noexcept {
        return step_limit_ &&
               reductions_since_step_limit_resume_ >= *step_limit_;
    }

    [[nodiscard]] std::size_t reductions() const noexcept {
        return reductions_;
    }

    [[nodiscard]] std::size_t
    reductions_since_step_limit_resume() const noexcept {
        return reductions_since_step_limit_resume_;
    }

    void reset_step_limit_count() noexcept {
        reductions_since_step_limit_resume_ = 0;
    }

private:
    evaluation_progress_callback const& callback_;
    std::optional<std::size_t> step_limit_;
    std::size_t reductions_ = 0;
    std::size_t reductions_since_step_limit_resume_ = 0;
};

} // namespace detail

inline evaluation_outcome eval_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback,
    evaluation_interrupt_callback const& interrupt_callback) {
    detail::scoped_evaluation_sigint_handler sigint_handler;
    detail::evaluation_progress_reporter progress(
        progress_callback, step_limit);
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
        auto const resume = interrupt_callback
            ? interrupt_callback()
            : detail::wait_for_evaluation_resume(input, output);
        return resume
                   ? detail::evaluation_interrupt_result::resumed
                   : detail::evaluation_interrupt_result::quit;
    };

    bool expression_was_printed = false;
    bool allow_one_reduction_after_zero_limit = false;
    for (;;) {
        auto const before_step = wait_after_interrupt(expression);
        if (before_step == detail::evaluation_interrupt_result::quit) {
            return evaluation_outcome::cancelled;
        }
        if (before_step == detail::evaluation_interrupt_result::resumed) {
            expression_was_printed = true;
        }

        detail::reduction_options const options{
            .basis_step = basis_step,
            .reduce_partial_k_argument = false,
        };
        if (progress.step_limit_reached() &&
            !allow_one_reduction_after_zero_limit) {
            auto const reducible =
                detail::has_next_redex(expression, options);
            if (!expression_was_printed) {
                print_expression(expression);
            }
            if (!reducible) {
                return evaluation_outcome::completed;
            }
            if (!step_limit_callback) {
                return evaluation_outcome::step_limit_reached;
            }
            if (!step_limit_callback(
                    progress.reductions_since_step_limit_resume())) {
                return evaluation_outcome::cancelled;
            }
            progress.reset_step_limit_count();
            allow_one_reduction_after_zero_limit =
                step_limit && *step_limit == 0;
            expression_was_printed = true;
            continue;
        }
        auto reduced = detail::reduce_next_redex(expression, options);
        auto next = reduced ? std::move(*reduced) : expression;
        auto const no_reduction = !reduced;
        if (reduced) {
            progress.completed_reduction();
            allow_one_reduction_after_zero_limit = false;
        }
        auto const after_step = wait_after_interrupt(next);
        if (after_step == detail::evaluation_interrupt_result::quit) {
            return evaluation_outcome::cancelled;
        }
        auto const next_was_printed =
            after_step == detail::evaluation_interrupt_result::resumed;

        if (no_reduction) {
            if (!expression_was_printed && !next_was_printed) {
                print_expression(expression);
            }
            return evaluation_outcome::completed;
        }

        expression = std::move(next);
        expression_was_printed = next_was_printed;
    }
}

inline evaluation_outcome eval_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback) {
    return eval_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        step_limit_callback,
        evaluation_interrupt_callback{});
}

inline evaluation_outcome eval_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit) {
    return eval_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        evaluation_step_limit_callback{});
}

inline evaluation_outcome eval_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    return eval_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        std::nullopt);
}

inline void eval(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit) {
    static_cast<void>(eval_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit));
}

inline void eval(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    eval(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        std::nullopt);
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

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback,
    evaluation_interrupt_callback const& interrupt_callback);

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback);

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit);

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback);

namespace detail {

[[nodiscard]] inline bool wait_after_single_step_run_interrupt(
    std::istream& input,
    std::ostream& output,
    evaluation_interrupt_callback const& interrupt_callback) {
    if (!consume_evaluation_interrupt()) {
        return true;
    }
    return interrupt_callback
        ? interrupt_callback()
        : wait_for_evaluation_resume(input, output);
}

} // namespace detail

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback,
    evaluation_interrupt_callback const& interrupt_callback) {
    detail::scoped_evaluation_sigint_handler sigint_handler;
    detail::evaluation_progress_reporter progress(
        progress_callback, step_limit);

    output.flush();
    bool expression_was_printed = false;
    bool allow_one_reduction_after_zero_limit = false;

    for (;;) {
        if (!detail::wait_after_single_step_run_interrupt(
                input, output, interrupt_callback)) {
            return evaluation_outcome::cancelled;
        }

        if (progress.step_limit_reached() &&
            !allow_one_reduction_after_zero_limit) {
            auto const reducible = detail::has_next_redex(
                expression,
                detail::reduction_options{
                    .basis_step = basis_step,
                });
            if (reducible && !expression_was_printed) {
                expression.print_to(output);
                detail::print_layout(output, "\n");
                output.flush();
            }
            if (!reducible) {
                return evaluation_outcome::completed;
            }
            if (!step_limit_callback) {
                return evaluation_outcome::step_limit_reached;
            }
            if (!step_limit_callback(
                    progress.reductions_since_step_limit_resume())) {
                return evaluation_outcome::cancelled;
            }
            progress.reset_step_limit_count();
            allow_one_reduction_after_zero_limit =
                step_limit && *step_limit == 0;
            expression_was_printed = true;
            continue;
        }

        auto next = single_step(expression, basis_step);
        auto const no_reduction =
            detail::quoted_access::root(next) ==
            detail::quoted_access::root(expression);
        if (!no_reduction) {
            progress.completed_reduction();
            allow_one_reduction_after_zero_limit = false;
        }
        if (!detail::wait_after_single_step_run_interrupt(
                input, output, interrupt_callback)) {
            return evaluation_outcome::cancelled;
        }

        if (no_reduction) {
            return evaluation_outcome::completed;
        }

        expression = std::move(next);
        expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        expression_was_printed = true;
    }
}

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback) {
    return single_step_run_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        step_limit_callback,
        evaluation_interrupt_callback{});
}

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit) {
    return single_step_run_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        evaluation_step_limit_callback{});
}

inline evaluation_outcome single_step_run_with_outcome(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    return single_step_run_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback,
        std::nullopt);
}

inline void single_step_run(
    quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    static_cast<void>(single_step_run_with_outcome(
        std::move(expression),
        output,
        input,
        basis_step,
        progress_callback));
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

enum class parser_reference_mode {
    captured,
    live
};

class registered_parser_basis {
public:
    registered_parser_basis(
        std::string name,
        bool predefined,
        std::size_t version = 0,
        std::optional<parser_reference_mode> reference_mode =
            std::nullopt)
        : name_(std::move(name)), predefined_(predefined),
          version_(version), reference_mode_(reference_mode) {}

    virtual ~registered_parser_basis() = default;

    [[nodiscard]] std::string const& name() const noexcept {
        return name_;
    }

    [[nodiscard]] bool predefined() const noexcept {
        return predefined_;
    }

    [[nodiscard]] std::size_t version() const noexcept {
        return version_;
    }

    [[nodiscard]] std::optional<parser_reference_mode>
    reference_mode() const noexcept {
        return reference_mode_;
    }

    [[nodiscard]] virtual quoted_expression expression() const = 0;

private:
    std::string name_;
    bool predefined_;
    std::size_t version_;
    std::optional<parser_reference_mode> reference_mode_;
};

template <class Basis>
class registered_parser_basis_model final : public registered_parser_basis {
public:
    registered_parser_basis_model(
        std::string name,
        Basis const& basis,
        bool predefined,
        std::size_t version = 0,
        std::optional<parser_reference_mode> reference_mode =
            std::nullopt)
        : registered_parser_basis(
              std::move(name), predefined, version,
              reference_mode),
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

struct stored_parser_definition {
    std::string source;
    std::string name;
    registered_parser_basis_ptr basis;
    std::vector<registered_parser_basis_ptr> dependencies;
    bool was_referred_to = false;
    bool references_command = false;

    [[nodiscard]] bool is_removal() const noexcept {
        return basis == nullptr && !references_command;
    }
};

using parser_definition_history =
    std::vector<stored_parser_definition>;

using parser_basis_version_history = std::unordered_map<
    std::string,
    std::vector<registered_parser_basis_ptr>,
    parser_basis_name_hash,
    std::equal_to<>>;

[[nodiscard]] inline parser_basis_version_history&
parser_basis_version_registry() {
    static parser_basis_version_history versions;
    return versions;
}

[[nodiscard]] inline bool& parser_snapshot_enabled() {
    static bool enabled = true;
    return enabled;
}

struct parser_live_binding {
    registered_parser_basis_ptr target;
};

using parser_live_binding_ptr =
    std::shared_ptr<parser_live_binding>;
using parser_live_binding_table = std::unordered_map<
    std::string,
    parser_live_binding_ptr,
    parser_basis_name_hash,
    std::equal_to<>>;

[[nodiscard]] inline parser_live_binding_table&
parser_live_binding_registry() {
    static parser_live_binding_table bindings;
    return bindings;
}

class quoted_parser_basis_reference_node final
    : public quoted_basis_node_base {
public:
    quoted_parser_basis_reference_node(
        registered_parser_basis_ptr target,
        parser_live_binding_ptr live_binding)
        : definition_name_(target->name()),
          printed_name_(live_binding
              ? target->name()
              : target->name() + "@" +
                    std::to_string(target->version())),
          target_(std::move(target)),
          live_binding_(std::move(live_binding)),
          contains_live_binding_(
              live_binding_ != nullptr ||
              registered_basis_contains_live_binding(target_)) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return printed_name_;
    }

    [[nodiscard]] std::string_view
    definition_name() const noexcept override {
        return definition_name_;
    }

    [[nodiscard]] bool is_live_binding() const noexcept override {
        return live_binding_ != nullptr;
    }

    [[nodiscard]] bool
    contains_live_binding() const noexcept override {
        return contains_live_binding_;
    }

    [[nodiscard]] std::size_t arity() const noexcept override {
        try {
            return registered_basis_arity(resolved_target());
        } catch (...) {
            return registered_basis_arity(target_);
        }
    }

    [[nodiscard]] quoted_expression body() const override {
        return registered_basis_body(resolved_target());
    }

    void print_to(std::ostream& output) const override {
        auto const token = ends_with_non_alphanumeric(definition_name_)
            ? printed_token::nonalphanumeric_terminated_basis
            : ends_with_ascii_digit(definition_name_)
                ? printed_token::digit_terminated_basis
                : printed_name_ != definition_name_
                    ? printed_token::compact_multicharacter_basis
                    : basis_printed_token(printed_name_);
        print_token(
            output,
            printed_name_,
            token);
    }

    void print_as_operand_to(std::ostream& output) const override {
        print_to(output);
    }

    [[nodiscard]] registered_parser_basis_ptr const&
    frozen_target() const noexcept {
        return target_;
    }

    [[nodiscard]] registered_parser_basis_ptr resolve_from(
        registered_parser_basis_table const& registered_bases) const {
        if (live_binding_) {
            if (auto const match =
                    registered_bases.find(definition_name_);
                match != registered_bases.end()) {
                return match->second;
            }
            if (auto target = std::atomic_load(
                    &live_binding_->target)) {
                return target;
            }
        }
        return target_;
    }

private:
    [[nodiscard]] static bool registered_basis_contains_live_binding(
        registered_parser_basis_ptr const& registration) {
        auto expression = registration->expression();
        return quoted_access::root(expression)->contains_live_binding();
    }

    [[nodiscard]] static std::size_t registered_basis_arity(
        registered_parser_basis_ptr const& registration) {
        auto expression = registration->expression();
        auto root = quoted_access::root(expression);
        if (root->kind() != quoted_node_kind::basis) {
            throw std::logic_error(
                "combdsl::registered parser basis is not a basis");
        }
        return static_cast<quoted_basis_node_base const&>(*root).arity();
    }

    [[nodiscard]] static quoted_expression registered_basis_body(
        registered_parser_basis_ptr const& registration) {
        auto expression = registration->expression();
        auto root = quoted_access::root(expression);
        if (root->kind() != quoted_node_kind::basis) {
            throw std::logic_error(
                "combdsl::registered parser basis is not a basis");
        }
        return static_cast<quoted_basis_node_base const&>(*root).body();
    }

    [[nodiscard]] registered_parser_basis_ptr
    resolved_target() const {
        if (!live_binding_) {
            return target_;
        }

        std::lock_guard transaction_lock(
            parser_definition_transaction_mutex());
        registered_parser_basis_ptr resolved;
        {
            std::lock_guard lock(parser_basis_registry_mutex());
            auto const& registered_bases = parser_basis_registry();
            if (auto const match =
                    registered_bases.find(definition_name_);
                match != registered_bases.end()) {
                resolved = match->second;
            }
        }
        if (!resolved) {
            resolved = std::atomic_load(&live_binding_->target);
            if (!resolved) {
                resolved = target_;
            }
        }
        return resolved;
    }

    std::string definition_name_;
    std::string printed_name_;
    registered_parser_basis_ptr target_;
    parser_live_binding_ptr live_binding_;
    bool contains_live_binding_;
};

[[nodiscard]] inline quoted_expression
make_parser_basis_reference(
    registered_parser_basis_ptr target,
    parser_live_binding_ptr live_binding = nullptr) {
    return quoted_access::make(
        std::make_shared<quoted_parser_basis_reference_node>(
            std::move(target), std::move(live_binding)));
}

[[nodiscard]] inline parser_definition_history&
parser_definition_registry() {
    static parser_definition_history definitions;
    return definitions;
}

[[nodiscard]] inline bool is_primitive_name(
    std::string_view name) noexcept {
    return name == "S" || name == "K" || name == "I" || name == "Y";
}

[[nodiscard]] inline bool has_reserved_version_suffix(
    std::string_view name) noexcept {
    auto const separator = name.rfind('@');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == name.size()) {
        return false;
    }
    return std::ranges::all_of(
        name.substr(separator + 1),
        [](char character) {
            return character >= '0' && character <= '9';
        });
}

enum class parser_definition_change {
    inserted,
    unchanged,
    replaced,
    rejected_predefined,
    rejected_circular
};

struct parser_definition_inspection {
    parser_definition_change change;
    std::string replaced_definition;
    std::vector<std::string> circular_path;
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

        auto const left_integer = left_root->integer_value();
        auto const right_integer = right_root->integer_value();
        if (left_integer || right_integer) {
            if (left_integer != right_integer) {
                return false;
            }
            continue;
        }

        auto const left_floating = left_root->floating_value();
        auto const right_floating = right_root->floating_value();
        if (left_floating || right_floating) {
            if (left_floating != right_floating) {
                return false;
            }
            continue;
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

            auto const* left_reference = dynamic_cast<
                quoted_parser_basis_reference_node const*>(
                    left_root.get());
            auto const* right_reference = dynamic_cast<
                quoted_parser_basis_reference_node const*>(
                    right_root.get());
            if (left_reference != nullptr ||
                right_reference != nullptr) {
                if (left_reference == nullptr ||
                    right_reference == nullptr ||
                    left_reference->is_live_binding() !=
                        right_reference->is_live_binding() ||
                    left_reference->definition_name() !=
                        right_reference->definition_name()) {
                    return false;
                }
                if (!left_reference->is_live_binding() &&
                    left_reference->frozen_target() !=
                        right_reference->frozen_target()) {
                    return false;
                }
                break;
            }
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

[[nodiscard]] inline std::vector<registered_parser_basis_ptr>
referenced_user_parser_bases(
    quoted_expression const& expression,
    registered_parser_basis_table const& registered_bases) {
    std::unordered_map<
        quoted_node const*, registered_parser_basis_ptr>
        user_basis_roots;
    for (auto const& [name, registered] : registered_bases) {
        static_cast<void>(name);
        if (!registered->predefined()) {
            user_basis_roots.emplace(
                quoted_access::root(registered->expression()).get(),
                registered);
        }
    }

    std::vector<registered_parser_basis_ptr> result;
    std::unordered_set<registered_parser_basis const*> added;
    std::unordered_set<quoted_node const*> visited;
    std::vector<std::shared_ptr<quoted_node const>> pending;
    pending.push_back(quoted_access::root(expression));

    while (!pending.empty()) {
        auto root = std::move(pending.back());
        pending.pop_back();
        if (!visited.emplace(root.get()).second) {
            continue;
        }

        if (auto const* reference = dynamic_cast<
                quoted_parser_basis_reference_node const*>(root.get())) {
            auto dependency =
                reference->resolve_from(registered_bases);
            if (!dependency->predefined() &&
                added.emplace(dependency.get()).second) {
                result.push_back(std::move(dependency));
            }
            continue;
        }

        if (auto const match = user_basis_roots.find(root.get());
            match != user_basis_roots.end()) {
            if (added.emplace(match->second.get()).second) {
                result.push_back(match->second);
            }
            continue;
        }

        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            pending.push_back(
                quoted_access::root(application.argument()));
            pending.push_back(
                quoted_access::root(application.function()));
            break;
        }
        case quoted_node_kind::pending_sk:
            pending.push_back(quoted_access::root(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
            break;
        case quoted_node_kind::recursive_y:
            pending.push_back(quoted_access::root(
                static_cast<quoted_recursive_y_node const&>(*root)
                    .generator()));
            break;
        case quoted_node_kind::basis_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::colored_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_colored_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::opaque:
        case quoted_node_kind::rec_func:
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
        case quoted_node_kind::basis:
            break;
        }
    }
    return result;
}

[[nodiscard]] inline std::vector<std::size_t>
referred_parser_definition_indices(
    parser_definition_history const& definitions,
    std::string_view referring_name,
    std::vector<registered_parser_basis_ptr> const& dependencies) {
    std::unordered_map<registered_parser_basis const*, std::size_t>
        definition_indices;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        if (definitions[index].basis != nullptr) {
            definition_indices.emplace(
                definitions[index].basis.get(), index);
        }
    }

    std::vector<registered_parser_basis_ptr> pending;
    for (auto const& dependency : dependencies) {
        if (dependency != nullptr &&
            dependency->name() != referring_name) {
            pending.push_back(dependency);
        }
    }

    std::unordered_set<registered_parser_basis const*> visited;
    std::vector<std::size_t> result;
    while (!pending.empty()) {
        auto dependency = std::move(pending.back());
        pending.pop_back();
        if (!visited.emplace(dependency.get()).second) {
            continue;
        }

        auto const match = definition_indices.find(dependency.get());
        if (match == definition_indices.end()) {
            continue;
        }
        auto const index = match->second;
        result.push_back(index);
        for (auto const& nested : definitions[index].dependencies) {
            if (nested != nullptr) {
                pending.push_back(nested);
            }
        }
    }
    return result;
}

[[nodiscard]] inline std::vector<std::string>
direct_named_parser_bases_in_expression(
    quoted_expression const& expression,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    std::vector<std::string> result;
    std::unordered_set<std::string> added;
    std::unordered_set<quoted_node const*> visited;
    std::vector<std::shared_ptr<quoted_node const>> pending;
    pending.push_back(quoted_access::root(expression));

    while (!pending.empty()) {
        auto root = std::move(pending.back());
        pending.pop_back();
        if (!visited.emplace(root.get()).second) {
            continue;
        }

        if (root->kind() == quoted_node_kind::basis) {
            auto const nested_name =
                static_cast<quoted_basis_node_base const&>(
                    *root).definition_name();
            if ((registered_bases.contains(nested_name) ||
                 registered_versions.contains(nested_name)) &&
                added.emplace(nested_name).second) {
                result.emplace_back(nested_name);
            }
            continue;
        }

        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            pending.push_back(quoted_access::root(
                application.argument()));
            pending.push_back(quoted_access::root(
                application.function()));
            break;
        }
        case quoted_node_kind::pending_sk:
            pending.push_back(quoted_access::root(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
            break;
        case quoted_node_kind::recursive_y:
            pending.push_back(quoted_access::root(
                static_cast<quoted_recursive_y_node const&>(*root)
                    .generator()));
            break;
        case quoted_node_kind::basis_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::colored_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_colored_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::opaque:
        case quoted_node_kind::rec_func:
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
        case quoted_node_kind::basis:
            break;
        }
    }

    std::ranges::sort(result);
    return result;
}

[[nodiscard]] inline std::vector<std::string>
direct_named_parser_bases_in_definition(
    quoted_expression const& expression,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::basis) {
        throw std::logic_error(
            "combdsl::registered parser basis is not a basis");
    }
    auto const& basis =
        static_cast<quoted_basis_node_base const&>(*root);
    return direct_named_parser_bases_in_expression(
        basis.body(), registered_bases, registered_versions);
}

[[nodiscard]] inline std::vector<registered_parser_basis_ptr>
direct_registered_parser_bases_in_definition(
    quoted_expression const& expression,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    auto const& root = quoted_access::root(expression);
    if (root->kind() != quoted_node_kind::basis) {
        throw std::logic_error(
            "combdsl::registered parser basis is not a basis");
    }
    auto const& basis =
        static_cast<quoted_basis_node_base const&>(*root);
    auto result = referenced_user_parser_bases(
        basis.body(), registered_bases);
    auto const names = direct_named_parser_bases_in_expression(
        basis.body(), registered_bases, registered_versions);
    std::unordered_set<std::string> represented_names;
    for (auto const& dependency : result) {
        represented_names.emplace(dependency->name());
    }
    for (auto const& name : names) {
        if (represented_names.contains(name)) {
            continue;
        }
        if (auto const current = registered_bases.find(name);
            current != registered_bases.end()) {
            result.push_back(current->second);
            represented_names.emplace(name);
        } else if (auto const versions =
                       registered_versions.find(name);
                   versions != registered_versions.end() &&
                   !versions->second.empty()) {
            result.push_back(versions->second.back());
            represented_names.emplace(name);
        }
    }
    return result;
}

[[nodiscard]] inline std::vector<std::string>
parser_basis_definition_circular_path(
    std::string_view name,
    quoted_expression const& proposed_definition,
    registered_parser_basis_table const& registered_bases) {
    auto const& proposed_root = quoted_access::root(
        proposed_definition);
    if (proposed_root->kind() != quoted_node_kind::basis) {
        throw std::logic_error(
            "combdsl::registered parser basis is not a basis");
    }

    enum class visit_state : unsigned char { gray, black };
    std::unordered_map<quoted_node const*, visit_state> states;
    std::unordered_map<quoted_node const*, std::size_t> stack_positions;
    std::vector<std::string> stack_names;
    std::vector<std::string> circular_path;

    std::function<bool(
        std::shared_ptr<quoted_node const> const&,
        std::string_view)> visit_basis;
    std::function<bool(std::shared_ptr<quoted_node const> const&)>
        visit_expression;

    visit_basis = [&](std::shared_ptr<quoted_node const> const& root,
                      std::string_view edge_name) {
        auto target_root = root;
        if (auto const* reference = dynamic_cast<
                quoted_parser_basis_reference_node const*>(root.get())) {
            registered_parser_basis_ptr target;
            if (reference->is_live_binding() &&
                reference->definition_name() == name) {
                target_root = proposed_root;
            } else {
                target = reference->resolve_from(registered_bases);
                target_root = quoted_access::root(target->expression());
            }
        }

        if (auto const state = states.find(target_root.get());
            state != states.end()) {
            if (state->second == visit_state::black) {
                return false;
            }
            auto const start = stack_positions.at(target_root.get());
            circular_path.assign(
                stack_names.begin() +
                    static_cast<std::ptrdiff_t>(start),
                stack_names.end());
            circular_path.emplace_back(edge_name);
            return true;
        }

        if (target_root->kind() != quoted_node_kind::basis) {
            throw std::logic_error(
                "combdsl::registered parser basis is not a basis");
        }
        states.emplace(target_root.get(), visit_state::gray);
        stack_positions.emplace(target_root.get(), stack_names.size());
        stack_names.emplace_back(edge_name);

        auto const& target_basis =
            static_cast<quoted_basis_node_base const&>(*target_root);
        if (visit_expression(quoted_access::root(target_basis.body()))) {
            return true;
        }

        stack_names.pop_back();
        stack_positions.erase(target_root.get());
        states[target_root.get()] = visit_state::black;
        return false;
    };

    visit_expression = [&](std::shared_ptr<quoted_node const> const& root) {
        if (root->kind() == quoted_node_kind::basis) {
            auto const& basis =
                static_cast<quoted_basis_node_base const&>(*root);
            return visit_basis(root, basis.name());
        }

        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            return visit_expression(
                       quoted_access::root(application.function())) ||
                   visit_expression(
                       quoted_access::root(application.argument()));
        }
        case quoted_node_kind::pending_sk:
            return visit_expression(quoted_access::root(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
        case quoted_node_kind::recursive_y:
            return visit_expression(quoted_access::root(
                static_cast<quoted_recursive_y_node const&>(*root)
                    .generator()));
        case quoted_node_kind::basis_argument:
            return visit_expression(quoted_access::root(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
        case quoted_node_kind::colored_argument:
            return visit_expression(quoted_access::root(
                static_cast<quoted_colored_argument_node const&>(*root)
                    .argument()));
        case quoted_node_kind::opaque:
        case quoted_node_kind::rec_func:
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
        case quoted_node_kind::basis:
            return false;
        }
        return false;
    };

    static_cast<void>(visit_basis(proposed_root, name));
    return circular_path;
}

template <class Basis>
void register_parser_basis(std::string_view name, Basis const& basis) {
    if (is_primitive_name(name)) {
        return;
    }
    if (has_reserved_version_suffix(name)) {
        auto message = std::string(
            "combdsl::basis name cannot end in a version suffix: ");
        message += name;
        throw std::invalid_argument(message);
    }

    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    auto const existing = entries.find(name);
    if (existing == entries.end()) {
        auto& versions = parser_basis_version_registry()[
            std::string(name)];
        if (!versions.empty()) {
            auto message = std::string(
                "combdsl::basis name is already user-defined: ");
            message += name;
            throw std::invalid_argument(message);
        }
        auto registration =
            std::make_shared<registered_parser_basis_model<Basis>>(
                std::string(name), basis, true,
                versions.size() + 1);
        entries.emplace(std::string(name), registration);
        versions.push_back(std::move(registration));
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
    quoted_expression const& basis,
    registered_parser_basis_table const& registered_bases,
    bool reject_circular) {
    if (is_primitive_name(name)) {
        return {
            parser_definition_change::rejected_predefined, {}, {}};
    }

    auto const match = registered_bases.find(name);
    if (match == registered_bases.end()) {
        auto circular_path = reject_circular
            ? parser_basis_definition_circular_path(
                  name, basis, registered_bases)
            : std::vector<std::string>{};
        if (!circular_path.empty()) {
            return {
                parser_definition_change::rejected_circular,
                {},
                std::move(circular_path)};
        }
        return {parser_definition_change::inserted, {}, {}};
    }
    auto const& existing = match->second;

    if (existing->predefined()) {
        return {
            parser_definition_change::rejected_predefined, {}, {}};
    }
    if (same_parser_basis_definition(
            existing->expression(), basis)) {
        return {parser_definition_change::unchanged, {}, {}};
    }
    auto circular_path = reject_circular
        ? parser_basis_definition_circular_path(
              name, basis, registered_bases)
        : std::vector<std::string>{};
    if (!circular_path.empty()) {
        return {
            parser_definition_change::rejected_circular,
            {},
            std::move(circular_path)};
    }
    return {
        parser_definition_change::replaced,
        format_parser_basis_definition(existing->expression()),
        {}};
}

[[nodiscard]] inline parser_definition_change
register_parser_definition_basis(
    std::string_view name,
    quoted_expression const& basis,
    std::string user_source,
    std::vector<registered_parser_basis_ptr> dependencies,
    parser_reference_mode reference_mode,
    bool reject_circular,
    std::vector<std::string>& circular_path) {
    if (is_primitive_name(name)) {
        return parser_definition_change::rejected_predefined;
    }

    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    auto& definitions = parser_definition_registry();
    auto const existing = entries.find(name);
    if (existing == entries.end() && reject_circular) {
        circular_path =
            parser_basis_definition_circular_path(
                name, basis, entries);
        if (!circular_path.empty()) {
            return parser_definition_change::rejected_circular;
        }
    }
    if (existing != entries.end()) {
        if (existing->second->predefined()) {
            return parser_definition_change::rejected_predefined;
        }
        if (same_parser_basis_definition(
                existing->second->expression(), basis)) {
            return parser_definition_change::unchanged;
        }
        if (reject_circular) {
            circular_path =
                parser_basis_definition_circular_path(
                    name, basis, entries);
            if (!circular_path.empty()) {
                return parser_definition_change::rejected_circular;
            }
        }

    }

    auto& versions = parser_basis_version_registry()[
        std::string(name)];
    auto registration =
        std::make_shared<
            registered_parser_basis_model<quoted_expression>>(
            std::string(name), basis, false,
            versions.size() + 1, reference_mode);
    definitions.push_back({
        std::move(user_source), std::string(name), registration,
        std::move(dependencies), false});
    versions.push_back(registration);
    auto& live_binding = parser_live_binding_registry()[
        std::string(name)];
    if (!live_binding) {
        live_binding = std::make_shared<parser_live_binding>();
    }
    registered_parser_basis_ptr binding_target = registration;
    std::atomic_store(
        &live_binding->target, std::move(binding_target));

    if (existing == entries.end()) {
        entries.emplace(std::string(name), std::move(registration));
        return parser_definition_change::inserted;
    }
    existing->second = std::move(registration);
    return parser_definition_change::replaced;
}

enum class parser_removal_change {
    removed,
    not_found,
    rejected_predefined
};

[[nodiscard]] inline parser_removal_change
remove_parser_definition_basis(
    std::string_view name,
    std::string user_source) {
    if (is_primitive_name(name)) {
        return parser_removal_change::rejected_predefined;
    }

    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    auto& entries = parser_basis_registry();
    auto const existing = entries.find(name);
    if (existing == entries.end()) {
        return parser_removal_change::not_found;
    }
    if (existing->second->predefined()) {
        return parser_removal_change::rejected_predefined;
    }

    auto& definitions = parser_definition_registry();
    definitions.push_back({
        std::move(user_source), std::string(name), nullptr, {},
        false});
    entries.erase(existing);
    return parser_removal_change::removed;
}

[[nodiscard]] inline registered_parser_basis_table
registered_parser_bases_snapshot() {
    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    return parser_basis_registry();
}

struct parser_lookup_snapshot {
    registered_parser_basis_table bases;
    parser_basis_version_history versions;
    parser_live_binding_table live_bindings;
    bool snapshot_enabled;
};

[[nodiscard]] inline parser_lookup_snapshot
registered_parser_lookup_snapshot() {
    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    return {
        parser_basis_registry(),
        parser_basis_version_registry(),
        parser_live_binding_registry(),
        parser_snapshot_enabled()};
}

inline void register_parser_reference_mode(
    bool enabled,
    std::string user_source) {
    std::lock_guard transaction_lock(
        parser_definition_transaction_mutex());
    std::lock_guard lock(parser_basis_registry_mutex());
    auto& definitions = parser_definition_registry();
    if (std::ranges::all_of(
            definitions,
            [](stored_parser_definition const& definition) {
                return definition.references_command;
            })) {
        definitions.clear();
    }
    definitions.push_back({
        std::move(user_source), {}, nullptr, {}, false, true});
    parser_snapshot_enabled() = enabled;
}

enum class parser_dependency_direction {
    depended_on_by,
    uses
};

struct parser_dependency_name_lists {
    std::vector<std::string> direct;
    std::vector<std::string> indirect;
};

[[nodiscard]] inline std::vector<std::string>
direct_parser_dependency_names(
    std::string_view name,
    parser_dependency_direction direction,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    auto const target = registered_bases.find(name);
    if (target == registered_bases.end()) {
        return {};
    }

    if (direction == parser_dependency_direction::uses) {
        return direct_named_parser_bases_in_definition(
            target->second->expression(), registered_bases,
            registered_versions);
    }

    std::vector<std::string> result;
    for (auto const& [candidate_name, candidate] :
         registered_bases) {
        if (candidate_name == name) {
            continue;
        }
        auto const dependencies =
            direct_named_parser_bases_in_definition(
                candidate->expression(), registered_bases,
                registered_versions);
        if (std::ranges::find(dependencies, name) !=
            dependencies.end()) {
            result.push_back(candidate_name);
        }
    }
    std::ranges::sort(result);
    return result;
}

[[nodiscard]] inline parser_dependency_name_lists
parser_dependency_names(
    std::string_view name,
    parser_dependency_direction direction,
    bool include_indirect,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    parser_dependency_name_lists result;
    result.direct = direct_parser_dependency_names(
        name, direction, registered_bases, registered_versions);
    if (!include_indirect) {
        return result;
    }

    if (direction == parser_dependency_direction::depended_on_by) {
        struct dependency_graph_entry {
            registered_parser_basis_ptr basis;
            std::vector<registered_parser_basis_ptr> dependencies;
        };
        std::vector<registered_parser_basis_ptr> pending;
        pending.reserve(registered_bases.size());
        for (auto const& [candidate_name, candidate] :
             registered_bases) {
            static_cast<void>(candidate_name);
            pending.push_back(candidate);
        }
        for (auto const& [version_name, versions] :
             registered_versions) {
            static_cast<void>(version_name);
            pending.insert(
                pending.end(), versions.begin(), versions.end());
        }

        std::unordered_set<registered_parser_basis const*> visited;
        std::vector<dependency_graph_entry> graph;
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            if (!visited.emplace(current.get()).second) {
                continue;
            }
            auto dependencies =
                direct_registered_parser_bases_in_definition(
                    current->expression(), registered_bases,
                    registered_versions);
            pending.insert(
                pending.end(), dependencies.begin(), dependencies.end());
            graph.push_back({
                std::move(current), std::move(dependencies)});
        }

        std::unordered_map<
            registered_parser_basis const*,
            std::vector<registered_parser_basis const*>> reverse_edges;
        std::vector<registered_parser_basis const*> reachable;
        std::unordered_set<registered_parser_basis const*>
            reaches_query;
        for (auto const& entry : graph) {
            if (entry.basis->name() == name &&
                reaches_query.emplace(entry.basis.get()).second) {
                reachable.push_back(entry.basis.get());
            }
            for (auto const& dependency : entry.dependencies) {
                reverse_edges[dependency.get()].push_back(
                    entry.basis.get());
            }
        }
        while (!reachable.empty()) {
            auto const current = reachable.back();
            reachable.pop_back();
            auto const users = reverse_edges.find(current);
            if (users == reverse_edges.end()) {
                continue;
            }
            for (auto const user : users->second) {
                if (reaches_query.emplace(user).second) {
                    reachable.push_back(user);
                }
            }
        }

        std::unordered_set<std::string> direct_names(
            result.direct.begin(), result.direct.end());
        for (auto const& [candidate_name, candidate] :
             registered_bases) {
            if (candidate_name != name &&
                !direct_names.contains(candidate_name) &&
                reaches_query.contains(candidate.get())) {
                result.indirect.push_back(candidate_name);
            }
        }
        std::ranges::sort(result.indirect);
        return result;
    }

    std::unordered_set<std::string> direct_names(
        result.direct.begin(), result.direct.end());
    std::unordered_set<registered_parser_basis const*> visited;
    auto const target = registered_bases.find(name);
    if (target == registered_bases.end()) {
        return result;
    }
    visited.emplace(target->second.get());
    std::vector<registered_parser_basis_ptr> pending;
    for (auto& dependency :
         direct_registered_parser_bases_in_definition(
             target->second->expression(), registered_bases,
             registered_versions)) {
        if (visited.emplace(dependency.get()).second) {
            pending.push_back(std::move(dependency));
        }
    }
    std::unordered_set<std::string> indirect_names;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        auto nested = direct_registered_parser_bases_in_definition(
            current->expression(), registered_bases,
            registered_versions);
        for (auto& dependency : nested) {
            if (!visited.emplace(dependency.get()).second) {
                continue;
            }
            if (dependency->name() != name &&
                !direct_names.contains(dependency->name())) {
                indirect_names.emplace(dependency->name());
            }
            pending.push_back(std::move(dependency));
        }
    }
    result.indirect.assign(
        indirect_names.begin(), indirect_names.end());
    std::ranges::sort(result.indirect);
    return result;
}

enum class parser_dependency_edge_kind {
    captured,
    live,
    predefined
};

struct parser_dependency_edge {
    registered_parser_basis_ptr target;
    parser_dependency_edge_kind kind;
};

[[nodiscard]] inline std::vector<parser_dependency_edge>
direct_parser_dependency_edges_in_definition(
    quoted_expression const& expression,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    auto const& expression_root = quoted_access::root(expression);
    if (expression_root->kind() != quoted_node_kind::basis) {
        throw std::logic_error(
            "combdsl::registered parser basis is not a basis");
    }

    std::unordered_map<
        quoted_node const*, registered_parser_basis_ptr>
        registered_roots;
    for (auto const& [name, registered] : registered_bases) {
        static_cast<void>(name);
        registered_roots.emplace(
            quoted_access::root(registered->expression()).get(),
            registered);
    }
    for (auto const& [name, versions] : registered_versions) {
        static_cast<void>(name);
        for (auto const& registered : versions) {
            registered_roots.emplace(
                quoted_access::root(registered->expression()).get(),
                registered);
        }
    }

    std::vector<parser_dependency_edge> result;
    auto add_edge = [&result](
                        registered_parser_basis_ptr target,
                        parser_dependency_edge_kind kind) {
        if (target->predefined()) {
            kind = parser_dependency_edge_kind::predefined;
        }
        auto const already_added = std::ranges::find_if(
            result,
            [&](parser_dependency_edge const& edge) {
                return edge.target == target && edge.kind == kind;
            });
        if (already_added == result.end()) {
            result.push_back({std::move(target), kind});
        }
    };

    auto const& definition =
        static_cast<quoted_basis_node_base const&>(*expression_root);
    std::unordered_set<quoted_node const*> visited;
    std::vector<std::shared_ptr<quoted_node const>> pending{
        quoted_access::root(definition.body())};
    while (!pending.empty()) {
        auto root = std::move(pending.back());
        pending.pop_back();
        if (!visited.emplace(root.get()).second) {
            continue;
        }

        if (auto const* reference = dynamic_cast<
                quoted_parser_basis_reference_node const*>(
                root.get())) {
            auto target = reference->resolve_from(registered_bases);
            auto const kind = reference->is_live_binding()
                ? parser_dependency_edge_kind::live
                : parser_dependency_edge_kind::captured;
            add_edge(std::move(target), kind);
            continue;
        }

        if (root->kind() == quoted_node_kind::basis) {
            registered_parser_basis_ptr target;
            if (auto const match = registered_roots.find(root.get());
                match != registered_roots.end()) {
                target = match->second;
            } else {
                auto const name =
                    static_cast<quoted_basis_node_base const&>(
                        *root).definition_name();
                if (auto const current = registered_bases.find(name);
                    current != registered_bases.end()) {
                    target = current->second;
                } else if (auto const versions =
                               registered_versions.find(name);
                           versions != registered_versions.end() &&
                           !versions->second.empty()) {
                    target = versions->second.back();
                }
            }
            if (target) {
                add_edge(
                    std::move(target),
                    parser_dependency_edge_kind::captured);
            }
            continue;
        }

        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(*root);
            pending.push_back(
                quoted_access::root(application.argument()));
            pending.push_back(
                quoted_access::root(application.function()));
            break;
        }
        case quoted_node_kind::pending_sk:
            pending.push_back(quoted_access::root(
                static_cast<quoted_pending_sk_node const&>(*root)
                    .application()));
            break;
        case quoted_node_kind::recursive_y:
            pending.push_back(quoted_access::root(
                static_cast<quoted_recursive_y_node const&>(*root)
                    .generator()));
            break;
        case quoted_node_kind::basis_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_basis_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::colored_argument:
            pending.push_back(quoted_access::root(
                static_cast<quoted_colored_argument_node const&>(*root)
                    .argument()));
            break;
        case quoted_node_kind::opaque:
        case quoted_node_kind::rec_func:
        case quoted_node_kind::identity:
        case quoted_node_kind::constant:
        case quoted_node_kind::substitution:
        case quoted_node_kind::fixed_point:
        case quoted_node_kind::basis:
            break;
        }
    }

    std::ranges::sort(
        result,
        [](parser_dependency_edge const& left,
           parser_dependency_edge const& right) {
            if (left.target->name() != right.target->name()) {
                return left.target->name() < right.target->name();
            }
            if (left.target->version() != right.target->version()) {
                return left.target->version() <
                       right.target->version();
            }
            return left.kind < right.kind;
        });
    return result;
}

struct parser_dependency_path {
    std::vector<registered_parser_basis_ptr> nodes;
    std::vector<parser_dependency_edge_kind> edge_kinds;

    [[nodiscard]] bool empty() const noexcept {
        return nodes.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes.size();
    }
};

[[nodiscard]] inline bool parser_dependency_path_less(
    parser_dependency_path const& left,
    parser_dependency_path const& right) {
    auto const common_size =
        std::min(left.nodes.size(), right.nodes.size());
    for (std::size_t index = 0; index < common_size; ++index) {
        auto const left_name = left.nodes[index]->name();
        auto const right_name = right.nodes[index]->name();
        if (left_name != right_name) {
            return left_name < right_name;
        }
    }
    if (left.nodes.size() != right.nodes.size()) {
        return left.nodes.size() < right.nodes.size();
    }
    for (std::size_t index = 0; index < left.nodes.size(); ++index) {
        if (left.nodes[index]->version() !=
            right.nodes[index]->version()) {
            return left.nodes[index]->version() <
                   right.nodes[index]->version();
        }
    }
    if (left.edge_kinds != right.edge_kinds) {
        return std::ranges::lexicographical_compare(
            left.edge_kinds, right.edge_kinds);
    }
    return false;
}

[[nodiscard]] inline parser_dependency_path
shortest_parser_dependency_path(
    registered_parser_basis_ptr const& start,
    std::string_view target_name,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    std::vector<parser_dependency_path> current_paths{
        {{start}, {}}};
    std::unordered_set<registered_parser_basis const*> visited{
        start.get()};
    while (!current_paths.empty()) {
        std::ranges::sort(
            current_paths, parser_dependency_path_less);
        for (auto const& path : current_paths) {
            if (path.nodes.back()->name() == target_name) {
                return path;
            }
        }

        std::vector<parser_dependency_path> candidates;
        for (auto const& path : current_paths) {
            auto edges =
                direct_parser_dependency_edges_in_definition(
                    path.nodes.back()->expression(), registered_bases,
                    registered_versions);
            for (auto const& edge : edges) {
                auto candidate = path;
                candidate.nodes.push_back(edge.target);
                candidate.edge_kinds.push_back(edge.kind);
                candidates.push_back(std::move(candidate));
            }
        }
        std::ranges::sort(candidates, parser_dependency_path_less);
        current_paths.clear();
        for (auto& candidate : candidates) {
            if (visited.emplace(candidate.nodes.back().get()).second) {
                current_paths.push_back(std::move(candidate));
            }
        }
    }
    return {};
}

[[nodiscard]] inline parser_dependency_path
parser_dependency_path_between(
    std::string_view first_name,
    std::string_view second_name,
    registered_parser_basis_table const& registered_bases,
    parser_basis_version_history const& registered_versions) {
    auto const first = registered_bases.find(first_name);
    auto const second = registered_bases.find(second_name);
    if (first == registered_bases.end() ||
        second == registered_bases.end()) {
        return {};
    }
    auto first_to_second = shortest_parser_dependency_path(
        first->second, second_name, registered_bases,
        registered_versions);
    auto second_to_first = shortest_parser_dependency_path(
        second->second, first_name, registered_bases,
        registered_versions);
    if (first_to_second.empty()) {
        return second_to_first;
    }
    if (second_to_first.empty()) {
        return first_to_second;
    }
    if (first_to_second.size() != second_to_first.size()) {
        return first_to_second.size() < second_to_first.size()
            ? first_to_second
            : second_to_first;
    }
    return parser_dependency_path_less(
               first_to_second, second_to_first)
        ? first_to_second
        : second_to_first;
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
                right_basis->predefined() ||
            left_basis->reference_mode() !=
                right_basis->reference_mode()) {
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

[[nodiscard]] inline bool same_parser_basis_versions(
    parser_basis_version_history const& left,
    parser_basis_version_history const& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (auto const& [name, left_versions] : left) {
        auto const match = right.find(name);
        if (match == right.end() ||
            match->second.size() != left_versions.size()) {
            return false;
        }
        for (std::size_t index = 0;
             index < left_versions.size(); ++index) {
            auto const& left_basis = left_versions[index];
            auto const& right_basis = match->second[index];
            if (left_basis == right_basis) {
                continue;
            }
            if (left_basis->predefined() !=
                    right_basis->predefined() ||
                left_basis->version() != right_basis->version() ||
                left_basis->reference_mode() !=
                    right_basis->reference_mode() ||
                !same_parser_basis_definition(
                    left_basis->expression(),
                    right_basis->expression())) {
                return false;
            }
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
    std::lock_guard transaction_lock(
        detail::parser_definition_transaction_mutex());
    std::lock_guard lock(detail::parser_basis_registry_mutex());
    auto const& definitions = detail::parser_definition_registry();

    if (definitions.empty()) {
        return {};
    }

    std::string result;
    if (!definitions.front().references_command) {
        result = "references captured";
    }
    for (auto const& definition : definitions) {
        if (!result.empty()) {
            result.push_back('\n');
        }
        result += definition.source;
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

struct step_limit_command {
    bool enabled = false;
    std::size_t limit = 0;
};

[[nodiscard]] inline std::optional<step_limit_command>
parse_step_limit_command(std::string_view source) {
    auto const whitespace = [](char value) noexcept {
        return value == ' ' || value == '\t' || value == '\n' ||
               value == '\r' || value == '\f' || value == '\v';
    };
    std::size_t position = 0;
    auto skip_whitespace = [&] {
        while (position < source.size() &&
               whitespace(source[position])) {
            ++position;
        }
    };
    auto next_word = [&] {
        skip_whitespace();
        auto const start = position;
        while (position < source.size() &&
               !whitespace(source[position])) {
            ++position;
        }
        return std::pair{
            source.substr(start, position - start), start};
    };

    if (next_word().first != "step") {
        return std::nullopt;
    }

    auto const [subcommand, subcommand_position] = next_word();
    if (subcommand != "limit") {
        throw parse_error(
            subcommand.empty() ? position : subcommand_position,
            "expected 'limit'");
    }

    auto const [option, option_position] = next_word();
    if (option.empty()) {
        throw parse_error(position, "expected 'off' or a number");
    }

    step_limit_command result;
    if (option == "off") {
        result.enabled = false;
    } else {
        std::size_t value = 0;
        for (char digit_character : option) {
            if (digit_character < '0' || digit_character > '9') {
                throw parse_error(
                    option_position, "expected 'off' or a number");
            }
            auto const digit = static_cast<std::size_t>(
                digit_character - '0');
            if (value >
                (std::numeric_limits<std::size_t>::max() - digit) /
                    10) {
                throw parse_error(
                    option_position, "step limit is too large");
            }
            value = value * 10 + digit;
        }
        if (value == 0) {
            throw parse_error(
                option_position,
                "step limit must be greater than zero");
        }
        result.enabled = true;
        result.limit = value;
    }

    skip_whitespace();
    if (position != source.size()) {
        throw parse_error(
            position, "unexpected input after step limit");
    }
    return result;
}

struct combinator_find_result {
    std::vector<quoted_expression> singles;
    std::vector<quoted_expression> pairs;
    std::vector<quoted_expression> triples;
    std::vector<quoted_expression> quads;
};

struct combinator_find_options {
    std::size_t maximum_size = 3;
    bool all_sizes = false;
};

struct catalog_combinator_find_result {
    std::vector<std::vector<quoted_expression>> completed_sizes;
    bool timed_out = false;
};

[[nodiscard]] inline combinator_find_result
find_combinator_matches(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression,
    combinator_find_options options = {});

[[nodiscard]] inline catalog_combinator_find_result
find_combinator_matches_among(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression,
    std::span<quoted_expression const> catalog,
    bool all_sizes = false);

namespace detail {

using find_clock = std::chrono::steady_clock;

inline thread_local std::function<find_clock::time_point()>
    find_clock_now_override;

[[nodiscard]] inline find_clock::time_point find_clock_now() {
    if (find_clock_now_override) {
        return find_clock_now_override();
    }
    return find_clock::now();
}

inline constexpr auto find_search_window =
    std::chrono::seconds{10};

using compare_clock = std::chrono::steady_clock;

inline thread_local std::function<compare_clock::time_point()>
    compare_clock_now_override;

[[nodiscard]] inline compare_clock::time_point
compare_clock_now() {
    if (compare_clock_now_override) {
        return compare_clock_now_override();
    }
    return compare_clock::now();
}

inline constexpr auto compare_normalization_window =
    std::chrono::milliseconds{500};

[[nodiscard]] inline std::optional<quoted_expression>
normalize_for_compare_until(
    quoted_expression expression,
    compare_clock::time_point deadline) {
    reduction_options const options{.basis_step = true};
    for (;;) {
        if (compare_clock_now() >= deadline) {
            return std::nullopt;
        }
        auto reduced = reduce_next_redex(expression, options);
        if (compare_clock_now() >= deadline) {
            return std::nullopt;
        }
        if (!reduced) {
            return expression;
        }
        expression = std::move(*reduced);
    }
}

[[nodiscard]] inline std::optional<quoted_expression>
normalize_for_compare(quoted_expression expression) {
    auto const deadline =
        compare_clock_now() + compare_normalization_window;
    return normalize_for_compare_until(
        std::move(expression), deadline);
}

enum class parser_definition_mode {
    register_definitions,
    inspect_definitions
};

struct parsed_input {
    quoted_expression expression;
    bool is_definition;
    bool is_display_only;
    bool is_show_all;
    bool is_find;
    bool is_find_no_match;
    std::string replaced_definition;
};

class quoted_expression_parser {
public:
    explicit quoted_expression_parser(
        std::string_view source,
        parser_definition_mode definition_mode)
        : source_(source), definition_mode_(definition_mode) {
        auto snapshot = registered_parser_lookup_snapshot();
        registered_bases_ = std::move(snapshot.bases);
        registered_versions_ = std::move(snapshot.versions);
        registered_live_bindings_ =
            std::move(snapshot.live_bindings);
        snapshot_enabled_ = snapshot.snapshot_enabled;
    }

    [[nodiscard]] parsed_input parse_input() {
        skip_whitespace();
        if (at_end()) {
            fail("expected an expression");
        }
        if (current() == ')') {
            fail("unexpected ')'");
        }

        if (is_argumentless_command("load") ||
            is_argumentless_command("save")) {
            throw parse_error(source_.size(), "missing filename");
        }

        auto const is_set_definition =
            begins_command("set");
        auto const is_define_definition =
            begins_command("define");
        auto const is_remove_definition =
            begins_command("remove");
        auto const is_references_command =
            begins_command("references");
        auto const is_legacy_snapshot_command =
            begins_command("snapshot");
        auto const is_show_command =
            begins_command("show");
        auto const is_revisions_command =
            begins_command("revisions");
        auto const is_inspect_command =
            begins_command("inspect");
        auto const is_compare_command =
            begins_command("compare");
        auto const is_find_command =
            begins_command("find");
        auto const is_abstract_command =
            begins_command("abstract");
        auto const is_depended_on_by_command =
            begins_command("dependson") ||
            begins_command("depends-on") ||
            begins_command("depends");
        auto const is_uses_command =
            begins_command("usedby") ||
            begins_command("used-by") ||
            begins_command("used");
        auto const is_definition =
            is_set_definition || is_define_definition ||
            is_remove_definition || is_references_command ||
            is_legacy_snapshot_command;
        auto result = is_set_definition
            ? parse_set_definition()
            : is_define_definition
                ? parse_define_definition()
                : is_remove_definition
                    ? parse_remove_definition()
                    : is_references_command
                        ? parse_references_command(false)
                        : is_legacy_snapshot_command
                            ? parse_references_command(true)
                            : is_show_command
                                ? parse_show_command()
                                : is_revisions_command
                                    ? parse_revisions_command()
                                    : is_inspect_command
                                        ? parse_inspect_command()
                                        : is_compare_command
                                            ? parse_compare_command()
                                            : is_find_command
                                                ? parse_find_command()
                                                : is_abstract_command
                                                    ? parse_abstract_command()
                                                    : is_depended_on_by_command
                                                        ? parse_dependency_command(
                                                              parser_dependency_direction::
                                                                  depended_on_by)
                                                        : is_uses_command
                                                            ? parse_dependency_command(
                                                                  parser_dependency_direction::
                                                                      uses)
                                                            : parse_expression();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }
        return {
            std::move(result),
            is_definition,
            is_show_command || is_revisions_command ||
                is_inspect_command || is_compare_command ||
                is_find_command ||
                is_abstract_command ||
                is_depended_on_by_command || is_uses_command,
            is_show_all_,
            is_find_command,
            is_find_no_match_,
            std::move(replaced_definition_)};
    }

private:
    struct optimizer_substitution {
        quoted_expression before;
        quoted_expression after;
    };

    struct inspect_reference {
        std::string name;
        std::string classification;
    };

    struct find_catalog_entry {
        quoted_expression expression;
        registered_parser_basis_ptr registration;
        bool live_reference = false;
    };

    struct find_catalog_prefix_match {
        find_catalog_entry entry;
        std::size_t size;
    };

    [[nodiscard]] static std::string inspect_printed_expression(
        quoted_expression const& expression) {
        std::ostringstream output;
        expression.print_to(output);
        return std::move(output).str();
    }

    [[nodiscard]] static bool inspect_input_matches_canonical(
        std::string_view input,
        std::string_view canonical) noexcept {
        std::size_t input_position = 0;
        std::size_t canonical_position = 0;
        while (input_position < input.size()) {
            auto byte = input[input_position++];
            if (byte == '\\' && input_position < input.size() &&
                (input[input_position] == '\\' ||
                 input[input_position] == '"')) {
                byte = input[input_position++];
            }
            if (canonical_position == canonical.size() ||
                canonical[canonical_position++] != byte) {
                return false;
            }
        }
        return canonical_position == canonical.size();
    }

    static void inspect_expression_contents(
        quoted_expression const& expression,
        std::vector<std::string>& free_symbols,
        std::vector<inspect_reference>& references) {
        std::unordered_set<std::string> seen_references;
        std::vector<quoted_expression> pending{expression};

        auto append_reference = [&](std::string name,
                                    std::string classification) {
            if (seen_references.emplace(name).second) {
                references.push_back({
                    std::move(name), std::move(classification)});
            }
        };

        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            auto const& root = quoted_access::root(current);

            switch (root->kind()) {
            case quoted_node_kind::application: {
                auto const& application =
                    static_cast<quoted_application_node const&>(*root);
                pending.push_back(application.argument());
                pending.push_back(application.function());
                break;
            }
            case quoted_node_kind::pending_sk:
                pending.push_back(
                    static_cast<quoted_pending_sk_node const&>(*root)
                        .application());
                break;
            case quoted_node_kind::recursive_y:
                pending.push_back(
                    static_cast<quoted_recursive_y_node const&>(*root)
                        .generator());
                break;
            case quoted_node_kind::basis_argument:
                pending.push_back(
                    static_cast<quoted_basis_argument_node const&>(*root)
                        .argument());
                break;
            case quoted_node_kind::colored_argument:
                pending.push_back(
                    static_cast<quoted_colored_argument_node const&>(*root)
                        .argument());
                break;
            case quoted_node_kind::opaque:
                if (root->atomic_kind() == quoted_atomic_kind::symbol) {
                    free_symbols.emplace_back(root->atomic_name());
                }
                break;
            case quoted_node_kind::identity:
                append_reference("I", "fundamental");
                break;
            case quoted_node_kind::constant:
                append_reference("K", "fundamental");
                break;
            case quoted_node_kind::substitution:
                append_reference("S", "fundamental");
                break;
            case quoted_node_kind::fixed_point:
                append_reference("Y", "fundamental");
                break;
            case quoted_node_kind::basis: {
                auto classification = std::string("pre-defined");
                if (auto const* reference = dynamic_cast<
                        quoted_parser_basis_reference_node const*>(
                        root.get())) {
                    if (!reference->frozen_target()->predefined()) {
                        classification = reference->is_live_binding()
                            ? "live"
                            : "captured";
                    }
                }
                append_reference(
                    inspect_printed_expression(current),
                    std::move(classification));
                break;
            }
            case quoted_node_kind::rec_func:
                break;
            }
        }

        std::ranges::sort(free_symbols);
        free_symbols.erase(
            std::unique(free_symbols.begin(), free_symbols.end()),
            free_symbols.end());
    }

    [[nodiscard]] static std::string inspect_redex_kind(
        quoted_expression expression) {
        while (quoted_access::root(expression)->kind() ==
               quoted_node_kind::application) {
            expression = static_cast<quoted_application_node const&>(
                *quoted_access::root(expression)).function();
        }

        switch (quoted_access::root(expression)->kind()) {
        case quoted_node_kind::identity:
            return "I";
        case quoted_node_kind::constant:
            return "K";
        case quoted_node_kind::substitution:
            return "S";
        case quoted_node_kind::fixed_point:
        case quoted_node_kind::recursive_y:
            return "Y";
        case quoted_node_kind::basis:
            return inspect_printed_expression(expression);
        case quoted_node_kind::pending_sk:
            return "SK";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] static std::string inspect_redex_location(
        std::vector<redex_path_frame> const& path) {
        if (path.empty()) {
            return "root";
        }

        std::string result;
        for (auto const& frame : path) {
            if (!result.empty()) {
                result.push_back('.');
            }
            result += frame.visiting_argument
                ? "argument"
                : "function";
        }
        return result;
    }

    [[nodiscard]] quoted_expression parse_inspect_command() {
        constexpr std::size_t keyword_size = 7;
        position_ += keyword_size;
        skip_whitespace();

        auto const original_expression = source_.substr(position_);
        auto expression = parse_expression();
        std::vector<std::string> free_symbols;
        std::vector<inspect_reference> references;
        inspect_expression_contents(
            expression, free_symbols, references);

        auto const canonical =
            inspect_printed_expression(expression);
        std::ostringstream output;
        if (!inspect_input_matches_canonical(
                original_expression, canonical)) {
            output << "canonical: " << canonical << '\n';
        }
        output << "free symbols:";
        if (free_symbols.empty()) {
            output << " none";
        } else {
            for (auto const& symbol : free_symbols) {
                output << ' ' << symbol;
            }
        }

        if (references.empty()) {
            output << "\nreferences: none";
        } else {
            output << "\nreferences:";
            for (auto const& reference : references) {
                output << "\n  " << reference.name << " ["
                       << reference.classification << ']';
            }
        }

        auto selected = locate_next_parsed_redex(expression);
        output << "\nnext reduction: ";
        if (!selected) {
            output << "none [normal form]";
        } else {
            selected->expression.print_to(output);
            output << " ["
                   << inspect_redex_kind(selected->expression)
                   << " at "
                   << inspect_redex_location(selected->path)
                   << ']';
        }
        return quote(std::move(output).str());
    }

    [[nodiscard]] quoted_expression parse_compare_command() {
        std::lock_guard transaction_lock(
            parser_definition_transaction_mutex());

        constexpr std::size_t keyword_size = 7;
        position_ += keyword_size;
        skip_whitespace();

        if (at_end() || current() != '?') {
            fail("expected '?'");
        }
        ++position_;

        std::string symbols;
        while (!at_end() &&
               current() >= 'a' && current() <= 'z') {
            symbols.push_back(current());
            ++position_;
        }
        if (symbols.empty()) {
            fail("expected at least one symbol");
        }
        if (at_end()) {
            fail("expected a left expression");
        }
        if (!is_whitespace(current())) {
            fail("expected whitespace after symbol list");
        }
        skip_whitespace();
        if (at_end() || current() == '=') {
            fail("expected a left expression");
        }

        auto left = parse_expression(true);
        skip_whitespace();
        if (at_end() || current() != '=') {
            fail("expected '='");
        }
        ++position_;
        skip_whitespace();
        if (at_end()) {
            fail("expected a right expression");
        }
        auto right = parse_expression();

        if (definition_mode_ ==
            parser_definition_mode::inspect_definitions) {
            return right;
        }

        for (auto const symbol_name : symbols) {
            auto argument = quote(symbol(symbol_name));
            left = left(argument);
            right = right(std::move(argument));
        }

        auto left_normal = normalize_for_compare(std::move(left));
        if (!left_normal) {
            return quote(std::string("inconclusive"));
        }
        auto right_normal = normalize_for_compare(std::move(right));
        if (!right_normal) {
            return quote(std::string("inconclusive"));
        }

        std::ostringstream output;
        if (same_parser_definition_expression(
                *left_normal, *right_normal)) {
            output << "both reduce to: ";
            left_normal->print_to(output);
        } else {
            output << "left reduces to: ";
            left_normal->print_to(output);
            output << "\nright reduces to: ";
            right_normal->print_to(output);
        }
        return quote(std::move(output).str());
    }

    [[nodiscard]] quoted_expression parse_references_command(
        bool legacy_snapshot) {
        auto const keyword_size = legacy_snapshot
            ? std::size_t{8}
            : std::size_t{10};
        position_ += keyword_size;
        skip_whitespace();

        if (at_end()) {
            throw parse_error(
                position_,
                legacy_snapshot
                    ? "expected 'on' or 'off'"
                    : "expected 'captured' or 'live'");
        }

        bool enabled = true;
        if (!at_end()) {
            auto const option_position = position_;
            auto option_end = position_;
            while (option_end < source_.size() &&
                   !is_whitespace(source_[option_end])) {
                ++option_end;
            }
            auto const option = source_.substr(
                option_position, option_end - option_position);
            if ((!legacy_snapshot && option == "captured") ||
                (legacy_snapshot && option == "on")) {
                enabled = true;
            } else if ((!legacy_snapshot && option == "live") ||
                       (legacy_snapshot && option == "off")) {
                enabled = false;
            } else {
                throw parse_error(
                    option_position,
                    legacy_snapshot
                        ? "expected 'on' or 'off'"
                        : "expected 'captured' or 'live'");
            }
            position_ = option_end;
            skip_whitespace();
            if (!at_end()) {
                fail(legacy_snapshot
                    ? "unexpected input after snapshot option"
                    : "unexpected input after references option");
            }
        }

        std::string canonical = enabled
            ? "references captured"
            : "references live";
        if (definition_mode_ ==
            parser_definition_mode::register_definitions) {
            register_parser_reference_mode(enabled, canonical);
        }
        return quote(std::move(canonical));
    }

    [[nodiscard]] bool begins_command(
        std::string_view keyword) const noexcept {
        auto const remaining = source_.substr(position_);
        return remaining.starts_with(keyword) &&
               (remaining.size() == keyword.size() ||
                (remaining.size() > keyword.size() &&
                 is_whitespace(remaining[keyword.size()])));
    }

    [[nodiscard]] bool is_argumentless_command(
        std::string_view keyword) const noexcept {
        auto const remaining = source_.substr(position_);
        if (!remaining.starts_with(keyword)) {
            return false;
        }
        auto offset = keyword.size();
        while (offset < remaining.size() &&
               is_whitespace(remaining[offset])) {
            ++offset;
        }
        return offset == remaining.size();
    }

    [[nodiscard]] std::optional<parser_reference_mode>
    parse_definition_reference_mode() {
        auto const remaining = source_.substr(position_);
        std::optional<parser_reference_mode> mode;
        std::size_t keyword_size = 0;
        if (remaining.starts_with("captured") &&
            (remaining.size() == 8 ||
             is_whitespace(remaining[8]))) {
            mode = parser_reference_mode::captured;
            keyword_size = 8;
        } else if (remaining.starts_with("live") &&
                   (remaining.size() == 4 ||
                    is_whitespace(remaining[4]))) {
            mode = parser_reference_mode::live;
            keyword_size = 4;
        } else {
            return std::nullopt;
        }

        position_ += keyword_size;
        snapshot_enabled_ =
            *mode == parser_reference_mode::captured;
        skip_whitespace();
        return mode;
    }

    [[nodiscard]] quoted_expression parse_set_definition() {
        constexpr std::size_t keyword_size = 3;
        position_ += keyword_size;
        skip_whitespace();
        if (at_end()) {
            fail("missing combinator name");
        }

        auto const reference_mode =
            parse_definition_reference_mode();
        if (at_end()) {
            fail("missing combinator name");
        }

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
        auto dependencies = referenced_user_parser_bases(
            body, registered_bases_);
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }

        auto user_source = canonical_set_definition(
            reference_mode,
            name.view(), arity, source_.substr(body_position));
        auto result = make_quoted_basis_snapshot(
            name, arity, std::move(body));
        finish_definition(
            name.view(),
            name_position,
            result,
            std::move(user_source),
            std::move(dependencies),
            snapshot_enabled_
                ? parser_reference_mode::captured
                : parser_reference_mode::live,
            true);
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
            fail("missing combinator name");
        }

        auto const name =
            source_.substr(name_position, name_end - name_position);
        position_ = source_.size();

        if (name == "all") {
            is_show_all_ = true;
            auto definitions = set_list();
            if (definitions.empty()) {
                return quote(std::string("Nothing to show"));
            }
            return quote(std::move(definitions));
        }

        if (is_primitive_name(name)) {
            auto message = std::string(name);
            message += " is a fundamental name";
            message += " with arity:";
            message += name == "S" ? "3" :
                       name == "K" ? "2" : "1";
            return quote(std::move(message));
        }

        registered_parser_basis_ptr shown_basis;
        if (auto versioned =
                parse_versioned_basis_name(name, name_position)) {
            auto const match = registered_versions_.find(
                versioned->first);
            if (match != registered_versions_.end() &&
                versioned->second != 0 &&
                versioned->second <= match->second.size()) {
                shown_basis =
                    match->second[versioned->second - 1];
            }
        } else if (auto const match = registered_bases_.find(name);
                   match != registered_bases_.end()) {
            shown_basis = match->second;
        }
        if (!shown_basis) {
            auto message = unescape_input(name);
            message += " is not a defined name";
            throw parse_error(name_position, message);
        }

        auto result = shown_basis->expression();
        auto const& root = quoted_access::root(result);
        if (root->kind() != quoted_node_kind::basis) {
            throw std::logic_error(
                "combdsl::registered parser basis is not a basis");
        }
        auto const& basis =
            static_cast<quoted_basis_node_base const&>(*root);
        std::ostringstream output;
        output << "arity:" << basis.arity() << ' ';
        basis.body().print_to(output);
        return quote(std::move(output).str());
    }

    [[nodiscard]] quoted_expression parse_revisions_command() {
        constexpr std::size_t keyword_size = 9;
        position_ += keyword_size;
        skip_whitespace();

        auto const name_position = position_;
        if (at_end()) {
            fail("missing combinator name");
        }
        auto const [name_text, parsed_name_position] =
            parse_definition_basis_name_token();
        if (!name_text.empty() && name_text.front() == '?') {
            throw parse_error(
                parsed_name_position,
                "combdsl::basis names cannot begin with ?");
        }
        if (auto versioned = parse_versioned_basis_name(
                name_text, parsed_name_position)) {
            static_cast<void>(versioned);
            throw parse_error(
                parsed_name_position + name_text.rfind('@'),
                "version suffix is not allowed in a revisions name");
        }
        basis_label name = [&] {
            try {
                return basis_label(name_text);
            } catch (std::length_error const& error) {
                throw parse_error(
                    parsed_name_position + 15, error.what());
            } catch (std::invalid_argument const& error) {
                throw parse_error(parsed_name_position, error.what());
            }
        }();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected input after name");
        }

        if (is_primitive_name(name.view())) {
            auto message = std::string(name.view());
            message += " is a fundamental name and has no revisions";
            throw parse_error(name_position, message);
        }

        auto const versions = registered_versions_.find(name.view());
        if (versions == registered_versions_.end() ||
            versions->second.empty()) {
            auto message = unescape_input(name.view());
            message += " is not a defined name";
            throw parse_error(name_position, message);
        }

        registered_parser_basis_ptr current_basis;
        if (auto const current = registered_bases_.find(name.view());
            current != registered_bases_.end()) {
            current_basis = current->second;
        }

        std::ostringstream output;
        for (std::size_t index = 0;
             index < versions->second.size(); ++index) {
            if (index != 0) {
                output << '\n';
            }
            auto const& revision = versions->second[index];
            auto expression = revision->expression();
            auto const& root = quoted_access::root(expression);
            if (root->kind() != quoted_node_kind::basis) {
                throw std::logic_error(
                    "combdsl::registered parser basis is not a basis");
            }
            auto const& basis =
                static_cast<quoted_basis_node_base const&>(*root);
            output << unescape_input(revision->name()) << '@'
                   << revision->version() << " arity:"
                   << basis.arity() << ' ';
            basis.body().print_to(output);
            if (revision->predefined()) {
                output << " [pre-defined]";
            } else {
                output << (revision->reference_mode() ==
                                   parser_reference_mode::captured
                               ? " [captured]"
                               : " [live]");
            }
            if (revision == current_basis) {
                output << " [current]";
            } else if (!current_basis &&
                       index + 1 == versions->second.size()) {
                output << " [removed]";
            }
        }
        return quote(std::move(output).str());
    }

    [[nodiscard]] quoted_expression parse_dependency_path_command() {
        auto parse_name = [this](std::string_view missing_message) {
            skip_whitespace();
            auto const name_position = position_;
            if (at_end()) {
                fail(missing_message);
            }
            auto const [name_text, parsed_name_position] =
                parse_definition_basis_name_token();
            basis_label name = [&] {
                try {
                    return basis_label(name_text);
                } catch (std::length_error const& error) {
                    throw parse_error(
                        parsed_name_position + 15, error.what());
                } catch (std::invalid_argument const& error) {
                    throw parse_error(
                        parsed_name_position, error.what());
                }
            }();
            return std::pair{std::move(name), name_position};
        };
        auto validate_name = [this](
                                 basis_label const& name,
                                 std::size_t name_position) {
            if (is_primitive_name(name.view())) {
                auto message = std::string(name.view());
                message +=
                    " is a fundamental name and cannot be queried";
                throw parse_error(name_position, message);
            }
            if (!registered_bases_.contains(name.view())) {
                auto message = unescape_input(name.view());
                message += " is not a defined name";
                throw parse_error(name_position, message);
            }
        };

        skip_whitespace();
        if (begins_command("between")) {
            position_ += 7;
        }
        auto [first, first_position] =
            parse_name("missing first combinator name");
        skip_whitespace();
        if (begins_command("and")) {
            position_ += 3;
        }
        auto [second, second_position] =
            parse_name("missing second combinator name");
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected input after second name");
        }

        validate_name(first, first_position);
        validate_name(second, second_position);
        if (first.view() == second.view()) {
            throw parse_error(
                second_position,
                "dependency path endpoints must be different");
        }

        auto const path = parser_dependency_path_between(
            first.view(), second.view(), registered_bases_,
            registered_versions_);
        if (path.empty()) {
            auto first_text = unescape_input(first.view());
            auto second_text = unescape_input(second.view());
            if (second_text < first_text) {
                std::swap(first_text, second_text);
            }
            first_text += " and ";
            first_text += second_text;
            first_text += " have no dependency path";
            return quote(std::move(first_text));
        }

        std::string output = unescape_input(path.nodes.front()->name());
        output += " uses ";
        output += unescape_input(path.nodes.back()->name());
        output += " via:";
        auto append_node = [&output](
                               registered_parser_basis_ptr const& node) {
            output += unescape_input(node->name());
            if (!node->predefined()) {
                output += '@';
                output += std::to_string(node->version());
            }
        };
        for (std::size_t index = 0;
             index < path.edge_kinds.size(); ++index) {
            output += "\n  ";
            append_node(path.nodes[index]);
            output += " -> ";
            auto const& target = path.nodes[index + 1];
            append_node(target);
            switch (path.edge_kinds[index]) {
            case parser_dependency_edge_kind::captured:
                output += "  [captured]";
                break;
            case parser_dependency_edge_kind::live:
                output += "  [live]";
                break;
            case parser_dependency_edge_kind::predefined:
                output += "  [pre-defined]";
                break;
            }
            if (!target->predefined() &&
                !registered_bases_.contains(target->name())) {
                output += " [name removed]";
            }
        }
        return quote(std::move(output));
    }

    [[nodiscard]] quoted_expression parse_dependency_command(
        parser_dependency_direction direction) {
        if (direction ==
            parser_dependency_direction::depended_on_by) {
            if (begins_command("dependson")) {
                position_ += 9;
            } else if (begins_command("depends-on")) {
                position_ += 10;
            } else {
                position_ += 7;
                skip_whitespace();
                auto const remaining = source_.substr(position_);
                if (!remaining.starts_with("on") ||
                    (remaining.size() > 2 &&
                     !is_whitespace(remaining[2]))) {
                    fail("expected 'on'");
                }
                position_ += 2;
            }
        } else {
            if (begins_command("usedby")) {
                position_ += 6;
            } else if (begins_command("used-by")) {
                position_ += 7;
            } else {
                position_ += 4;
                skip_whitespace();
                auto const remaining = source_.substr(position_);
                if (!remaining.starts_with("by") ||
                    (remaining.size() > 2 &&
                     !is_whitespace(remaining[2]))) {
                    fail("expected 'by'");
                }
                position_ += 2;
            }
        }

        skip_whitespace();
        if (direction == parser_dependency_direction::uses &&
            begins_command("path")) {
            position_ += 4;
            return parse_dependency_path_command();
        }
        bool const include_indirect = begins_command("all");
        if (include_indirect) {
            position_ += 3;
            skip_whitespace();
        }
        auto const name_position = position_;
        if (at_end()) {
            fail("missing combinator name");
        }
        auto const [name_text, parsed_name_position] =
            parse_definition_basis_name_token();
        basis_label name = [&] {
            try {
                return basis_label(name_text);
            } catch (std::length_error const& error) {
                throw parse_error(
                    parsed_name_position + 15, error.what());
            } catch (std::invalid_argument const& error) {
                throw parse_error(parsed_name_position, error.what());
            }
        }();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected input after name");
        }

        if (is_primitive_name(name.view())) {
            auto message = std::string(name.view());
            message +=
                " is a fundamental name and cannot be queried";
            throw parse_error(name_position, message);
        }
        if (!registered_bases_.contains(name.view())) {
            auto message = unescape_input(name.view());
            message += " is not a defined name";
            throw parse_error(name_position, message);
        }

        auto const names = parser_dependency_names(
            name.view(), direction, include_indirect, registered_bases_,
            registered_versions_);
        std::string output = unescape_input(name.view());
        if (direction ==
            parser_dependency_direction::depended_on_by) {
            output += names.direct.empty()
                ? " is not directly depended on by anything"
                : " is directly depended on by:";
        } else {
            output += names.direct.empty()
                ? " directly uses nothing"
                : " directly uses:";
        }
        for (auto const& dependency_name : names.direct) {
            output.push_back(' ');
            output += unescape_input(dependency_name);
        }
        if (!names.indirect.empty()) {
            output.push_back('\n');
            output += unescape_input(name.view());
            output += direction ==
                    parser_dependency_direction::depended_on_by
                ? " is indirectly depended on by:"
                : " indirectly uses:";
            for (auto const& dependency_name : names.indirect) {
                output.push_back(' ');
                output += unescape_input(dependency_name);
            }
        }
        return quote(std::move(output));
    }

    [[nodiscard]] std::optional<find_catalog_entry>
    try_resolve_unversioned_find_catalog_bird(
        std::string_view token) const {
        if (token == "S") {
            return find_catalog_entry{quote(S), {}, false};
        }
        if (token == "K") {
            return find_catalog_entry{quote(K), {}, false};
        }
        if (token == "I") {
            return find_catalog_entry{quote(I), {}, false};
        }
        if (token == "Y") {
            return find_catalog_entry{quote(Y), {}, false};
        }

        if (auto const match = registered_bases_.find(token);
            match != registered_bases_.end()) {
            return find_catalog_entry{
                parser_basis_reference(match->second),
                match->second,
                !match->second->predefined() && !snapshot_enabled_,
            };
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<find_catalog_entry>
    try_resolve_find_catalog_bird(
        std::string_view token,
        std::size_t token_position) const {
        if (has_reserved_version_suffix(token)) {
            auto const versioned =
                parse_versioned_basis_name(token, token_position);
            if (!versioned) {
                throw std::logic_error(
                    "combdsl::validated basis revision did not parse");
            }
            auto const match = registered_versions_.find(
                versioned->first);
            if (match != registered_versions_.end() &&
                versioned->second != 0 &&
                versioned->second <= match->second.size()) {
                auto registration =
                    match->second[versioned->second - 1];
                return find_catalog_entry{
                    make_parser_basis_reference(registration),
                    std::move(registration),
                    false,
                };
            }
            return std::nullopt;
        }
        return try_resolve_unversioned_find_catalog_bird(token);
    }

    [[nodiscard]] std::optional<find_catalog_prefix_match>
    longest_find_catalog_bird_prefix(
        std::string_view remaining) const {
        constexpr std::size_t maximum_basis_name_size = 15;
        std::optional<find_catalog_prefix_match> best;

        auto const maximum_name_size = std::min(
            remaining.size(), maximum_basis_name_size);
        for (auto prefix_size = maximum_name_size;
             prefix_size != 0;
             --prefix_size) {
            if (auto entry =
                    try_resolve_unversioned_find_catalog_bird(
                        remaining.substr(0, prefix_size))) {
                best.emplace(
                    find_catalog_prefix_match{
                        std::move(*entry), prefix_size});
                break;
            }
        }

        for (std::size_t name_size = 1;
             name_size <= maximum_name_size;
             ++name_size) {
            if (name_size + 1 >= remaining.size() ||
                remaining[name_size] != '@') {
                continue;
            }
            auto const versions = registered_versions_.find(
                remaining.substr(0, name_size));
            if (versions == registered_versions_.end()) {
                continue;
            }

            auto digit_position = name_size + 1;
            if (remaining[digit_position] < '0' ||
                remaining[digit_position] > '9') {
                continue;
            }

            std::size_t version = 0;
            std::optional<find_catalog_prefix_match>
                version_match;
            for (; digit_position < remaining.size();
                 ++digit_position) {
                auto const character = remaining[digit_position];
                if (character < '0' || character > '9') {
                    break;
                }
                auto const digit = static_cast<std::size_t>(
                    character - '0');
                if (version >
                    (std::numeric_limits<std::size_t>::max() -
                     digit) /
                        10) {
                    break;
                }

                version = version * 10 + digit;
                if (version == 0 ||
                    version > versions->second.size()) {
                    continue;
                }
                auto const prefix_size = digit_position + 1;
                auto registration =
                    versions->second[version - 1];
                version_match.emplace(find_catalog_prefix_match{
                    find_catalog_entry{
                        make_parser_basis_reference(registration),
                        std::move(registration),
                        false,
                    },
                    prefix_size,
                });
            }
            if (version_match &&
                (!best || version_match->size > best->size)) {
                best = std::move(version_match);
            }
        }
        return best;
    }

    [[nodiscard]] std::vector<quoted_expression>
    parse_find_catalog() {
        std::vector<find_catalog_entry> entries;
        while (!at_end() && current() != '?') {
            auto const group_position = position_;
            auto const group = current_basis_token();
            if (group.empty()) {
                fail("expected a bird name or '?'");
            }

            auto const group_end = group_position + group.size();
            auto append_bird = [&](find_catalog_entry bird) {
                auto const duplicate = std::ranges::any_of(
                    entries,
                    [&](find_catalog_entry const& existing) {
                        auto const same_registration =
                            existing.registration &&
                            bird.registration &&
                            existing.registration ==
                                bird.registration &&
                            existing.live_reference ==
                                bird.live_reference;
                        return same_registration ||
                               same_parser_definition_expression(
                                   existing.expression,
                                   bird.expression);
                    });
                if (!duplicate) {
                    entries.push_back(std::move(bird));
                }
            };

            if (auto exact = try_resolve_find_catalog_bird(
                    group, group_position)) {
                position_ = group_end;
                append_bird(std::move(*exact));
            } else {
                while (position_ < group_end) {
                    auto const remaining = source_.substr(
                        position_, group_end - position_);
                    auto prefix = longest_find_catalog_bird_prefix(
                        remaining);
                    if (!prefix) {
                        auto message = unescape_input(remaining);
                        message += " is not a defined name";
                        throw parse_error(position_, message);
                    }

                    position_ += prefix->size;
                    append_bird(std::move(prefix->entry));
                }
            }

            if (at_end()) {
                fail("expected '?'");
            }
            if (!is_whitespace(current())) {
                fail("expected whitespace after bird name");
            }
            skip_whitespace();
        }
        if (entries.empty()) {
            fail("expected at least one bird name");
        }
        std::vector<quoted_expression> catalog;
        catalog.reserve(entries.size());
        for (auto& entry : entries) {
            catalog.push_back(std::move(entry.expression));
        }
        return catalog;
    }

    [[nodiscard]] quoted_expression parse_find_command() {
        constexpr std::size_t keyword_size = 4;
        position_ += keyword_size;
        skip_whitespace();

        bool all_sizes = false;
        auto const remaining = source_.substr(position_);
        if (remaining.starts_with("all")) {
            all_sizes = true;
            position_ += 3;
            if (!at_end() && !is_whitespace(current())) {
                fail("expected whitespace after 'all'");
            }
            skip_whitespace();
        }

        std::size_t maximum_size = 3;
        std::vector<quoted_expression> catalog;
        bool search_catalog = false;
        auto const after_all = source_.substr(position_);
        if (after_all.starts_with("among")) {
            search_catalog = true;
            position_ += 5;
            if (!at_end() && !is_whitespace(current())) {
                fail("expected whitespace after 'among'");
            }
            skip_whitespace();
            catalog = parse_find_catalog();
        } else if (!at_end() && current() >= '0' && current() <= '9') {
            auto const size_position = position_;
            maximum_size = 0;
            bool size_too_large = false;
            while (!at_end() &&
                   current() >= '0' && current() <= '9') {
                auto const digit = static_cast<std::size_t>(
                    current() - '0');
                if (maximum_size != 0 || digit > 4) {
                    size_too_large = true;
                } else {
                    maximum_size = digit;
                }
                ++position_;
            }
            if (size_too_large || maximum_size == 0) {
                throw parse_error(
                    size_position,
                    "find maximum size must be from 1 to 4");
            }
            if (!at_end() && !is_whitespace(current())) {
                fail("expected whitespace after find maximum size");
            }
            skip_whitespace();
        }

        if (at_end() || current() != '?') {
            fail("expected '?'");
        }
        ++position_;

        std::vector<quoted_atomic> symbols;
        while (!at_end() &&
               current() >= 'a' && current() <= 'z') {
            symbols.emplace_back(symbol(current()));
            ++position_;
        }
        if (symbols.empty()) {
            fail("expected at least one symbol");
        }
        if (!at_end() && !is_whitespace(current()) &&
            current() != '=') {
            fail("expected a lowercase symbol or '='");
        }
        skip_whitespace();
        if (at_end() || current() != '=') {
            fail("expected '='");
        }
        ++position_;

        auto target = parse_expression();
        if (definition_mode_ ==
            parser_definition_mode::inspect_definitions) {
            return target;
        }

        std::ostringstream output;
        bool first = true;
        auto append_matches = [&](auto const& expressions) {
            for (auto const& expression : expressions) {
                if (!first) {
                    output << '\n';
                }
                output << "?=";
                expression.print_to(output);
                first = false;
            }
        };
        if (search_catalog) {
            auto matches = find_combinator_matches_among(
                symbols, target, catalog, all_sizes);
            for (auto const& size_matches :
                 matches.completed_sizes) {
                append_matches(size_matches);
            }
        } else {
            auto matches = find_combinator_matches(
                symbols,
                target,
                {.maximum_size = maximum_size,
                 .all_sizes = all_sizes});
            append_matches(matches.singles);
            append_matches(matches.pairs);
            append_matches(matches.triples);
            append_matches(matches.quads);
        }
        if (first) {
            is_find_no_match_ = true;
            return quote(std::string(
                "No match within search bounds"));
        }
        return quote(std::move(output).str());
    }

    [[nodiscard]] quoted_expression parse_abstract_command() {
        constexpr std::size_t keyword_size = 8;
        position_ += keyword_size;
        skip_whitespace();

        enum class trace_mode {
            none,
            steps,
            ministeps,
        };

        auto mode = trace_mode::none;
        auto const remaining = source_.substr(position_);
        if (remaining.starts_with("steps") &&
            (remaining.size() == 5 ||
             is_whitespace(remaining[5]))) {
            mode = trace_mode::steps;
            position_ += 5;
            if (!at_end() && !is_whitespace(current())) {
                fail("expected whitespace after 'steps'");
            }
            skip_whitespace();
        } else if (remaining.starts_with("ministeps") &&
                   (remaining.size() == 9 ||
                    is_whitespace(remaining[9]))) {
            mode = trace_mode::ministeps;
            position_ += 9;
            if (!at_end() && !is_whitespace(current())) {
                fail("expected whitespace after 'ministeps'");
            }
            skip_whitespace();
        }
        auto const show_steps = mode != trace_mode::none;

        if (at_end() || current() != '?') {
            fail("expected '?'");
        }
        ++position_;

        std::string symbols;
        while (!at_end() &&
               current() >= 'a' && current() <= 'z') {
            symbols.push_back(current());
            ++position_;
        }
        if (symbols.empty()) {
            fail("expected at least one symbol");
        }
        if (!at_end() && !is_whitespace(current()) &&
            current() != '=') {
            fail("expected a lowercase symbol or '='");
        }
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
        if (definition_mode_ ==
            parser_definition_mode::inspect_definitions) {
            return body;
        }

        std::ostringstream trace;
        bool first_trace_line = true;
        auto append_expression = [&](std::string_view label,
                                     quoted_expression const& value) {
            if (!first_trace_line) {
                trace << '\n';
            }
            trace << label;
            value.print_to(trace);
            first_trace_line = false;
        };

        auto const original_body = body;
        body = reduce_saturated_bases(std::move(body));
        if (show_steps &&
            !same_parser_definition_expression(
                original_body, body)) {
            trace << "preprocess: ";
            original_body.print_to(trace);
            trace << " -> ";
            body.print_to(trace);
            first_trace_line = false;
        }

        std::vector<quoted_atomic> pending_atoms;
        pending_atoms.reserve(symbols.size());
        for (auto const symbol_name : symbols) {
            pending_atoms.emplace_back(symbol(symbol_name));
        }
        for (auto symbol_position = symbols.rbegin();
             symbol_position != symbols.rend();
             ++symbol_position) {
            pending_atoms.pop_back();
            auto const before_takeout = body;
            auto const takeout_symbol =
                quoted_atomic{symbol(*symbol_position)};
            if (mode == trace_mode::ministeps) {
                if (!first_trace_line) {
                    trace << '\n';
                }
                trace << "takeout " << *symbol_position
                      << " from ";
                before_takeout.print_to(trace);
                trace << ": ";
                auto ministeps = takeout_with_pending_atoms_ministeps(
                    takeout_symbol,
                    std::move(body),
                    pending_atoms);
                ministeps.stages.front().print_to(trace);
                for (std::size_t stage = 1;
                     stage < ministeps.stages.size();
                     ++stage) {
                    trace << "\n= ";
                    ministeps.stages[stage].print_to(trace);
                }
                body = std::move(ministeps.result);
                first_trace_line = false;
            } else {
                body = takeout_with_pending_atoms(
                    takeout_symbol,
                    std::move(body),
                    pending_atoms);
                if (show_steps) {
                    if (!first_trace_line) {
                        trace << '\n';
                    }
                    trace << "takeout " << *symbol_position
                          << " from ";
                    before_takeout.print_to(trace);
                    trace << ": ";
                    body.print_to(trace);
                    first_trace_line = false;
                }
            }
        }

        std::vector<optimizer_substitution> substitutions;
        body = optimize_final_takeout(
            std::move(body),
            show_steps ? std::addressof(substitutions) : nullptr);
        if (!show_steps) {
            std::ostringstream output;
            output << "?=";
            body.print_to(output);
            return quote(std::move(output).str());
        }
        for (auto const& substitution : substitutions) {
            if (!first_trace_line) {
                trace << '\n';
            }
            trace << "optimize: ";
            substitution.before.print_to(trace);
            trace << " -> ";
            substitution.after.print_to(trace);
            first_trace_line = false;
        }
        append_expression("?=", body);
        return quote(std::move(trace).str());
    }

    [[nodiscard]] quoted_expression parse_remove_definition() {
        constexpr std::size_t keyword_size = 6;
        position_ += keyword_size;
        skip_whitespace();

        auto const name_position = position_;
        if (at_end()) {
            fail("missing combinator name");
        }
        auto const [name_text, parsed_name_position] =
            parse_definition_basis_name_token();
        if (!name_text.empty() && name_text.front() == '?') {
            throw parse_error(
                parsed_name_position,
                "combdsl::basis names cannot begin with ?");
        }
        if (auto versioned = parse_versioned_basis_name(
                name_text, parsed_name_position)) {
            static_cast<void>(versioned);
            throw parse_error(
                parsed_name_position + name_text.rfind('@'),
                "version suffix is not allowed in a removal name");
        }
        basis_label name = [&] {
            try {
                return basis_label(name_text);
            } catch (std::length_error const& error) {
                throw parse_error(
                    parsed_name_position + 15, error.what());
            } catch (std::invalid_argument const& error) {
                throw parse_error(parsed_name_position, error.what());
            }
        }();
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected input after name");
        }

        if (is_primitive_name(name.view())) {
            auto message = std::string(name.view());
            message +=
                " is a pre-defined basis and cannot be removed";
            throw parse_error(name_position, message);
        }

        auto const match = registered_bases_.find(name.view());
        if (match == registered_bases_.end()) {
            auto message = unescape_input(name.view());
            message += " is not a defined name";
            throw parse_error(name_position, message);
        }
        if (match->second->predefined()) {
            auto message = unescape_input(name.view());
            message +=
                " is a pre-defined basis and cannot be removed";
            throw parse_error(name_position, message);
        }

        auto result = match->second->expression();
        if (definition_mode_ ==
            parser_definition_mode::register_definitions) {
            auto const change = remove_parser_definition_basis(
                name.view(), canonical_remove_definition(name.view()));
            if (change == parser_removal_change::not_found) {
                auto message = unescape_input(name.view());
                message += " is not a defined name";
                throw parse_error(name_position, message);
            }
            if (change ==
                parser_removal_change::rejected_predefined) {
                auto message = unescape_input(name.view());
                message +=
                    " is a pre-defined basis and cannot be removed";
                throw parse_error(name_position, message);
            }
        }
        return result;
    }

    [[nodiscard]] quoted_expression parse_define_definition() {
        constexpr std::size_t keyword_size = 6;
        position_ += keyword_size;
        skip_whitespace();
        if (at_end()) {
            fail("missing combinator name");
        }

        auto const reference_mode =
            parse_definition_reference_mode();
        if (at_end()) {
            fail("missing combinator name");
        }

        auto const name_position = position_;
        auto [name, symbols] = parse_define_signature();
        auto const body_position = position_;
        auto recursive_function = make_quoted_rec_func(name);
        recursive_function_.emplace(recursive_function);
        auto body = parse_expression();
        recursive_function_.reset();
        auto dependencies = referenced_user_parser_bases(
            body, registered_bases_);
        skip_whitespace();
        if (!at_end()) {
            fail("unexpected ')'");
        }

        auto user_source = canonical_define_definition(
            reference_mode,
            name.view(), symbols, source_.substr(body_position));

        body = reduce_saturated_bases(std::move(body));
        std::vector<quoted_atomic> pending_atoms;
        pending_atoms.reserve(symbols.size() + 1);
        pending_atoms.emplace_back(recursive_function);
        for (auto const symbol_name : symbols) {
            pending_atoms.emplace_back(symbol(symbol_name));
        }
        for (auto symbol_position = symbols.rbegin();
             symbol_position != symbols.rend();
             ++symbol_position) {
            pending_atoms.pop_back();
            body = takeout_with_pending_atoms(
                quoted_atomic{symbol(*symbol_position)},
                std::move(body),
                pending_atoms);
        }

        if (contains_quoted_atom(recursive_function, body)) {
            pending_atoms.clear();
            body = takeout_with_pending_atoms(
                quoted_atomic{recursive_function},
                std::move(body),
                pending_atoms);
            body = optimize_final_takeout(
                quote(Y)(optimize_final_takeout(
                    std::move(body))));
        } else {
            body = optimize_final_takeout(
                std::move(body));
        }

        auto result = make_quoted_basis_snapshot(
            name, symbols.size(), std::move(body));
        finish_definition(
            name.view(),
            name_position,
            result,
            std::move(user_source),
            std::move(dependencies),
            snapshot_enabled_
                ? parser_reference_mode::captured
                : parser_reference_mode::live);
        return result;
    }

    void finish_definition(
        std::string_view name,
        std::size_t name_position,
        quoted_expression const& result,
        std::string user_source,
        std::vector<registered_parser_basis_ptr> dependencies,
        parser_reference_mode reference_mode,
        bool reject_circular = true) {
        parser_definition_change change;
        std::vector<std::string> circular_path;
        if (definition_mode_ ==
            parser_definition_mode::inspect_definitions) {
            auto inspection =
                inspect_parser_definition_basis(
                    name,
                    result,
                    registered_bases_,
                    reject_circular);
            change = inspection.change;
            replaced_definition_ =
                std::move(inspection.replaced_definition);
            circular_path = std::move(inspection.circular_path);
        } else {
            change = register_parser_definition_basis(
                name,
                result,
                std::move(user_source),
                std::move(dependencies),
                reference_mode,
                reject_circular,
                circular_path);
        }

        if (change ==
            parser_definition_change::rejected_predefined) {
            auto message = std::string(name);
            message +=
                " is a pre-defined basis and cannot be redefined";
            throw parse_error(name_position, message);
        }
        if (change ==
            parser_definition_change::rejected_circular) {
            auto message = unescape_input(name);
            message += " would have a circular definition";
            if (!circular_path.empty()) {
                message.push_back('\n');
                for (std::size_t index = 0;
                     index < circular_path.size();
                     ++index) {
                    if (index != 0) {
                        message += " -> ";
                    }
                    message += unescape_input(circular_path[index]);
                }
            }
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
        if (!is_lowercase_name(token)) {
            if (auto symbols =
                    parse_adjacent_definition_symbols(token)) {
                auto const name_size = token.size() - symbols->size();
                auto name = validated_definition_basis_name(
                    token.substr(0, name_size), name_position);
                return {std::move(name), std::move(*symbols)};
            }
        }

        auto name = validated_definition_basis_name(
            token, name_position);
        skip_whitespace();
        if (at_end()) {
            fail("expected '='");
        }
        if (current() == '=') {
            ++position_;
            return {std::move(name), {}};
        }

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
                       .definition_name() == name;
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

    [[nodiscard]] static bool is_cardinal_star_thrush(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "C*") &&
               is_named_basis(application->argument(), "T");
    }

    [[nodiscard]] static bool is_bluebird_double_dove(
        quoted_expression const& expression) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), "D")) {
            return false;
        }

        auto const* inner = as_application(outer->function());
        return inner != nullptr &&
               is_named_basis(inner->function(), "B") &&
               is_named_basis(inner->argument(), "D");
    }

    [[nodiscard]] static bool is_bluebird_owl_mockingbird(
        quoted_expression const& expression) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), "M")) {
            return false;
        }

        auto const* inner = as_application(outer->function());
        return inner != nullptr &&
               is_named_basis(inner->function(), "B") &&
               is_named_basis(inner->argument(), "O");
    }

    [[nodiscard]] static bool is_bluebird_queer_thrush(
        quoted_expression const& expression,
        std::string_view final_argument) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), final_argument)) {
            return false;
        }

        auto const* inner = as_application(outer->function());
        if (inner == nullptr ||
            !is_named_basis(inner->function(), "B")) {
            return false;
        }

        auto const* queer_thrush =
            as_application(inner->argument());
        return queer_thrush != nullptr &&
               is_named_basis(queer_thrush->function(), "Q") &&
               is_named_basis(queer_thrush->argument(), "T");
    }

    [[nodiscard]] static bool is_bluebird_thrush(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "T");
    }

    [[nodiscard]] static bool is_bluebird_warbler(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "W");
    }

    [[nodiscard]] static bool is_bluebird_warbler_star(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "W*");
    }

    [[nodiscard]] static bool is_bluebird_cardinal(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "C");
    }

    [[nodiscard]] static bool is_bluebird_cardinal_star(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "C*");
    }

    [[nodiscard]] static bool is_sage_owl(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               quoted_access::root(application->function())->kind() ==
                   quoted_node_kind::fixed_point &&
               is_named_basis(application->argument(), "O");
    }

    [[nodiscard]] static bool is_double_bluebird(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "B") &&
               is_named_basis(application->argument(), "B");
    }

    [[nodiscard]] static bool is_starling_bluebird_thrush(
        quoted_expression const& expression) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), "T")) {
            return false;
        }

        auto const* inner = as_application(outer->function());
        return inner != nullptr &&
               quoted_access::root(inner->function())->kind() ==
                   quoted_node_kind::substitution &&
               is_named_basis(inner->argument(), "B");
    }

    [[nodiscard]] static bool is_queer_mockingbird(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "Q") &&
               is_named_basis(application->argument(), "M");
    }

    [[nodiscard]] static bool is_dove_cardinal(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "D") &&
               is_named_basis(application->argument(), "C");
    }

    [[nodiscard]] static bool is_warbler_cardinal(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "W") &&
               is_named_basis(application->argument(), "C");
    }

    [[nodiscard]] static bool is_starling_robin(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               quoted_access::root(application->function())->kind() ==
                   quoted_node_kind::substitution &&
               is_named_basis(application->argument(), "R");
    }

    [[nodiscard]] static bool is_warbler_vireo(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "W") &&
               is_named_basis(application->argument(), "V");
    }

    [[nodiscard]] static bool is_warbler_bluebird(
        quoted_expression const& expression) noexcept {
        auto const* application = as_application(expression);
        return application != nullptr &&
               is_named_basis(application->function(), "W") &&
               is_named_basis(application->argument(), "B");
    }

    [[nodiscard]] static bool is_jay_pattern(
        quoted_expression const& expression) noexcept {
        auto const* outer = as_application(expression);
        if (outer == nullptr ||
            !is_named_basis(outer->argument(), "D")) {
            return false;
        }

        auto const* starling =
            as_application(outer->function());
        if (starling == nullptr ||
            quoted_access::root(starling->function())->kind() !=
                quoted_node_kind::substitution) {
            return false;
        }

        auto const* dove =
            as_application(starling->argument());
        if (dove == nullptr ||
            !is_named_basis(dove->function(), "D")) {
            return false;
        }

        auto const* cardinal =
            as_application(dove->argument());
        if (cardinal == nullptr ||
            !is_named_basis(cardinal->argument(), "C")) {
            return false;
        }

        auto const* bluebird_queer =
            as_application(cardinal->function());
        return bluebird_queer != nullptr &&
               is_named_basis(bluebird_queer->function(), "B") &&
               is_named_basis(bluebird_queer->argument(), "Q");
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

    [[nodiscard]] std::optional<quoted_expression>
    optimize_final_takeout_at_root(
        quoted_expression const& expression) const {
        if (is_cardinal_star_thrush(expression)) {
            return registered_basis_expression("V");
        }
        if (is_bluebird_double_dove(expression)) {
            return registered_basis_expression("E");
        }
        if (is_bluebird_owl_mockingbird(expression)) {
            return registered_basis_expression("U");
        }
        if (is_bluebird_queer_thrush(expression, "R")) {
            return registered_basis_expression("F");
        }
        if (is_bluebird_queer_thrush(expression, "B")) {
            return registered_basis_expression("Q1");
        }
        if (is_bluebird_thrush(expression)) {
            return registered_basis_expression("Q3");
        }
        if (is_bluebird_warbler_star(expression)) {
            return registered_basis_expression("W**");
        }
        if (is_bluebird_warbler(expression)) {
            return registered_basis_expression("W*");
        }
        if (is_bluebird_cardinal_star(expression)) {
            return registered_basis_expression("C**");
        }
        if (is_bluebird_cardinal(expression)) {
            return registered_basis_expression("C*");
        }
        if (is_sage_owl(expression)) {
            return quote(Y);
        }
        if (is_double_bluebird(expression)) {
            return registered_basis_expression("D");
        }
        if (is_starling_bluebird_thrush(expression)) {
            return registered_basis_expression("A");
        }
        if (is_starling_robin(expression)) {
            return registered_basis_expression("H");
        }
        if (is_queer_mockingbird(expression)) {
            return registered_basis_expression("L");
        }
        if (is_dove_cardinal(expression)) {
            return registered_basis_expression("G");
        }
        if (is_warbler_cardinal(expression)) {
            return registered_basis_expression("N");
        }
        if (is_warbler_vireo(expression)) {
            return registered_basis_expression("W1");
        }
        if (is_warbler_bluebird(expression)) {
            return registered_basis_expression("Z");
        }
        if (is_jay_pattern(expression)) {
            return registered_basis_expression("J");
        }
        return std::nullopt;
    }

    [[nodiscard]] quoted_expression optimize_final_takeout(
        quoted_expression expression,
        std::vector<optimizer_substitution>* substitutions =
            nullptr) const {
        if (auto optimized =
                optimize_final_takeout_at_root(expression)) {
            if (substitutions != nullptr) {
                substitutions->push_back({expression, *optimized});
            }
            return *optimized;
        }

        auto const* application = as_application(expression);
        if (application == nullptr) {
            return expression;
        }

        auto function = optimize_final_takeout(
            application->function(), substitutions);
        auto argument = optimize_final_takeout(
            application->argument(), substitutions);
        if (quoted_access::root(function) !=
                quoted_access::root(application->function()) ||
            quoted_access::root(argument) !=
                quoted_access::root(application->argument())) {
            expression = make_quoted_application(
                std::move(function), std::move(argument));
        }

        if (auto optimized =
                optimize_final_takeout_at_root(expression)) {
            if (substitutions != nullptr) {
                substitutions->push_back({expression, *optimized});
            }
            return *optimized;
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
        std::optional<parser_reference_mode> reference_mode,
        std::string_view name,
        std::size_t arity,
        std::string_view body) {
        std::string result = "set ";
        if (reference_mode) {
            result += *reference_mode ==
                    parser_reference_mode::captured
                ? "captured "
                : "live ";
        }
        result += name;
        result += " = ";
        result += std::to_string(arity);
        return append_canonical_body(std::move(result), body);
    }

    [[nodiscard]] static std::string canonical_define_definition(
        std::optional<parser_reference_mode> reference_mode,
        std::string_view name,
        std::string_view symbols,
        std::string_view body) {
        std::string result = "define ";
        if (reference_mode) {
            result += *reference_mode ==
                    parser_reference_mode::captured
                ? "captured "
                : "live ";
        }
        result += name;
        if (!symbols.empty()) {
            result.push_back(' ');
            result += symbols;
        }
        result += " =";
        return append_canonical_body(std::move(result), body);
    }

    [[nodiscard]] static std::string canonical_remove_definition(
        std::string_view name) {
        std::string result = "remove ";
        result += name;
        return unescape_input(result);
    }

    [[nodiscard]] basis_label validated_definition_basis_name(
        std::string_view name,
        std::size_t name_position) const {
        if (!name.empty() && name.front() == '?') {
            throw parse_error(
                name_position,
                "combdsl::basis names cannot begin with ?");
        }
        if (auto versioned =
                parse_versioned_basis_name(name, name_position)) {
            static_cast<void>(versioned);
            throw parse_error(
                name_position + name.rfind('@'),
                "version suffix is not allowed in a definition name");
        }
        if (name == "abstract" ||
            name == "all" ||
            name == "among" ||
            name == "path" ||
            name == "between" ||
            name == "and" ||
            name == "captured" ||
            name == "live" ||
            name == "limit" ||
            name == "step" ||
            name == "steps" ||
            name == "ministeps" ||
            name == "references" ||
            name == "revisions" ||
            name == "inspect" ||
            name == "compare" ||
            name == "snapshot" ||
            name == "set" ||
            name == "define" ||
            name == "show" ||
            name == "single" ||
            name == "key" ||
            name == "basis" ||
            name == "colorize" ||
            name == "about" ||
            name == "birds" ||
            name == "help" ||
            name == "load" ||
            name == "remove" ||
            name == "save" ||
            name == "find" ||
            name == "dependson" ||
            name == "depends-on" ||
            name == "depends" ||
            name == "on" ||
            name == "usedby" ||
            name == "used-by" ||
            name == "used" ||
            name == "by" ||
            name == "quit" ||
            name == "exit") {
            auto message = std::string(name);
            message += " is a reserved word";
            throw parse_error(name_position, message);
        }

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
            arity_end == source_.size() ||
            !is_whitespace(source_[arity_end])) {
            return 0;
        }

        auto body_position = arity_end;
        while (body_position < source_.size() &&
               is_whitespace(source_[body_position])) {
            ++body_position;
        }
        if (body_position == source_.size()) {
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

        position_ = body_position;
        return arity;
    }

    [[nodiscard]] quoted_expression parse_expression(
        bool stop_at_equals = false) {
        std::optional<quoted_expression> result;
        bool previous_atom_requires_token_separator = false;
        skip_whitespace();

        while (!at_end() && current() != ')' &&
               (!stop_at_equals || current() != '=')) {
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

        if (auto named_basis = parse_exact_named_basis_token()) {
            return std::move(*named_basis);
        }

        if (auto named_expression =
                parse_unseparated_multicharacter_name()) {
            return std::move(*named_expression);
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
            if (auto number = parse_numeric_literal()) {
                return std::move(*number);
            }
            fail("unknown operand");
        }
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_numeric_literal() {
        auto const literal_position = position_;
        auto end = position_;
        if (source_[end] == '+' || source_[end] == '-') {
            ++end;
        }

        auto const integer_begin = end;
        while (end < source_.size() &&
               source_[end] >= '0' && source_[end] <= '9') {
            ++end;
        }
        auto has_digits = end != integer_begin;

        bool floating = false;
        if (end < source_.size() && source_[end] == '.') {
            floating = true;
            ++end;
            auto const fraction_begin = end;
            while (end < source_.size() &&
                   source_[end] >= '0' && source_[end] <= '9') {
                ++end;
            }
            has_digits = has_digits || end != fraction_begin;
        }

        if (!has_digits) {
            if (floating) {
                fail("invalid numeric literal");
            }
            return std::nullopt;
        }

        bool has_exponent = false;
        if (end < source_.size() &&
            (source_[end] == 'e' || source_[end] == 'E')) {
            auto exponent_end = end + 1;
            if (exponent_end < source_.size() &&
                (source_[exponent_end] == '+' ||
                 source_[exponent_end] == '-')) {
                ++exponent_end;
                if (exponent_end == source_.size() ||
                    source_[exponent_end] < '0' ||
                    source_[exponent_end] > '9') {
                    fail("invalid numeric literal");
                }
            }
            if (exponent_end < source_.size() &&
                source_[exponent_end] >= '0' &&
                source_[exponent_end] <= '9') {
                floating = true;
                has_exponent = true;
                end = exponent_end + 1;
                while (end < source_.size() &&
                       source_[end] >= '0' && source_[end] <= '9') {
                    ++end;
                }
            }
        }

        if (end < source_.size() &&
            (source_[end] == '.' ||
             (has_exponent &&
              (source_[end] == 'e' || source_[end] == 'E')))) {
            fail("invalid numeric literal");
        }

        auto literal = source_.substr(
            literal_position, end - literal_position);
        if (floating ||
            (!literal.empty() &&
             (literal.front() == '+' || literal.front() == '-'))) {
            fail("invalid numeric literal");
        }

        std::int64_t value = 0;
        auto const [parsed_end, error] = std::from_chars(
            literal.data(), literal.data() + literal.size(), value);
        if (error == std::errc::result_out_of_range) {
            fail("integer literal is out of range");
        }
        if (error != std::errc{} ||
            parsed_end != literal.data() + literal.size()) {
            fail("invalid numeric literal");
        }
        position_ = end;
        return quote(value);
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

    [[nodiscard]] std::optional<std::pair<std::string_view, std::size_t>>
    parse_versioned_basis_name(
        std::string_view token,
        std::size_t token_position) const {
        auto const separator = token.rfind('@');
        if (separator == std::string_view::npos ||
            separator == 0 || separator + 1 == token.size()) {
            return std::nullopt;
        }

        std::size_t version = 0;
        for (auto index = separator + 1;
             index < token.size(); ++index) {
            auto const character = token[index];
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            auto const digit = static_cast<std::size_t>(
                character - '0');
            if (version >
                (std::numeric_limits<std::size_t>::max() - digit) /
                    10) {
                throw parse_error(
                    token_position + separator + 1,
                    "basis version is out of range");
            }
            version = version * 10 + digit;
        }
        return std::pair{
            token.substr(0, separator), version};
    }

    [[nodiscard]] quoted_expression parser_basis_reference(
        registered_parser_basis_ptr const& basis) const {
        if (basis->predefined()) {
            return basis->expression();
        }
        if (snapshot_enabled_) {
            return make_parser_basis_reference(basis);
        }
        auto const binding = registered_live_bindings_.find(
            basis->name());
        if (binding == registered_live_bindings_.end()) {
            throw std::logic_error(
                "combdsl::user parser basis has no live binding");
        }
        return make_parser_basis_reference(
            basis, binding->second);
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_exact_named_basis_token() {
        auto const token = current_basis_token();
        if (auto versioned =
                parse_versioned_basis_name(token, position_)) {
            auto const match = registered_versions_.find(
                versioned->first);
            if (match == registered_versions_.end() ||
                versioned->second == 0 ||
                versioned->second > match->second.size()) {
                if (has_valid_unseparated_versioned_basis_suffix(
                        token)) {
                    return std::nullopt;
                }
                fail("unknown operand");
            }
            position_ += token.size();
            return make_parser_basis_reference(
                match->second[versioned->second - 1]);
        }
        if (auto const match = registered_bases_.find(token);
            match != registered_bases_.end()) {
            position_ += token.size();
            return parser_basis_reference(match->second);
        }

        return std::nullopt;
    }

    [[nodiscard]] bool
    has_valid_unseparated_versioned_basis_suffix(
        std::string_view token) const {
        for (std::size_t suffix_position = 1;
             suffix_position < token.size();
             ++suffix_position) {
            auto const suffix = token.substr(suffix_position);
            auto const versioned = parse_versioned_basis_name(
                suffix, position_ + suffix_position);
            if (!versioned ||
                (!ends_with_non_alphanumeric(versioned->first) &&
                 !ends_with_ascii_digit(versioned->first))) {
                continue;
            }

            auto const match = registered_versions_.find(
                versioned->first);
            if (match != registered_versions_.end() &&
                versioned->second != 0 &&
                versioned->second <= match->second.size()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<quoted_expression>
    parse_unseparated_multicharacter_name() {
        auto const token = current_basis_token();
        if (token.size() <= 2) {
            return std::nullopt;
        }

        std::string_view recursive_name;
        if (recursive_function_) {
            recursive_name =
                quoted_access::root(*recursive_function_)->atomic_name();
        }

        auto const maximum_prefix_size = token.size() - 1;
        for (auto prefix_size = maximum_prefix_size;
             prefix_size > 1;
             --prefix_size) {
            auto const prefix = token.substr(0, prefix_size);
            if (auto versioned =
                    parse_versioned_basis_name(prefix, position_)) {
                auto const match = registered_versions_.find(
                    versioned->first);
                if (match != registered_versions_.end() &&
                    versioned->second != 0 &&
                    versioned->second <= match->second.size()) {
                    position_ += prefix_size;
                    return make_parser_basis_reference(
                        match->second[versioned->second - 1]);
                }
                continue;
            }
            if (prefix_size > 15) {
                continue;
            }
            if (ends_with_lowercase_ascii_letter(prefix) &&
                !begins_with_basis_requiring_no_leading_separator(
                    token.substr(prefix_size))) {
                continue;
            }

            if (recursive_function_ && prefix == recursive_name) {
                position_ += prefix_size;
                return *recursive_function_;
            }

            if (auto const match = registered_bases_.find(prefix);
                match != registered_bases_.end()) {
                position_ += prefix_size;
                return parser_basis_reference(match->second);
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] bool
    begins_with_basis_requiring_no_leading_separator(
        std::string_view token) const noexcept {
        for (auto const& [name, basis] : registered_bases_) {
            static_cast<void>(basis);
            if ((ends_with_non_alphanumeric(name) ||
                 ends_with_ascii_digit(name)) &&
                token.starts_with(name)) {
                return true;
            }
        }

        if (recursive_function_) {
            auto const name =
                quoted_access::root(*recursive_function_)->atomic_name();
            if ((ends_with_non_alphanumeric(name) ||
                 ends_with_ascii_digit(name)) &&
                token.starts_with(name)) {
                return true;
            }
        }

        for (auto const& [name, versions] : registered_versions_) {
            if ((!ends_with_non_alphanumeric(name) &&
                 !ends_with_ascii_digit(name)) ||
                !token.starts_with(name) ||
                token.size() <= name.size() + 1 ||
                token[name.size()] != '@') {
                continue;
            }

            std::size_t version = 0;
            auto position = name.size() + 1;
            for (; position < token.size(); ++position) {
                auto const character = token[position];
                if (character < '0' || character > '9') {
                    break;
                }
                auto const digit = static_cast<std::size_t>(
                    character - '0');
                if (version >
                    (std::numeric_limits<std::size_t>::max() - digit) /
                        10) {
                    version = 0;
                    break;
                }
                version = version * 10 + digit;
            }
            if (position > name.size() + 1 &&
                version != 0 && version <= versions.size()) {
                return true;
            }
        }
        return false;
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
        return parser_basis_reference(match->second);
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
    parser_basis_version_history registered_versions_;
    parser_live_binding_table registered_live_bindings_;
    std::optional<quoted_expression> recursive_function_;
    parser_definition_mode definition_mode_;
    bool snapshot_enabled_ = true;
    bool is_show_all_ = false;
    bool is_find_no_match_ = false;
    std::string replaced_definition_;
};

[[nodiscard]] inline parsed_input parse_input(
    std::string_view source,
    parser_definition_mode definition_mode =
        parser_definition_mode::register_definitions) {
    auto is_whitespace = [](char value) noexcept {
        return value == ' ' || value == '\t' || value == '\n' ||
               value == '\r' || value == '\f' || value == '\v';
    };
    auto first = std::size_t{0};
    while (first < source.size() && is_whitespace(source[first])) {
        ++first;
    }
    auto const remaining = source.substr(first);
    auto begins_command = [&](std::string_view keyword) noexcept {
        return remaining.starts_with(keyword) &&
               (remaining.size() == keyword.size() ||
                (remaining.size() > keyword.size() &&
                 is_whitespace(remaining[keyword.size()])));
    };
    auto const is_definition =
        begins_command("set") || begins_command("define") ||
        begins_command("remove") || begins_command("references") ||
        begins_command("snapshot");
    auto const requires_registry_transaction =
        is_definition || begins_command("compare");

    if (requires_registry_transaction) {
        std::lock_guard transaction_lock(
            parser_definition_transaction_mutex());
        return quoted_expression_parser(
            source, definition_mode).parse_input();
    }
    return quoted_expression_parser(
        source, definition_mode).parse_input();
}

} // namespace detail

[[nodiscard]] inline quoted_expression parse(std::string_view source) {
    return std::move(detail::parse_input(source).expression);
}

inline evaluation_outcome parse_eval_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback,
    evaluation_interrupt_callback const& interrupt_callback) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return evaluation_outcome::completed;
    }
    return eval_with_outcome(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        step_limit_callback,
        interrupt_callback);
}

inline evaluation_outcome parse_eval_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback) {
    return parse_eval_with_outcome(
        source,
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        step_limit_callback,
        evaluation_interrupt_callback{});
}

inline evaluation_outcome parse_eval_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit) {
    return parse_eval_with_outcome(
        source,
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        evaluation_step_limit_callback{});
}

inline evaluation_outcome parse_eval_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    return parse_eval_with_outcome(
        source,
        output,
        input,
        basis_step,
        progress_callback,
        std::nullopt);
}

inline void parse_eval(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit) {
    static_cast<void>(parse_eval_with_outcome(
        source,
        output,
        input,
        basis_step,
        progress_callback,
        step_limit));
}

inline void parse_eval(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    evaluation_progress_callback const& progress_callback) {
    parse_eval(
        source,
        output,
        input,
        basis_step,
        progress_callback,
        std::nullopt);
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

inline evaluation_outcome parse_and_step_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback,
    evaluation_interrupt_callback const& interrupt_callback) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return evaluation_outcome::completed;
    }
    return single_step_run_with_outcome(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        evaluation_progress_callback{},
        step_limit,
        step_limit_callback,
        interrupt_callback);
}

inline evaluation_outcome parse_and_step_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    evaluation_step_limit_callback const& step_limit_callback) {
    return parse_and_step_with_outcome(
        source,
        output,
        input,
        basis_step,
        step_limit,
        step_limit_callback,
        evaluation_interrupt_callback{});
}

inline evaluation_outcome parse_and_step_with_outcome(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    std::optional<std::size_t> step_limit) {
    return parse_and_step_with_outcome(
        source,
        output,
        input,
        basis_step,
        step_limit,
        evaluation_step_limit_callback{});
}

inline evaluation_outcome parse_and_step_with_outcome(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    return parse_and_step_with_outcome(
        source, output, input, basis_step, std::nullopt);
}

inline void parse_and_step(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    static_cast<void>(parse_and_step_with_outcome(
        source, output, input, basis_step));
}

inline void parse_and_key_step(
    std::string_view source,
    std::ostream& output = std::cout,
    std::istream& input = std::cin,
    bool basis_step = false) {
    auto parsed = detail::parse_input(source);
    if (parsed.is_display_only) {
        parsed.expression.print_to(output);
        detail::print_layout(output, "\n");
        output.flush();
        return;
    }
    if (parsed.is_definition) {
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
BASIS(U, 2, B(O)(M));
BASIS(N, 2, S(S)(K));
BASIS(R, 3, S(K(S(S)))(S(K(K))(K)));
BASIS(C, 3, S(S(K(B))(S))(K(K)));
inline const auto C_star = ::combdsl::basis("C*", 4, B(C));
inline const auto C_star_star =
    ::combdsl::basis("C**", 5, B(C_star));
inline const auto W_star = ::combdsl::basis("W*", 3, B(W));
inline const auto W_star_star =
    ::combdsl::basis("W**", 4, B(W_star));
BASIS(Q, 3, S(K(S(B)))(K));
BASIS(Q1, 3, B(C)(B));
BASIS(Q3, 3, B(T));
BASIS(V, 3, S(S(K(S))(S(K(K))(S(K(S))(T))))(K(K)));
BASIS(D, 4, S(K(S(K(S))))(S(K(K))));
BASIS(L, 2, S(B)(K(M)));
BASIS(W1, 2, C(W));
BASIS(Z, 2, S(B)(I));
BASIS(A, 2, S(B)(T));
BASIS(E, 5, B(D)(D));
BASIS(F, 3, C(V));
BASIS(G, 4, D(C));
BASIS(H, 3, S(R));
BASIS(J, 4, C_star_star(H(E)));

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

namespace detail {

[[nodiscard]] inline std::size_t count_contained_quoted_atoms(
    std::span<quoted_atomic const> atoms,
    quoted_expression const& expression) {
    return static_cast<std::size_t>(std::count_if(
        atoms.begin(),
        atoms.end(),
        [&](quoted_atomic const& atom) {
            return contains_quoted_atom(
                atom.expression(), expression);
        }));
}

[[nodiscard]] inline bool contains_next_pending_atom(
    std::span<quoted_atomic const> pending_atoms,
    quoted_expression const& expression) {
    return !pending_atoms.empty() &&
           contains_quoted_atom(
               pending_atoms.back().expression(),
               expression);
}

[[nodiscard]] inline quoted_expression
takeout_impl(
    quoted_atomic const& qa,
    quoted_expression qe,
    std::span<quoted_atomic const> pending_atoms) {
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
            return quote(O)(takeout_impl(
                qa, qarg, pending_atoms));
        }
        if (qarg_is_qa && qfun_contains_qa) {
            return quote(W)(takeout_impl(
                qa, qfun, pending_atoms));
        }
        if (qfun_contains_qa && !qarg_contains_qa) {
            auto t = takeout_impl(
                qa, qfun, pending_atoms);
            auto const qarg_contains_next =
                contains_next_pending_atom(
                    pending_atoms, qarg);
            auto const t_contains_next =
                contains_next_pending_atom(
                    pending_atoms, t);
            auto const use_cardinal =
                qarg_contains_next != t_contains_next
                    ? qarg_contains_next
                    : count_contained_quoted_atoms(
                          pending_atoms, qarg) >=
                          count_contained_quoted_atoms(
                              pending_atoms, t);
            if (use_cardinal) {
                return quote(C)(
                    std::move(t))(qarg);
            }
            return quote(R)(
                qarg)(std::move(t));
        }
        if (qarg_contains_qa && !qfun_contains_qa) {
            auto t = takeout_impl(
                qa, qarg, pending_atoms);
            auto const qfun_contains_next =
                contains_next_pending_atom(
                    pending_atoms, qfun);
            auto const t_contains_next =
                contains_next_pending_atom(
                    pending_atoms, t);
            auto const use_queer =
                qfun_contains_next != t_contains_next
                    ? qfun_contains_next
                    : count_contained_quoted_atoms(
                          pending_atoms, qfun) >
                          count_contained_quoted_atoms(
                              pending_atoms, t);
            if (use_queer) {
                return quote(Q)(
                    std::move(t))(qfun);
            }
            return quote(B)(
                qfun)(std::move(t));
        }
        if (qfun_contains_qa && qarg_contains_qa) {
            return quote(S)(
                takeout_impl(
                    qa, qfun, pending_atoms))(
                takeout_impl(
                    qa, qarg, pending_atoms));
        }
    }

    throw std::logic_error(
        "combdsl::takeout has no matching case");
}

[[nodiscard]] inline takeout_ministep_result
make_completed_takeout_ministep(quoted_expression result) {
    std::vector<quoted_expression> stages;
    stages.push_back(result);
    return {std::move(result), std::move(stages)};
}

class quoted_takeout_ministep_node final : public quoted_node {
public:
    explicit quoted_takeout_ministep_node(std::string text)
        : text_(std::move(text)) {}

    [[nodiscard]] quoted_node_kind kind() const noexcept override {
        return quoted_node_kind::opaque;
    }

    void print_to(std::ostream& output) const override {
        print_layout(output, text_);
    }

private:
    std::string text_;
};

[[nodiscard]] inline quoted_expression
make_takeout_ministep_placeholder(
    quoted_atomic const& qa,
    quoted_expression const& expression) {
    std::ostringstream output;
    output << "[takeout ";
    qa.expression().print_to(output);
    output << " from ";
    expression.print_to(output);
    output << ']';
    return quoted_access::make(
        std::make_shared<quoted_takeout_ministep_node>(
            std::move(output).str()));
}

template <class Wrap>
[[nodiscard]] inline takeout_ministep_result
wrap_takeout_ministeps(
    quoted_atomic const& qa,
    quoted_expression const& child_source,
    takeout_ministep_result child,
    Wrap&& wrap) {
    std::vector<quoted_expression> stages;
    stages.reserve(child.stages.size() + 1);
    stages.push_back(std::invoke(
        wrap,
        make_takeout_ministep_placeholder(qa, child_source)));
    for (auto const& child_stage : child.stages) {
        stages.push_back(std::invoke(wrap, child_stage));
    }
    auto result = stages.back();
    return {std::move(result), std::move(stages)};
}

template <class Wrap>
[[nodiscard]] inline takeout_ministep_result
wrap_two_takeout_ministeps(
    quoted_atomic const& qa,
    quoted_expression const& function_source,
    takeout_ministep_result function,
    quoted_expression const& argument_source,
    takeout_ministep_result argument,
    Wrap&& wrap) {
    auto const function_placeholder =
        make_takeout_ministep_placeholder(qa, function_source);
    auto const argument_placeholder =
        make_takeout_ministep_placeholder(qa, argument_source);

    std::vector<quoted_expression> stages;
    stages.reserve(
        function.stages.size() + argument.stages.size() + 1);
    stages.push_back(std::invoke(
        wrap, function_placeholder, argument_placeholder));
    for (auto const& function_stage : function.stages) {
        stages.push_back(std::invoke(
            wrap, function_stage, argument_placeholder));
    }
    for (auto const& argument_stage : argument.stages) {
        stages.push_back(std::invoke(
            wrap, function.result, argument_stage));
    }
    auto result = stages.back();
    return {std::move(result), std::move(stages)};
}

[[nodiscard]] inline takeout_ministep_result
takeout_with_pending_atoms_ministeps(
    quoted_atomic const& qa,
    quoted_expression qe,
    std::span<quoted_atomic const> pending_atoms) {
    if (detail::same_quoted_atom(qa.expression(), qe)) {
        return make_completed_takeout_ministep(
            detail::make_quoted_primitive(
                detail::quoted_node_kind::identity));
    }

    if (!detail::contains_quoted_atom(qa.expression(), qe)) {
        return make_completed_takeout_ministep(
            detail::make_quoted_application(
                detail::make_quoted_primitive(
                    detail::quoted_node_kind::constant),
                std::move(qe)));
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
            return make_completed_takeout_ministep(quote(M));
        }
        if (qfun_is_qa && !qarg_contains_qa) {
            return make_completed_takeout_ministep(quote(T)(qarg));
        }
        if (qarg_is_qa && !qfun_contains_qa) {
            return make_completed_takeout_ministep(qfun);
        }
        if (qfun_is_qa && qarg_contains_qa) {
            return wrap_takeout_ministeps(
                qa,
                qarg,
                takeout_with_pending_atoms_ministeps(
                    qa, qarg, pending_atoms),
                [](quoted_expression child) {
                    return quote(O)(std::move(child));
                });
        }
        if (qarg_is_qa && qfun_contains_qa) {
            return wrap_takeout_ministeps(
                qa,
                qfun,
                takeout_with_pending_atoms_ministeps(
                    qa, qfun, pending_atoms),
                [](quoted_expression child) {
                    return quote(W)(std::move(child));
                });
        }
        if (qfun_contains_qa && !qarg_contains_qa) {
            auto t = takeout_with_pending_atoms_ministeps(
                qa, qfun, pending_atoms);
            auto const qarg_contains_next =
                contains_next_pending_atom(
                    pending_atoms, qarg);
            auto const t_contains_next =
                contains_next_pending_atom(
                    pending_atoms, t.result);
            auto const use_cardinal =
                qarg_contains_next != t_contains_next
                    ? qarg_contains_next
                    : count_contained_quoted_atoms(
                          pending_atoms, qarg) >=
                          count_contained_quoted_atoms(
                              pending_atoms, t.result);
            return wrap_takeout_ministeps(
                qa,
                qfun,
                std::move(t),
                [&](quoted_expression child) {
                    if (use_cardinal) {
                        return quote(C)(
                            std::move(child))(qarg);
                    }
                    return quote(R)(
                        qarg)(std::move(child));
                });
        }
        if (qarg_contains_qa && !qfun_contains_qa) {
            auto t = takeout_with_pending_atoms_ministeps(
                qa, qarg, pending_atoms);
            auto const qfun_contains_next =
                contains_next_pending_atom(
                    pending_atoms, qfun);
            auto const t_contains_next =
                contains_next_pending_atom(
                    pending_atoms, t.result);
            auto const use_queer =
                qfun_contains_next != t_contains_next
                    ? qfun_contains_next
                    : count_contained_quoted_atoms(
                          pending_atoms, qfun) >
                          count_contained_quoted_atoms(
                              pending_atoms, t.result);
            return wrap_takeout_ministeps(
                qa,
                qarg,
                std::move(t),
                [&](quoted_expression child) {
                    if (use_queer) {
                        return quote(Q)(
                            std::move(child))(qfun);
                    }
                    return quote(B)(
                        qfun)(std::move(child));
                });
        }
        if (qfun_contains_qa && qarg_contains_qa) {
            return wrap_two_takeout_ministeps(
                qa,
                qfun,
                takeout_with_pending_atoms_ministeps(
                    qa, qfun, pending_atoms),
                qarg,
                takeout_with_pending_atoms_ministeps(
                    qa, qarg, pending_atoms),
                [](quoted_expression function,
                   quoted_expression argument) {
                    return quote(S)(
                        std::move(function))(
                        std::move(argument));
                });
        }
    }

    throw std::logic_error(
        "combdsl::takeout has no matching case");
}

[[nodiscard]] inline quoted_expression
takeout_with_pending_atoms(
    quoted_atomic const& qa,
    quoted_expression qe,
    std::span<quoted_atomic const> pending_atoms) {
    return takeout_impl(
        qa, std::move(qe), pending_atoms);
}

} // namespace detail

[[nodiscard]] inline quoted_expression
takeout(quoted_atomic qa, quoted_expression qe) {
    return detail::takeout_impl(
        qa,
        std::move(qe),
        std::span<quoted_atomic const>{});
}

inline constexpr std::size_t search_for_xy_subexp_candidate_count =
    129'958;
inline constexpr std::size_t search_for_xyz_subexp_candidate_count =
    3'137'844;
inline constexpr std::size_t search_for_subexp_candidate_count =
    search_for_xy_subexp_candidate_count +
    search_for_xyz_subexp_candidate_count;
inline constexpr std::size_t check_for_match_reduction_limit = 256;
inline constexpr std::size_t check_for_match_combinator_count = 30;
inline constexpr std::size_t check_for_match_excluded_pair_count = 42;
inline constexpr std::size_t check_for_pairs_match_candidate_count =
    check_for_match_combinator_count *
        check_for_match_combinator_count -
    check_for_match_excluded_pair_count;
inline constexpr std::size_t
    check_for_match_left_trip_candidate_count =
        check_for_match_combinator_count *
            check_for_pairs_match_candidate_count -
        check_for_match_combinator_count *
            check_for_match_combinator_count -
        check_for_match_combinator_count;
inline constexpr std::size_t
    check_for_match_right_trip_candidate_count =
        (check_for_match_combinator_count - 1) *
        check_for_pairs_match_candidate_count;
inline constexpr std::size_t check_for_trips_match_candidate_count =
    check_for_match_left_trip_candidate_count +
    check_for_match_right_trip_candidate_count;
inline constexpr std::size_t check_for_trips_match_shape_count = 2;
inline constexpr std::size_t check_for_trips_match_column_count =
    check_for_trips_match_shape_count *
    check_for_match_combinator_count *
    check_for_match_combinator_count;
inline constexpr std::size_t check_for_quads_match_shape_count = 5;
inline constexpr std::size_t check_for_quads_match_tuple_count =
    check_for_match_combinator_count *
    check_for_match_combinator_count *
    check_for_match_combinator_count *
    check_for_match_combinator_count;
inline constexpr std::size_t check_for_quads_match_candidate_count =
    check_for_match_combinator_count *
        check_for_match_left_trip_candidate_count +
    check_for_pairs_match_candidate_count *
        check_for_pairs_match_candidate_count +
    (check_for_match_combinator_count - 1) *
        check_for_match_combinator_count *
        check_for_pairs_match_candidate_count +
    (check_for_match_combinator_count - 1) *
        check_for_match_left_trip_candidate_count +
    (check_for_match_combinator_count - 1) *
        (check_for_match_combinator_count - 1) *
        check_for_pairs_match_candidate_count;
inline constexpr std::size_t check_for_quads_match_column_count =
    check_for_quads_match_shape_count *
    check_for_match_combinator_count *
    check_for_match_combinator_count *
    check_for_match_combinator_count;

struct subexpression_search_match {
    quoted_expression source_expression;
    quoted_expression takeout_result;
    std::size_t examined_expression_count;
};

namespace detail {

struct symbol_application_shape {
    std::uint16_t application_bits;
    std::uint8_t node_count;
};

[[nodiscard]] inline auto const&
symbol_application_shapes() {
    static auto const shapes = [] {
        std::array<
            std::vector<symbol_application_shape>,
            9> result;
        result[1].push_back({0, 1});

        for (std::size_t leaf_count = 2;
             leaf_count <= 8;
             ++leaf_count) {
            for (std::size_t left_leaf_count = 1;
                 left_leaf_count < leaf_count;
                 ++left_leaf_count) {
                auto const right_leaf_count =
                    leaf_count - left_leaf_count;
                for (auto const& left :
                     result[left_leaf_count]) {
                    for (auto const& right :
                         result[right_leaf_count]) {
                        auto const right_shift =
                            1U + left.node_count;
                        result[leaf_count].push_back({
                            static_cast<std::uint16_t>(
                                1U |
                                (static_cast<unsigned>(
                                     left.application_bits)
                                 << 1U) |
                                (static_cast<unsigned>(
                                     right.application_bits)
                                 << right_shift)),
                            static_cast<std::uint8_t>(
                                1U + left.node_count +
                                right.node_count),
                        });
                    }
                }
            }
        }

        return result;
    }();
    return shapes;
}

template <std::size_t SymbolCount>
[[nodiscard]] inline quoted_expression
make_symbol_application_tree(
    symbol_application_shape shape,
    std::size_t leaf_count,
    std::size_t labeling,
    std::array<
        quoted_expression,
        SymbolCount> const& symbols) {
    static_assert(SymbolCount != 0);
    std::array<std::uint8_t, 8> labels{};
    for (auto position = leaf_count;
         position > 0;
         --position) {
        labels[position - 1] =
            static_cast<std::uint8_t>(
                labeling % SymbolCount);
        labeling /= SymbolCount;
    }

    std::size_t node_position = 0;
    std::size_t leaf_position = 0;
    auto build = [&](auto const& self) -> quoted_expression {
        auto const is_application =
            (shape.application_bits &
             (1U << node_position)) != 0;
        ++node_position;
        if (!is_application) {
            return symbols[labels[leaf_position++]];
        }

        auto function = self(self);
        auto argument = self(self);
        return make_quoted_application(
            std::move(function),
            std::move(argument));
    };

    return build(build);
}

[[nodiscard]] inline bool
possibly_same_quoted_expression(
    quoted_expression const& left,
    quoted_expression const& right) {
    auto const& left_root = quoted_access::root(left);
    auto const& right_root = quoted_access::root(right);
    if (left_root == right_root) {
        return true;
    }
    if (left_root->kind() != right_root->kind()) {
        return false;
    }

    auto const left_atomic = left_root->atomic_kind();
    auto const right_atomic = right_root->atomic_kind();
    if (left_atomic != quoted_atomic_kind::none ||
        right_atomic != quoted_atomic_kind::none) {
        return left_atomic != quoted_atomic_kind::none &&
               left_atomic == right_atomic &&
               left_root->atomic_name() ==
                   right_root->atomic_name();
    }

    switch (left_root->kind()) {
    case quoted_node_kind::identity:
    case quoted_node_kind::constant:
    case quoted_node_kind::substitution:
    case quoted_node_kind::fixed_point:
        return true;
    case quoted_node_kind::application: {
        auto const& left_application =
            static_cast<quoted_application_node const&>(
                *left_root);
        auto const& right_application =
            static_cast<quoted_application_node const&>(
                *right_root);
        return possibly_same_quoted_expression(
                   left_application.function(),
                   right_application.function()) &&
               possibly_same_quoted_expression(
                   left_application.argument(),
                   right_application.argument());
    }
    case quoted_node_kind::pending_sk:
        return possibly_same_quoted_expression(
            static_cast<quoted_pending_sk_node const&>(
                *left_root).application(),
            static_cast<quoted_pending_sk_node const&>(
                *right_root).application());
    case quoted_node_kind::recursive_y:
        return possibly_same_quoted_expression(
            static_cast<quoted_recursive_y_node const&>(
                *left_root).generator(),
            static_cast<quoted_recursive_y_node const&>(
                *right_root).generator());
    case quoted_node_kind::basis_argument:
        return possibly_same_quoted_expression(
            static_cast<quoted_basis_argument_node const&>(
                *left_root).argument(),
            static_cast<quoted_basis_argument_node const&>(
                *right_root).argument());
    case quoted_node_kind::basis: {
        auto const& left_basis =
            static_cast<quoted_basis_node_base const&>(
                *left_root);
        auto const& right_basis =
            static_cast<quoted_basis_node_base const&>(
                *right_root);
        return left_basis.name() == right_basis.name() &&
               left_basis.arity() == right_basis.arity();
    }
    case quoted_node_kind::opaque:
    case quoted_node_kind::rec_func:
    case quoted_node_kind::colored_argument:
        return false;
    }
    return false;
}

[[nodiscard]] inline bool
contains_quoted_subexpression(
    quoted_expression const& expression,
    quoted_expression const& possible_container) {
    std::vector<quoted_expression> pending{
        possible_container,
    };

    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (possibly_same_quoted_expression(
                expression, current) &&
            same_parser_definition_expression(
                expression, current)) {
            return true;
        }

        auto const& root = quoted_access::root(current);
        switch (root->kind()) {
        case quoted_node_kind::application: {
            auto const& application =
                static_cast<quoted_application_node const&>(
                    *root);
            pending.push_back(application.argument());
            pending.push_back(application.function());
            break;
        }
        case quoted_node_kind::pending_sk:
            pending.push_back(
                static_cast<quoted_pending_sk_node const&>(
                    *root).application());
            break;
        case quoted_node_kind::recursive_y:
            pending.push_back(
                static_cast<quoted_recursive_y_node const&>(
                    *root).generator());
            break;
        case quoted_node_kind::basis_argument:
            pending.push_back(
                static_cast<quoted_basis_argument_node const&>(
                    *root).argument());
            break;
        case quoted_node_kind::colored_argument:
            pending.push_back(
                static_cast<quoted_colored_argument_node const&>(
                    *root).argument());
            break;
        default:
            break;
        }
    }

    return false;
}

template <std::size_t SymbolCount, std::size_t TakeoutCount>
[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_symbol_subexpression(
    quoted_expression const& expression,
    std::array<
        quoted_expression,
        SymbolCount> const& symbols,
    std::array<
        quoted_atomic,
        TakeoutCount> const& takeout_order) {
    static_assert(SymbolCount != 0);
    std::size_t examined_expression_count = 0;
    auto const& shapes = symbol_application_shapes();

    for (std::size_t leaf_count = 1;
         leaf_count <= 8;
         ++leaf_count) {
        std::size_t labeling_count = 1;
        for (std::size_t index = 0;
             index < leaf_count;
             ++index) {
            labeling_count *= SymbolCount;
        }

        for (auto const shape : shapes[leaf_count]) {
            for (std::size_t labeling = 0;
                 labeling < labeling_count;
                 ++labeling) {
                auto source_expression =
                    make_symbol_application_tree(
                        shape,
                        leaf_count,
                        labeling,
                        symbols);
                ++examined_expression_count;
                auto result = source_expression;
                for (std::size_t remaining_takeouts =
                         TakeoutCount;
                     remaining_takeouts != 0;
                     --remaining_takeouts) {
                    auto const takeout_index =
                        remaining_takeouts - 1;
                    auto const pending_atoms =
                        std::span<quoted_atomic const>{
                            takeout_order}.first(
                                takeout_index);
                    result = takeout_with_pending_atoms(
                        takeout_order[takeout_index],
                        std::move(result),
                        pending_atoms);
                }
                if (contains_quoted_subexpression(
                        expression, result)) {
                    return subexpression_search_match{
                        std::move(source_expression),
                        std::move(result),
                        examined_expression_count,
                    };
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace detail

[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_xy_subexp(quoted_expression const& expression) {
    static std::array<quoted_expression, 2> const symbols{
        quote(x),
        quote(y),
    };
    static std::array<quoted_atomic, 2> const takeout_order{
        quoted_atomic{x},
        quoted_atomic{y},
    };
    return detail::search_for_symbol_subexpression(
        expression, symbols, takeout_order);
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_xy_subexp(Expression&& expression) {
    return search_for_xy_subexp(
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_xyz_subexp(quoted_expression const& expression) {
    static std::array<quoted_expression, 3> const symbols{
        quote(x),
        quote(y),
        quote(z),
    };
    static std::array<quoted_atomic, 3> const takeout_order{
        quoted_atomic{x},
        quoted_atomic{y},
        quoted_atomic{z},
    };
    return detail::search_for_symbol_subexpression(
        expression, symbols, takeout_order);
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_xyz_subexp(Expression&& expression) {
    return search_for_xyz_subexp(
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_subexp(quoted_expression const& expression) {
    if (auto result =
            search_for_xy_subexp(expression)) {
        return result;
    }

    auto result = search_for_xyz_subexp(expression);
    if (result) {
        result->examined_expression_count +=
            search_for_xy_subexp_candidate_count;
    }
    return result;
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::optional<subexpression_search_match>
search_for_subexp(Expression&& expression) {
    return search_for_subexp(
        quote(std::forward<Expression>(expression)));
}

namespace detail {

inline constexpr std::size_t
    combinator_match_expression_key_size_limit = 65'536;

[[nodiscard]] inline std::optional<quoted_expression>
normalize_for_combinator_match(quoted_expression expression) {
    std::unordered_set<std::string> seen;
    auto initial_key = quoted_expression_key(expression);
    if (initial_key.size() >
        combinator_match_expression_key_size_limit) {
        return std::nullopt;
    }
    seen.emplace(std::move(initial_key));
    std::size_t remaining_steps =
        check_for_match_reduction_limit;

    for (;;) {
        auto reduced = reduce_next_redex(
            expression,
            reduction_options{
                .basis_step = false,
                .reduce_partial_k_argument = false,
            });
        if (!reduced) {
            return expression;
        }
        if (remaining_steps == 0) {
            return std::nullopt;
        }
        --remaining_steps;

        expression = std::move(*reduced);
        auto key = quoted_expression_key(expression);
        if (key.size() >
            combinator_match_expression_key_size_limit ||
            !seen.emplace(std::move(key)).second) {
            return std::nullopt;
        }
    }
}

struct timed_find_normalization_result {
    std::optional<quoted_expression> expression;
    bool timed_out = false;
};

[[nodiscard]] inline timed_find_normalization_result
normalize_for_combinator_match_until(
    quoted_expression expression,
    find_clock::time_point deadline) {
    if (find_clock_now() >= deadline) {
        return {{}, true};
    }

    std::unordered_set<std::string> seen;
    auto initial_key = quoted_expression_key(expression);
    if (find_clock_now() >= deadline) {
        return {{}, true};
    }
    if (initial_key.size() >
        combinator_match_expression_key_size_limit) {
        return {};
    }
    seen.emplace(std::move(initial_key));
    std::size_t remaining_steps =
        check_for_match_reduction_limit;

    for (;;) {
        if (find_clock_now() >= deadline) {
            return {{}, true};
        }
        auto reduced = reduce_next_redex(
            expression,
            reduction_options{
                .basis_step = true,
                .reduce_partial_k_argument = false,
            });
        if (find_clock_now() >= deadline) {
            return {{}, true};
        }
        if (!reduced) {
            return {std::move(expression), false};
        }
        if (remaining_steps == 0) {
            return {};
        }
        --remaining_steps;

        expression = std::move(*reduced);
        auto key = quoted_expression_key(expression);
        if (find_clock_now() >= deadline) {
            return {{}, true};
        }
        if (key.size() >
                combinator_match_expression_key_size_limit ||
            !seen.emplace(std::move(key)).second) {
            return {};
        }
    }
}

[[nodiscard]] inline auto const&
predefined_bird_combinators() {
    static std::array<
        quoted_expression,
        check_for_match_combinator_count> const combinators{
        quote(A),
        quote(B),
        quote(C),
        quote(C_star),
        quote(C_star_star),
        quote(D),
        quote(E),
        quote(F),
        quote(G),
        quote(H),
        quote(I),
        quote(J),
        quote(K),
        quote(L),
        quote(M),
        quote(N),
        quote(O),
        quote(Q),
        quote(Q1),
        quote(Q3),
        quote(R),
        quote(S),
        quote(T),
        quote(U),
        quote(V),
        quote(W),
        quote(W_star),
        quote(W_star_star),
        quote(W1),
        quote(Z),
    };
    return combinators;
}

[[nodiscard]] inline bool is_identity_combinator(
    quoted_expression const& expression) noexcept {
    return quoted_access::root(expression)->kind() ==
           quoted_node_kind::identity;
}

[[nodiscard]] inline bool is_excluded_match_pair(
    quoted_expression const& function,
    quoted_expression const& argument) noexcept {
    auto combinator_name =
        [](quoted_expression const& expression) noexcept
            -> std::string_view {
            auto const& root = quoted_access::root(expression);
            switch (root->kind()) {
            case quoted_node_kind::identity:
                return "I";
            case quoted_node_kind::constant:
                return "K";
            case quoted_node_kind::basis:
                return static_cast<quoted_basis_node_base const&>(*root)
                    .definition_name();
            default:
                return {};
            }
        };
    auto const function_name = combinator_name(function);
    auto const argument_name = combinator_name(argument);
    if (function_name == "I") {
        return true;
    }
    if ((function_name == "M" || function_name == "U") &&
        (argument_name == "M" || argument_name == "U")) {
        return true;
    }
    static constexpr std::array identity_pairs{
        std::pair<std::string_view, std::string_view>{"B", "I"},
        std::pair<std::string_view, std::string_view>{"C", "T"},
        std::pair<std::string_view, std::string_view>{"M", "I"},
        std::pair<std::string_view, std::string_view>{"N", "K"},
        std::pair<std::string_view, std::string_view>{"Q", "I"},
        std::pair<std::string_view, std::string_view>{"W", "K"},
        std::pair<std::string_view, std::string_view>{"W*", "K"},
        std::pair<std::string_view, std::string_view>{"Z", "I"},
    };
    return std::ranges::find(
               identity_pairs,
               std::pair{function_name, argument_name}) !=
           identity_pairs.end();
}

[[nodiscard]] inline bool
is_allowed_predefined_bird_left_trip_head(
    quoted_expression const& first,
    quoted_expression const& second) noexcept {
    return quoted_access::root(first)->kind() !=
               quoted_node_kind::constant &&
           !(quoted_access::root(first)->kind() ==
                 quoted_node_kind::substitution &&
             quoted_access::root(second)->kind() ==
                 quoted_node_kind::constant);
}

[[nodiscard]] inline bool
is_allowed_predefined_bird_left_trip(
    quoted_expression const& first,
    quoted_expression const& second) noexcept {
    return !is_excluded_match_pair(first, second) &&
           is_allowed_predefined_bird_left_trip_head(
               first, second);
}

[[nodiscard]] inline bool
is_allowed_predefined_bird_right_trip(
    quoted_expression const& first,
    quoted_expression const& second,
    quoted_expression const& third) noexcept {
    return !is_identity_combinator(first) &&
           !is_excluded_match_pair(second, third);
}

template <class Visitor>
inline void for_each_predefined_bird_pair(
    Visitor&& visitor) {
    auto const& combinators =
        predefined_bird_combinators();
    for (auto const& function : combinators) {
        for (auto const& argument : combinators) {
            if (!is_excluded_match_pair(function, argument)) {
                visitor(function(argument));
            }
        }
    }
}

template <class Visitor>
inline void visit_predefined_bird_trip_shape(
    std::size_t shape_index,
    quoted_expression const& first,
    quoted_expression const& second,
    quoted_expression const& third,
    Visitor&& visitor) {
    switch (shape_index) {
    case 0:
        visitor(first(second)(third));
        break;
    case 1:
        visitor(first(second(third)));
        break;
    }
}

[[nodiscard]] inline std::uint8_t
predefined_bird_trip_shape_mask(
    quoted_expression const& first,
    quoted_expression const& second,
    quoted_expression const& third) noexcept {
    std::uint8_t result = 0;
    if (is_allowed_predefined_bird_left_trip(
            first, second)) {
        result |= std::uint8_t{1} << 0;
    }
    if (is_allowed_predefined_bird_right_trip(
            first, second, third)) {
        result |= std::uint8_t{1} << 1;
    }
    return result;
}

template <class Visitor>
inline void for_each_predefined_bird_trip(
    Visitor&& visitor) {
    auto const& combinators =
        predefined_bird_combinators();
    for (auto const& first : combinators) {
        for (auto const& second : combinators) {
            for (auto const& third : combinators) {
                auto const shape_mask =
                    predefined_bird_trip_shape_mask(
                        first, second, third);
                for (std::size_t shape_index = 0;
                     shape_index <
                         check_for_trips_match_shape_count;
                     ++shape_index) {
                    if ((shape_mask &
                         (std::uint8_t{1} << shape_index)) != 0) {
                        visit_predefined_bird_trip_shape(
                            shape_index,
                            first,
                            second,
                            third,
                            visitor);
                    }
                }
            }
        }
    }
}

template <class Visitor>
inline void for_each_predefined_bird_trip_column_at(
    std::size_t column_index,
    Visitor&& visitor) {
    auto const& combinators =
        predefined_bird_combinators();
    auto remainder = column_index;
    auto const shape_index =
        remainder % check_for_trips_match_shape_count;
    remainder /= check_for_trips_match_shape_count;
    auto const third_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const first_index =
        remainder % check_for_match_combinator_count;

    auto const& first = combinators[first_index];
    auto const& third = combinators[third_index];
    for (std::size_t second_index = 0;
         second_index < check_for_match_combinator_count;
         ++second_index) {
        auto const& second = combinators[second_index];
        if ((predefined_bird_trip_shape_mask(
                 first, second, third) &
             (std::uint8_t{1} << shape_index)) == 0) {
            continue;
        }
        auto const tuple_index =
            (first_index * check_for_match_combinator_count +
             second_index) *
                check_for_match_combinator_count +
            third_index;
        visit_predefined_bird_trip_shape(
            shape_index,
            first,
            second,
            third,
            [&](quoted_expression trip) {
                visitor(
                    tuple_index *
                            check_for_trips_match_shape_count +
                        shape_index,
                    std::move(trip));
            });
    }
}

[[nodiscard]] inline std::uint8_t
predefined_bird_quad_shape_mask(
    quoted_expression const& first,
    quoted_expression const& second,
    quoted_expression const& third,
    quoted_expression const& fourth) noexcept {
    auto const pair_ab =
        !is_excluded_match_pair(first, second);
    auto const pair_bc =
        !is_excluded_match_pair(second, third);
    auto const pair_cd =
        !is_excluded_match_pair(third, fourth);
    auto const first_is_identity =
        is_identity_combinator(first);
    auto const second_is_identity =
        is_identity_combinator(second);
    auto const left_trip_abc =
        pair_ab &&
        is_allowed_predefined_bird_left_trip_head(
            first, second);
    auto const right_trip_abc =
        !first_is_identity && pair_bc;
    auto const left_trip_bcd =
        pair_bc &&
        is_allowed_predefined_bird_left_trip_head(
            second, third);
    auto const right_trip_bcd =
        !second_is_identity && pair_cd;

    std::uint8_t result = 0;
    if (left_trip_abc) {
        result |= std::uint8_t{1} << 0;
    }
    if (pair_ab && pair_cd) {
        result |= std::uint8_t{1} << 1;
    }
    if (right_trip_abc) {
        result |= std::uint8_t{1} << 2;
    }
    if (left_trip_bcd && !first_is_identity) {
        result |= std::uint8_t{1} << 3;
    }
    if (right_trip_bcd && !first_is_identity) {
        result |= std::uint8_t{1} << 4;
    }
    return result;
}

template <class Visitor>
inline void visit_predefined_bird_quad_shape(
    std::size_t shape_index,
    quoted_expression const& first,
    quoted_expression const& second,
    quoted_expression const& third,
    quoted_expression const& fourth,
    Visitor&& visitor) {
    switch (shape_index) {
    case 0:
        visitor(first(second)(third)(fourth));
        break;
    case 1:
        visitor(first(second)(third(fourth)));
        break;
    case 2:
        visitor(first(second(third))(fourth));
        break;
    case 3:
        visitor(first(second(third)(fourth)));
        break;
    case 4:
        visitor(first(second(third(fourth))));
        break;
    }
}

template <class Visitor>
inline void for_each_predefined_bird_quad_at(
    std::size_t tuple_index,
    Visitor&& visitor) {
    auto const& combinators =
        predefined_bird_combinators();
    auto remainder = tuple_index;
    auto const fourth_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const third_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const second_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const first_index =
        remainder % check_for_match_combinator_count;

    auto const& first = combinators[first_index];
    auto const& second = combinators[second_index];
    auto const& third = combinators[third_index];
    auto const& fourth = combinators[fourth_index];
    auto const shape_mask =
        predefined_bird_quad_shape_mask(
            first, second, third, fourth);

    for (std::size_t shape_index = 0;
         shape_index < check_for_quads_match_shape_count;
         ++shape_index) {
        if ((shape_mask &
             (std::uint8_t{1} << shape_index)) != 0) {
            visit_predefined_bird_quad_shape(
                shape_index,
                first,
                second,
                third,
                fourth,
                [&](quoted_expression quad) {
                    visitor(
                        shape_index,
                        std::move(quad));
                });
        }
    }
}

template <class Visitor>
inline void for_each_predefined_bird_quad_column_at(
    std::size_t column_index,
    Visitor&& visitor) {
    auto const& combinators =
        predefined_bird_combinators();
    auto remainder = column_index;
    auto const shape_index =
        remainder % check_for_quads_match_shape_count;
    remainder /= check_for_quads_match_shape_count;
    auto const fourth_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const third_index =
        remainder % check_for_match_combinator_count;
    remainder /= check_for_match_combinator_count;
    auto const first_index =
        remainder % check_for_match_combinator_count;

    auto const& first = combinators[first_index];
    auto const& third = combinators[third_index];
    auto const& fourth = combinators[fourth_index];
    for (std::size_t second_index = 0;
         second_index < check_for_match_combinator_count;
         ++second_index) {
        auto const& second = combinators[second_index];
        if ((predefined_bird_quad_shape_mask(
                 first, second, third, fourth) &
             (std::uint8_t{1} << shape_index)) == 0) {
            continue;
        }
        auto const tuple_index =
            ((first_index * check_for_match_combinator_count +
              second_index) *
                 check_for_match_combinator_count +
             third_index) *
                check_for_match_combinator_count +
            fourth_index;
        visit_predefined_bird_quad_shape(
            shape_index,
            first,
            second,
            third,
            fourth,
            [&](quoted_expression quad) {
                visitor(
                    tuple_index *
                            check_for_quads_match_shape_count +
                        shape_index,
                    std::move(quad));
            });
    }
}

[[nodiscard]] inline bool check_normalized_match(
    quoted_expression combs,
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& normalized_expression) {
    for (auto const& symbol_value : symbol_list) {
        combs = combs(symbol_value.expression());
    }

    auto normalized_combs =
        normalize_for_combinator_match(std::move(combs));
    return normalized_combs &&
           same_parser_definition_expression(
               *normalized_combs,
               normalized_expression);
}

struct timed_find_match_result {
    bool matches = false;
    bool timed_out = false;
};

[[nodiscard]] inline timed_find_match_result
check_normalized_match_until(
    quoted_expression combs,
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& normalized_expression,
    find_clock::time_point deadline) {
    for (auto const& symbol_value : symbol_list) {
        if (find_clock_now() >= deadline) {
            return {false, true};
        }
        combs = combs(symbol_value.expression());
    }

    auto normalized_combs =
        normalize_for_combinator_match_until(
            std::move(combs), deadline);
    if (normalized_combs.timed_out) {
        return {false, true};
    }
    auto const matches =
        normalized_combs.expression &&
        same_parser_definition_expression(
            *normalized_combs.expression,
            normalized_expression);
    if (find_clock_now() >= deadline) {
        return {false, true};
    }
    return {matches, false};
}

enum class catalog_find_enumeration_status {
    completed,
    timed_out,
};

template <class Visitor>
[[nodiscard]] inline catalog_find_enumeration_status
for_each_catalog_candidate_of_size(
    std::size_t leaf_count,
    std::span<quoted_expression const> catalog,
    find_clock::time_point deadline,
    Visitor&& visitor) {
    if (leaf_count == 0 || catalog.empty()) {
        return catalog_find_enumeration_status::completed;
    }
    if (leaf_count >
        std::numeric_limits<std::size_t>::max() / 2 + 1) {
        return catalog_find_enumeration_status::completed;
    }

    auto const node_count = leaf_count * 2 - 1;
    // A pre-order full-binary-tree encoding: true is an application and
    // false is a catalog leaf. The explicit DFS stack avoids making the
    // unbounded leaf count an unbounded C++ call-stack depth.
    std::vector<bool> shape(node_count);
    struct shape_frame {
        std::size_t position;
        std::size_t applications;
        std::size_t leaves;
        std::size_t open_slots;
        std::uint8_t next_choice = 0;
    };
    std::vector<shape_frame> pending;
    pending.reserve(node_count);
    pending.push_back({0, 0, 0, 1});

    auto visit_labelings = [&](std::vector<bool> const& tree_shape) {
        std::vector<std::size_t> labels(leaf_count, 0);
        for (;;) {
            if (find_clock_now() >= deadline) {
                return catalog_find_enumeration_status::timed_out;
            }

            std::vector<quoted_expression> expressions;
            expressions.reserve(leaf_count);
            auto label_position = leaf_count;
            bool excluded = false;
            for (auto position = node_count;
                 position > 0;
                 --position) {
                if ((position & std::size_t{63}) == 0 &&
                    find_clock_now() >= deadline) {
                    return catalog_find_enumeration_status::timed_out;
                }
                if (!tree_shape[position - 1]) {
                    --label_position;
                    expressions.push_back(
                        catalog[labels[label_position]]);
                    continue;
                }

                auto function = std::move(expressions.back());
                expressions.pop_back();
                auto argument = std::move(expressions.back());
                expressions.pop_back();
                if (is_excluded_match_pair(function, argument)) {
                    excluded = true;
                    break;
                }
                expressions.push_back(
                    function(std::move(argument)));
            }
            if (!excluded &&
                !std::invoke(
                    visitor,
                    std::move(expressions.back()))) {
                return catalog_find_enumeration_status::timed_out;
            }

            bool advanced = false;
            for (auto position = leaf_count;
                 position > 0;
                 --position) {
                auto& label = labels[position - 1];
                ++label;
                if (label != catalog.size()) {
                    advanced = true;
                    break;
                }
                label = 0;
            }
            if (!advanced) {
                return catalog_find_enumeration_status::completed;
            }
        }
    };

    while (!pending.empty()) {
        if (find_clock_now() >= deadline) {
            return catalog_find_enumeration_status::timed_out;
        }

        auto& frame = pending.back();
        if (frame.position == node_count) {
            if (frame.applications == leaf_count - 1 &&
                frame.leaves == leaf_count &&
                frame.open_slots == 0) {
                auto const status = visit_labelings(shape);
                if (status !=
                    catalog_find_enumeration_status::completed) {
                    return status;
                }
            }
            pending.pop_back();
            continue;
        }

        if (frame.next_choice == 0) {
            frame.next_choice = 1;
            if (frame.applications < leaf_count - 1) {
                auto const new_open_slots =
                    frame.open_slots + 1;
                if (new_open_slots <=
                    leaf_count - frame.leaves) {
                    shape[frame.position] = true;
                    pending.push_back({
                        frame.position + 1,
                        frame.applications + 1,
                        frame.leaves,
                        new_open_slots,
                    });
                }
            }
            continue;
        }

        if (frame.next_choice == 1) {
            frame.next_choice = 2;
            if (frame.leaves < leaf_count &&
                frame.open_slots != 0) {
                auto const new_open_slots =
                    frame.open_slots - 1;
                if (new_open_slots != 0 ||
                    frame.position + 1 == node_count) {
                    shape[frame.position] = false;
                    pending.push_back({
                        frame.position + 1,
                        frame.applications,
                        frame.leaves + 1,
                        new_open_slots,
                    });
                }
            }
            continue;
        }

        pending.pop_back();
    }
    return catalog_find_enumeration_status::completed;
}

#if !defined(__EMSCRIPTEN__)
struct indexed_find_match {
    std::size_t index;
    quoted_expression expression;
};

inline constexpr std::size_t native_find_worker_limit =
    std::numeric_limits<std::uint64_t>::digits;

[[nodiscard]] inline constexpr std::size_t native_find_worker_count(
    std::size_t maximum_work_count,
    unsigned reported_hardware_concurrency =
        std::thread::hardware_concurrency()) noexcept {
    if (maximum_work_count == 0) {
        return 0;
    }
    auto const requested_worker_count =
        reported_hardware_concurrency > 1
            ? static_cast<std::size_t>(
                  reported_hardware_concurrency - 1)
            : std::size_t{1};
    return std::min(
        maximum_work_count,
        std::min(
            requested_worker_count,
            native_find_worker_limit));
}

template <class Work, class Generator, class Processor>
inline void dispatch_native_find_work(
    std::size_t maximum_work_count,
    Generator&& generate,
    Processor&& process,
    unsigned reported_hardware_concurrency =
        std::thread::hardware_concurrency()) {
    auto const worker_count = native_find_worker_count(
        maximum_work_count,
        reported_hardware_concurrency);
    if (worker_count == 0) {
        return;
    }

    struct worker_slot {
        std::optional<Work> work;
        std::exception_ptr failure;
        std::counting_semaphore<2> ready{0};
    };

    std::vector<std::unique_ptr<worker_slot>> slots;
    slots.reserve(worker_count);
    for (std::size_t index = 0;
         index < worker_count;
         ++index) {
        slots.push_back(std::make_unique<worker_slot>());
    }

    auto const all_worker_mask =
        worker_count == native_find_worker_limit
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{1} << worker_count) - 1;
    // The cross-atomic state/wakeup handshakes rely on the default
    // sequentially consistent ordering of all four state atomics.
    std::atomic<std::uint64_t> empty_worker_bits =
        all_worker_mask;
    std::atomic<std::uint64_t> waiting_worker_bits = 0;
    std::atomic<bool> producer_waiting = false;
    std::atomic<bool> stopping = false;
    std::atomic<std::size_t> failure_worker = worker_count;
    std::counting_semaphore<> producer_semaphore{0};

    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    try {
        for (std::size_t worker_index = 0;
             worker_index < worker_count;
             ++worker_index) {
            workers.emplace_back(
                [&, worker_index] {
                    auto& slot = *slots[worker_index];
                    auto const worker_mask =
                        std::uint64_t{1} << worker_index;
                    auto notify_producer = [&] {
                        if (producer_waiting.load()) {
                            producer_semaphore.release();
                        }
                    };

                    for (;;) {
                        if (stopping.load()) {
                            return;
                        }
                        if ((empty_worker_bits.load() &
                             worker_mask) == 0) {
                            try {
                                Work work =
                                    std::move(*slot.work);
                                slot.work.reset();
                                // The mailbox can be filled again while
                                // this work is being processed locally.
                                empty_worker_bits.fetch_or(
                                    worker_mask);
                                notify_producer();
                                // A generator or peer-worker failure
                                // cancels work that has not started.
                                if (stopping.load()) {
                                    return;
                                }
                                std::invoke(
                                    process,
                                    std::move(work));
                            } catch (...) {
                                slot.failure =
                                    std::current_exception();
                                auto expected = worker_count;
                                static_cast<void>(
                                    failure_worker
                                        .compare_exchange_strong(
                                            expected,
                                            worker_index));
                                stopping.store(true);
                                notify_producer();
                                return;
                            }
                            continue;
                        }

                        waiting_worker_bits.fetch_or(
                            worker_mask);
                        notify_producer();

                        // Recheck after registering as waiting. The
                        // side that clears the waiting bit owns the
                        // wakeup: the worker continues directly, or the
                        // producer releases the semaphore below.
                        if (((empty_worker_bits.load() &
                              worker_mask) == 0) &&
                            (waiting_worker_bits.fetch_and(
                                 ~worker_mask) &
                             worker_mask) != 0) {
                            continue;
                        }
                        slot.ready.acquire();
                    }
                });
        }
    } catch (...) {
        auto const startup_failure =
            std::current_exception();
        stopping.store(true);
        for (std::size_t worker_index = 0;
             worker_index < workers.size();
             ++worker_index) {
            slots[worker_index]->ready.release();
        }
        for (auto& worker : workers) {
            worker.join();
        }
        std::rethrow_exception(startup_failure);
    }

    auto wait_for_worker_state =
        [&](std::atomic<std::uint64_t>& state,
            auto&& ready) {
        auto value = state.load();
        while (!std::invoke(ready, value) &&
               !stopping.load()) {
            producer_waiting.store(true);
            value = state.load();
            if (!std::invoke(ready, value) &&
                !stopping.load()) {
                producer_semaphore.acquire();
            }
            producer_waiting.store(false);
            while (producer_semaphore.try_acquire()) {
            }
            value = state.load();
        }
        return value;
    };

    static_cast<void>(wait_for_worker_state(
        waiting_worker_bits,
        [&](std::uint64_t waiting) {
            return waiting == all_worker_mask;
        }));

    // generate must invoke submit synchronously on this producer thread
    // and must not retain it. process may be invoked concurrently, so
    // any state it shares between workers must be synchronized.
    auto submit = [&](Work work) {
        auto const empty = wait_for_worker_state(
            empty_worker_bits,
            [](std::uint64_t available) {
                return available != 0;
            });
        if (stopping.load()) {
            return false;
        }
        auto const worker_index =
            static_cast<std::size_t>(
                std::countr_zero(empty));
        auto const worker_mask =
            std::uint64_t{1} << worker_index;
        slots[worker_index]->work.emplace(
            std::move(work));
        empty_worker_bits.fetch_and(~worker_mask);
        // If the producer clears the waiting bit, it owns the wakeup.
        if ((waiting_worker_bits.fetch_and(
                 ~worker_mask) &
             worker_mask) != 0) {
            slots[worker_index]->ready.release();
        }
        return true;
    };

    std::exception_ptr generator_failure;
    try {
        std::invoke(
            std::forward<Generator>(generate),
            submit);
    } catch (...) {
        generator_failure = std::current_exception();
    }

    if (!generator_failure && !stopping.load()) {
        // First drain every mailbox, then wait until all local work is
        // complete and every worker has registered as waiting.
        static_cast<void>(wait_for_worker_state(
            empty_worker_bits,
            [&](std::uint64_t empty) {
                return empty == all_worker_mask;
            }));
        static_cast<void>(wait_for_worker_state(
            waiting_worker_bits,
            [&](std::uint64_t waiting) {
                return waiting == all_worker_mask;
            }));
    }
    stopping.store(true);
    for (auto& slot : slots) {
        slot->ready.release();
    }
    for (auto& worker : workers) {
        worker.join();
    }

    if (generator_failure) {
        std::rethrow_exception(generator_failure);
    }
    auto const failed_worker = failure_worker.load();
    if (failed_worker < worker_count) {
        std::rethrow_exception(
            slots[failed_worker]->failure);
    }
}
#endif

} // namespace detail

[[nodiscard]] inline bool check_for_match(
    quoted_expression combs,
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression) {
    auto normalized_expression =
        detail::normalize_for_combinator_match(expression);
    return normalized_expression &&
           detail::check_normalized_match(
               std::move(combs),
               symbol_list,
               *normalized_expression);
}

template <class Combs, class Expression>
    requires (!(std::same_as<
                    std::remove_cvref_t<Combs>,
                    quoted_expression> &&
                std::same_as<
                    std::remove_cvref_t<Expression>,
                    quoted_expression>))
[[nodiscard]] inline bool check_for_match(
    Combs&& combs,
    std::span<quoted_atomic const> symbol_list,
    Expression&& expression) {
    return check_for_match(
        quote(std::forward<Combs>(combs)),
        symbol_list,
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::vector<quoted_expression>
check_for_singles_match(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression) {
    std::vector<quoted_expression> result;
    auto normalized_expression =
        detail::normalize_for_combinator_match(expression);
    if (!normalized_expression) {
        return result;
    }

    for (auto const& combinator :
         detail::predefined_bird_combinators()) {
        if (detail::check_normalized_match(
                combinator,
                symbol_list,
                *normalized_expression)) {
            result.push_back(combinator);
        }
    }
    return result;
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::vector<quoted_expression>
check_for_singles_match(
    std::span<quoted_atomic const> symbol_list,
    Expression&& expression) {
    return check_for_singles_match(
        symbol_list,
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::vector<quoted_expression>
check_for_pairs_match(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression) {
    std::vector<quoted_expression> result;
    auto normalized_expression =
        detail::normalize_for_combinator_match(expression);
    if (!normalized_expression) {
        return result;
    }

    detail::for_each_predefined_bird_pair(
        [&](quoted_expression pair) {
            if (detail::check_normalized_match(
                    pair,
                    symbol_list,
                    *normalized_expression)) {
                result.push_back(std::move(pair));
            }
        });
    return result;
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::vector<quoted_expression>
check_for_pairs_match(
    std::span<quoted_atomic const> symbol_list,
    Expression&& expression) {
    return check_for_pairs_match(
        symbol_list,
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::vector<quoted_expression>
check_for_trips_match(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression) {
    std::vector<quoted_expression> result;
    auto normalized_expression =
        detail::normalize_for_combinator_match(expression);
    if (!normalized_expression) {
        return result;
    }

#if defined(__EMSCRIPTEN__)
    detail::for_each_predefined_bird_trip(
        [&](quoted_expression trip) {
            if (detail::check_normalized_match(
                    trip,
                    symbol_list,
                    *normalized_expression)) {
                result.push_back(std::move(trip));
            }
        });
#else
    (void)detail::predefined_bird_combinators();
    std::vector<detail::indexed_find_match> matches;
    std::mutex matches_mutex;

    detail::dispatch_native_find_work<std::size_t>(
        check_for_trips_match_column_count,
        [&](auto&& submit) {
            for (std::size_t column_index = 0;
                 column_index <
                     check_for_trips_match_column_count;
                 ++column_index) {
                if (!submit(column_index)) {
                    break;
                }
            }
        },
        [&](std::size_t column_index) {
            detail::for_each_predefined_bird_trip_column_at(
                column_index,
                [&](std::size_t candidate_index,
                    quoted_expression trip) {
                    if (detail::check_normalized_match(
                            trip,
                            symbol_list,
                            *normalized_expression)) {
                        std::scoped_lock lock(matches_mutex);
                        matches.push_back({
                            candidate_index,
                            std::move(trip),
                        });
                    }
                });
        });

    std::ranges::sort(
        matches,
        {},
        &detail::indexed_find_match::index);
    result.reserve(matches.size());
    for (auto& match : matches) {
        result.push_back(std::move(match.expression));
    }
#endif
    return result;
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::vector<quoted_expression>
check_for_trips_match(
    std::span<quoted_atomic const> symbol_list,
    Expression&& expression) {
    return check_for_trips_match(
        symbol_list,
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline std::vector<quoted_expression>
check_for_quads_match(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression) {
    std::vector<quoted_expression> result;
    auto normalized_expression =
        detail::normalize_for_combinator_match(expression);
    if (!normalized_expression) {
        return result;
    }
    (void)detail::predefined_bird_combinators();

#if defined(__EMSCRIPTEN__)
    for (std::size_t tuple_index = 0;
         tuple_index < check_for_quads_match_tuple_count;
         ++tuple_index) {
        detail::for_each_predefined_bird_quad_at(
            tuple_index,
            [&](std::size_t, quoted_expression quad) {
                if (detail::check_normalized_match(
                        quad,
                        symbol_list,
                        *normalized_expression)) {
                    result.push_back(std::move(quad));
                }
            });
    }
#else
    std::vector<detail::indexed_find_match> matches;
    std::mutex matches_mutex;

    detail::dispatch_native_find_work<std::size_t>(
        check_for_quads_match_column_count,
        [&](auto&& submit) {
            for (std::size_t column_index = 0;
                 column_index <
                     check_for_quads_match_column_count;
                 ++column_index) {
                if (!submit(column_index)) {
                    break;
                }
            }
        },
        [&](std::size_t column_index) {
            detail::for_each_predefined_bird_quad_column_at(
                column_index,
                [&](std::size_t candidate_index,
                    quoted_expression quad) {
                    if (detail::check_normalized_match(
                            quad,
                            symbol_list,
                            *normalized_expression)) {
                        std::scoped_lock lock(matches_mutex);
                        matches.push_back({
                            candidate_index,
                            std::move(quad),
                        });
                    }
                });
        });

    std::ranges::sort(
        matches,
        {},
        &detail::indexed_find_match::index);
    result.reserve(matches.size());
    for (auto& match : matches) {
        result.push_back(std::move(match.expression));
    }
#endif
    return result;
}

template <class Expression>
    requires (!std::same_as<
              std::remove_cvref_t<Expression>,
              quoted_expression>)
[[nodiscard]] inline std::vector<quoted_expression>
check_for_quads_match(
    std::span<quoted_atomic const> symbol_list,
    Expression&& expression) {
    return check_for_quads_match(
        symbol_list,
        quote(std::forward<Expression>(expression)));
}

[[nodiscard]] inline combinator_find_result
find_combinator_matches(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression,
    combinator_find_options options) {
    if (options.maximum_size < 1 || options.maximum_size > 4) {
        throw std::invalid_argument(
            "combdsl::find maximum size must be from 1 to 4");
    }

    combinator_find_result result;
    result.singles = check_for_singles_match(
        symbol_list, expression);
    if (options.maximum_size == 1 ||
        (!options.all_sizes && !result.singles.empty())) {
        return result;
    }
    result.pairs = check_for_pairs_match(
        symbol_list, expression);
    if (options.maximum_size == 2 ||
        (!options.all_sizes && !result.pairs.empty())) {
        return result;
    }
    result.triples = check_for_trips_match(
        symbol_list, expression);
    if (options.maximum_size == 3 ||
        (!options.all_sizes && !result.triples.empty())) {
        return result;
    }
    result.quads = check_for_quads_match(
        symbol_list, expression);
    return result;
}

[[nodiscard]] inline catalog_combinator_find_result
find_combinator_matches_among(
    std::span<quoted_atomic const> symbol_list,
    quoted_expression const& expression,
    std::span<quoted_expression const> catalog,
    bool all_sizes) {
    if (catalog.empty()) {
        throw std::invalid_argument(
            "combdsl::find among catalog cannot be empty");
    }

    catalog_combinator_find_result result;
    auto const deadline =
        detail::find_clock_now() + detail::find_search_window;
    auto normalized_expression =
        detail::normalize_for_combinator_match_until(
            expression, deadline);
    if (normalized_expression.timed_out) {
        result.timed_out = true;
        return result;
    }
    if (!normalized_expression.expression) {
        return result;
    }

    for (std::size_t leaf_count = 1;; ++leaf_count) {
        if (detail::find_clock_now() >= deadline) {
            result.timed_out = true;
            return result;
        }
        std::vector<quoted_expression> matches;
        auto const enumeration_status =
            detail::for_each_catalog_candidate_of_size(
                leaf_count,
                catalog,
                deadline,
                [&](quoted_expression candidate) {
                    auto const checked =
                        detail::check_normalized_match_until(
                            candidate,
                            symbol_list,
                            *normalized_expression.expression,
                            deadline);
                    if (checked.timed_out) {
                        return false;
                    }
                    if (checked.matches) {
                        matches.push_back(std::move(candidate));
                    }
                    return true;
                });
        if (enumeration_status ==
            detail::catalog_find_enumeration_status::timed_out) {
            result.timed_out = true;
            return result;
        }
        if (detail::find_clock_now() >= deadline) {
            result.timed_out = true;
            return result;
        }

        auto const found_match = !matches.empty();
        result.completed_sizes.push_back(std::move(matches));
        if (!all_sizes && found_match) {
            return result;
        }
        if (leaf_count ==
            std::numeric_limits<std::size_t>::max()) {
            return result;
        }
    }
}

} // namespace combdsl
