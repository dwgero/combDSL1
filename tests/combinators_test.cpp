/*
 * C++ combinator DSL
 * Copyright (C) 2026  David W. Gero
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <combdsl/combinators.hpp>

#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using combdsl::I;
using combdsl::K;
using combdsl::S;
using combdsl::Y;
using combdsl::basis;
using combdsl::color_step;
using combdsl::defer;
using combdsl::eval;
using combdsl::force;
using combdsl::input_escape;
using combdsl::is_single_utf8_char;
using combdsl::parse;
using combdsl::parse_and_step;
using combdsl::parse_eval;
using combdsl::parse_error;
using combdsl::quote;
using combdsl::read_parse_eval;
using combdsl::single_step;
using combdsl::single_step_loop;
using combdsl::single_step_run;
using combdsl::symbol;
using combdsl::a;
using combdsl::b;
using combdsl::c;
using combdsl::d;
using combdsl::e;
using combdsl::f;
using combdsl::g;
using combdsl::h;
using combdsl::i;
using combdsl::j;
using combdsl::k;
using combdsl::l;
using combdsl::m;
using combdsl::n;
using combdsl::o;
using combdsl::p;
using combdsl::q;
using combdsl::r;
using combdsl::s;
using combdsl::t;
using combdsl::u;
using combdsl::v;
using combdsl::w;
using combdsl::x;
using combdsl::y;
using combdsl::z;
using combdsl::M;
using combdsl::W;
using combdsl::O;
using combdsl::B;
using combdsl::T;
using combdsl::N;
using combdsl::R;
using combdsl::C;
using combdsl::P;
using combdsl::V;
using combdsl::D;
using combdsl::L;
using combdsl::Z;
using combdsl::A;
using combdsl::Cstar;
using combdsl::Vstar;
using combdsl::V4;
using combdsl::G;
using combdsl::Hprime;
using combdsl::H;

void ensure_external_basis_registered();

namespace {

const auto G2 = basis("G2", 1, Vstar(V4(S)(T)(K))(K(K))(S(K)));
const auto bazTest =
    basis("bazTest", 3, B(W)(B(B(C))(
        B(B(S(u)))(C(B(B)(B))(T)))));

constexpr auto add = [](int left) {
    return [left](int right) { return left + right; };
};

constexpr auto square_number = [](int value) { return value * value; };
constexpr auto increment = [](int value) { return value + 1; };

struct named_value {
    [[nodiscard]] std::string toString() const {
        return "named";
    }
};

struct move_only_named {
    move_only_named() = default;
    move_only_named(move_only_named const&) = delete;
    move_only_named(move_only_named&&) = default;

    [[nodiscard]] std::string toString() const {
        return "move-only";
    }
};

struct copy_only_named {
    copy_only_named() = default;
    copy_only_named(copy_only_named const&) = default;
    copy_only_named(copy_only_named&&) = delete;

    [[nodiscard]] std::string_view toString() const {
        return "copy-only";
    }
};

struct operand_named_value {
    void print_as_operand_to(std::ostream& output) const {
        output << "wrong";
    }

    [[nodiscard]] std::string toString() const {
        return "operand";
    }
};

struct owned_text {
    std::string value;
};

class interrupting_quoted_node final
    : public combdsl::detail::quoted_node {
public:
    [[nodiscard]] combdsl::detail::quoted_node_kind kind()
        const noexcept override {
        if (!raised_) {
            raised_ = true;
            static_cast<void>(std::raise(SIGINT));
        }
        return combdsl::detail::quoted_node_kind::opaque;
    }

    void print_to(std::ostream& output) const override {
        output << "current";
    }

private:
    mutable bool raised_ = false;
};

[[nodiscard]] combdsl::quoted_expression interrupting_expression() {
    return combdsl::detail::quoted_access::make(
        std::make_shared<interrupting_quoted_node>());
}

class interrupting_identity_node final
    : public combdsl::detail::quoted_node {
public:
    [[nodiscard]] combdsl::detail::quoted_node_kind kind()
        const noexcept override {
        if (!raised_) {
            raised_ = true;
            static_cast<void>(std::raise(SIGINT));
        }
        return combdsl::detail::quoted_node_kind::identity;
    }

    void print_to(std::ostream& output) const override {
        output << "interrupting-I";
    }

private:
    mutable bool raised_ = false;
};

[[nodiscard]] combdsl::quoted_expression
interrupting_identity() {
    return combdsl::detail::quoted_access::make(
        std::make_shared<interrupting_identity_node>());
}

[[nodiscard]] combdsl::quoted_expression
interrupting_identity_expression(combdsl::quoted_expression argument) {
    return interrupting_identity()(std::move(argument));
}

static_assert(std::is_same_v<decltype(I(std::declval<int&>())), int&>);
static_assert(std::is_same_v<decltype(parse("x")),
                             combdsl::quoted_expression>);
static_assert(is_single_utf8_char(
    std::string_view("\xF0\x9F\x98\x80", 4)));
static_assert(!is_single_utf8_char(
    std::string_view("\xF4\x90\x80\x80", 4)));
constexpr auto constexpr_utf8_symbol = symbol("\xCE\xBB");
static_assert(std::is_same_v<
              decltype(input_escape(std::declval<std::string_view>())),
              std::string>);
static_assert(combdsl::detail::is_raw_string_operand_v<std::string&>);
static_assert(combdsl::detail::is_raw_string_operand_v<const char (&)[5]>);
static_assert(
    !combdsl::detail::is_raw_string_operand_v<volatile std::string&>);
static_assert(
    !combdsl::detail::is_raw_string_operand_v<volatile std::string_view&>);
static_assert(
    !combdsl::detail::is_raw_string_operand_v<volatile char (&)[5]>);
static_assert(!combdsl::detail::is_raw_string_operand_v<volatile char*&>);
static_assert(!combdsl::detail::is_raw_string_operand_v<char* volatile&>);

std::size_t test_failures = 0;
std::size_t tests_run = 0;

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

class output_capture {
public:
    explicit output_capture(std::ostream& destination)
        : original_(std::cout.rdbuf(destination.rdbuf())) {}

    output_capture(output_capture const&) = delete;
    output_capture& operator=(output_capture const&) = delete;

    ~output_capture() {
        std::cout.rdbuf(original_);
    }

private:
    std::streambuf* original_;
};

class input_redirect {
public:
    explicit input_redirect(std::istream& source)
        : original_(std::cin.rdbuf(source.rdbuf())) {}

    input_redirect(input_redirect const&) = delete;
    input_redirect& operator=(input_redirect const&) = delete;

    ~input_redirect() {
        std::cin.rdbuf(original_);
    }

private:
    std::streambuf* original_;
};

class interrupt_on_flush_buffer : public std::stringbuf {
public:
    explicit interrupt_on_flush_buffer(std::size_t interrupts = 1)
        : interrupts_(interrupts) {}

protected:
    int sync() override {
        auto const result = std::stringbuf::sync();
        if (interrupts_ != 0) {
            --interrupts_;
            std::raise(SIGINT);
        }
        return result;
    }

private:
    std::size_t interrupts_;
};

volatile std::sig_atomic_t test_sigint_received = 0;

void test_sigint_handler(int) {
    test_sigint_received = 1;
}

template <class Expression>
    requires std::invocable<Expression&&>
void test(
    std::string_view title,
    Expression&& expression,
    std::string_view expected) {
    ++tests_run;

    std::ostringstream output;
    {
        output_capture capture(output);
        std::invoke(std::forward<Expression>(expression));
    }

    auto const actual = output.str();
    if (actual != expected) {
        std::cerr << "FAILED:   " << title << '\n'
                  << "expected: " << expected << '\n'
                  << "actual:   " << actual << '\n';
        ++test_failures;
    }
}

void test_valid_utf8_symbol(
    std::string_view title,
    std::string bytes) {
    test(title, symbol(bytes), bytes);
}

void test_invalid_utf8_symbol(
    std::string_view title,
    std::string bytes) {
    test(
        title,
        [bytes = std::move(bytes)] {
            try {
                static_cast<void>(symbol(bytes));
            } catch (std::invalid_argument const&) {
                std::cout << "invalid";
            }
        },
        "invalid");
}

[[nodiscard]] std::string colored_argument(
    std::string_view color,
    std::string_view class_name,
    std::string_view argument) {
    std::string result;
    result.reserve(
        color.size() + class_name.size() + argument.size() + 57);
    result += "<font color=\"";
    result += color;
    result += "\"><span class=\"";
    result += class_name;
    result += "\">&nbsp;";
    result += argument;
    result += "&nbsp;</span></font>";
    return result;
}

[[nodiscard]] std::string red_argument(std::string_view argument) {
    return colored_argument("red", "wor", argument);
}

[[nodiscard]] std::string green_argument(std::string_view argument) {
    return colored_argument("#00cc00", "wog", argument);
}

[[nodiscard]] std::string blue_argument(std::string_view argument) {
    return colored_argument("blue", "wob", argument);
}

[[nodiscard]] std::string dark_orange_argument(
    std::string_view argument) {
    return colored_argument("#ff8c00", "woo", argument);
}

void test_parse_failure(
    std::string_view title,
    std::string_view source,
    std::size_t expected_position) {
    ++tests_run;

    try {
        static_cast<void>(parse(source));
    } catch (parse_error const& error) {
        if (error.position() != expected_position) {
            std::cerr << "FAILED:   " << title << '\n'
                      << "expected position: " << expected_position << '\n'
                      << "actual position:   " << error.position() << '\n';
            ++test_failures;
        }
        return;
    } catch (std::exception const& error) {
        std::cerr << "FAILED:   " << title << '\n'
                  << "expected: combdsl::parse_error\n"
                  << "actual:   " << error.what() << '\n';
        ++test_failures;
        return;
    } catch (...) {
        std::cerr << "FAILED:   " << title << '\n'
                  << "expected: combdsl::parse_error\n"
                  << "actual:   unknown exception\n";
        ++test_failures;
        return;
    }

    std::cerr << "FAILED:   " << title << '\n'
              << "expected: combdsl::parse_error\n"
              << "actual:   no exception\n";
    ++test_failures;
}

}  // namespace

int main() {
    ensure_external_basis_registered();

    const auto circle = symbol("\xE2\x97\x8F");
    const auto square = symbol("\xE2\x96\xA0");
    const auto triangle = symbol("\xE2\x96\xB2");
    std::string mutable_utf8_name("\xCE\xBB", 2);
    const auto lambda = symbol(mutable_utf8_name);
    mutable_utf8_name.assign("x");
    std::string mutable_raw_string = "word";
    const auto copied_raw_string = x(mutable_raw_string);
    mutable_raw_string.assign("changed");
    std::string mutable_raw_view_source = "word";
    const auto copied_raw_view =
        x(std::string_view(mutable_raw_view_source));
    mutable_raw_view_source.assign("changed");
    char mutable_raw_array[] = "word";
    const auto copied_raw_array = x(mutable_raw_array);
    mutable_raw_array[0] = 's';
    char const* raw_string_pointer = "word";
    char const* null_raw_string = nullptr;
    const auto raw_string_basis = basis("RawText", 1, "word");
    const auto zero_arity_basis = basis("Qzero", 0, K);
    auto seven_character_basis = basis("1234567", 1, I);
    std::string copied_basis_name = "Alias";
    auto copied_name_basis = basis(copied_basis_name, 1, I);
    copied_basis_name.assign("other");
    auto null_terminated_basis =
        basis(std::string_view("Trimmed\0ignored", 15), 1, I);

    int basis_deferred_evaluations = 0;
    auto deferred_basis = basis("D", 1, defer([&basis_deferred_evaluations] {
        ++basis_deferred_evaluations;
        return 42;
    }));
    static_cast<void>(basis("Scope", 1, I));
    static_cast<void>(basis("foo", 1, I));
    static_cast<void>(basis("J", 1, I));
    static_cast<void>(basis("Jlong", 1, K));
    static_cast<void>(basis("Sfoo", 1, I));
    static_cast<void>(basis("K", 1, I));
    static_cast<void>(basis("MovOnly", 1, K(move_only_named{})));
    static_cast<void>(basis("Dupe", 1, I));
    auto duplicate_basis = basis("Dupe", 1, K);
    static_cast<void>(basis("M", 1, I));

    int printed_lambda_calls = 0;
    auto unprintable_lambda = [&printed_lambda_calls] {
        ++printed_lambda_calls;
    };
    int printed_deferred_evaluations = 0;
    auto printed_deferred = defer([&printed_deferred_evaluations] {
        ++printed_deferred_evaluations;
        return 42;
    });
    int symbolic_deferred_evaluations = 0;
    auto symbolic_deferred = defer([&symbolic_deferred_evaluations] {
        ++symbolic_deferred_evaluations;
        return 7;
    });

    test("I", I, "I");
    test("K", K, "K");
    test("S", S, "S");
    test("Y", Y, "Y");
    test("K(42)", K(42), "K<42>");
    test("K(named)", K(named_value{}), "K<named>");
    test("S(K)", S(K), "SK");
    test("S(K)(I)", S(K)(I), "SKI");
    test("K(unprintable)", K(unprintable_lambda), "K<?>");
    test("K(deferred)", K(printed_deferred), "K<deferred>");
    test("S(add)(I)", S(add)(I), "S<?>I");
    test("K(Y)", K(Y), "KY");
    test("predefined symbols a-z",
         [] {
             a();
             b();
             c();
             d();
             e();
             f();
             g();
             h();
             i();
             j();
             k();
             l();
             m();
             n();
             o();
             p();
             q();
             r();
             s();
             t();
             u();
             v();
             w();
             x();
             y();
             z();
         },
         "abcdefghijklmnopqrstuvwxyz");
    test("symbol u", u, "u");
    test("symbol v", v, "v");
    test("symbol w", w, "w");
    test("symbol x", x, "x");
    test("symbol y", y, "y");
    test("symbol z", z, "z");
    test("symbol circle", circle, "\xE2\x97\x8F");
    test("symbol square", square, "\xE2\x96\xA0");
    test("symbol triangle", triangle, "\xE2\x96\xB2");
    test("I with circle, square, and triangle",
         I(circle)(square)(triangle),
         "\xE2\x97\x8F" "\xE2\x96\xA0" "\xE2\x96\xB2");
    test("K with circle, square, and triangle",
         K(circle)(square)(triangle),
         "\xE2\x97\x8F" "\xE2\x96\xB2");
    test("S with circle, square, and triangle",
         S(circle)(square)(triangle),
         "\xE2\x97\x8F" "\xE2\x96\xB2"
         "(" "\xE2\x96\xA0" "\xE2\x96\xB2" ")");
    test("Y with circle, square, and triangle",
         Y(circle)(square)(triangle),
         "\xE2\x97\x8F" "<deferred Y("
         "\xE2\x97\x8F" ")>"
         "\xE2\x96\xA0" "\xE2\x96\xB2");
    test_valid_utf8_symbol("ASCII string symbol", std::string("Q"));
    test_valid_utf8_symbol(
        "null UTF-8 symbol", std::string(1, '\0'));
    test_valid_utf8_symbol(
        "two-byte UTF-8 symbol", std::string("\xC2\xA2", 2));
    test_valid_utf8_symbol(
        "lowest three-byte UTF-8 symbol",
        std::string("\xE0\xA0\x80", 3));
    test_valid_utf8_symbol(
        "three-byte UTF-8 symbol", std::string("\xE2\x82\xAC", 3));
    test_valid_utf8_symbol(
        "lowest four-byte UTF-8 symbol",
        std::string("\xF0\x90\x80\x80", 4));
    test_valid_utf8_symbol(
        "four-byte UTF-8 symbol",
        std::string("\xF0\x9F\x98\x80", 4));
    test_valid_utf8_symbol(
        "highest UTF-8 symbol",
        std::string("\xF4\x8F\xBF\xBF", 4));
    test("UTF-8 symbol owns its name", lambda, "\xCE\xBB");
    test("constexpr UTF-8 symbol", constexpr_utf8_symbol, "\xCE\xBB");
    test("UTF-8 symbol application", lambda(x), "\xCE\xBB" "x");
    test("quoted UTF-8 symbol", quote(lambda), "\xCE\xBB");
    test("single UTF-8 validator accepts one character",
         [] {
             std::cout << (is_single_utf8_char(
                                  std::string("\xF0\x9F\x98\x80", 4))
                               ? "valid"
                               : "invalid");
         },
         "valid");
    test_invalid_utf8_symbol("empty UTF-8 symbol", std::string());
    test_invalid_utf8_symbol(
        "multiple ASCII symbols", std::string("xy"));
    test_invalid_utf8_symbol(
        "multiple UTF-8 symbols",
        std::string("\xC2\xA2\xC2\xA2", 4));
    test_invalid_utf8_symbol(
        "stray UTF-8 trailing byte", std::string("\x80", 1));
    test_invalid_utf8_symbol(
        "invalid two-byte UTF-8 trail", std::string("\xC2\x20", 2));
    test_invalid_utf8_symbol(
        "truncated two-byte UTF-8 symbol", std::string("\xC2", 1));
    test_invalid_utf8_symbol(
        "overlong two-byte UTF-8 symbol", std::string("\xC0\x80", 2));
    test_invalid_utf8_symbol(
        "overlong three-byte UTF-8 symbol",
        std::string("\xE0\x9F\xBF", 3));
    test_invalid_utf8_symbol(
        "UTF-8 surrogate", std::string("\xED\xA0\x80", 3));
    test_invalid_utf8_symbol(
        "overlong four-byte UTF-8 symbol",
        std::string("\xF0\x8F\xBF\xBF", 4));
    test_invalid_utf8_symbol(
        "UTF-8 symbol above U+10FFFF",
        std::string("\xF4\x90\x80\x80", 4));
    test_invalid_utf8_symbol(
        "invalid UTF-8 leading byte",
        std::string("\xF5\x80\x80\x80", 4));
    test_invalid_utf8_symbol(
        "five-byte UTF-8 symbol",
        std::string("\xF0\x90\x80\x80\x80", 5));
    test("uppercase symbol rejected",
         [] {
             try {
                 static_cast<void>(symbol('Q'));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("punctuation symbol rejected",
         [] {
             try {
                 static_cast<void>(symbol('@'));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("input escape empty",
         [] { std::cout << input_escape(""); }, "");
    test("input escape ordinary text",
         [] { std::cout << input_escape("abc XYZ 123()"); },
         "abc XYZ 123()");
    test("input escape treats R as an ordinary character",
         [] { std::cout << input_escape("R"); }, "R");
    test("input escape double quote",
         [] { std::cout << input_escape("\""); }, "\\\"");
    test("input escape backslash",
         [] { std::cout << input_escape("\\"); }, "\\\\");
    test("input escape mixed text",
         [] { std::cout << input_escape("a\\b\"c"); },
         "a\\\\b\\\"c");
    test("input escape adjacent special characters",
         [] {
             char const input[] = {'\\', '"'};
             char const expected[] = {'\\', '\\', '\\', '"'};
             std::cout <<
                 (input_escape(std::string_view(input, sizeof input)) ==
                          std::string_view(expected, sizeof expected)
                      ? "valid"
                      : "invalid");
         },
         "valid");
    test("input escape preserves embedded null",
         [] {
             char const input[] = {'a', '\0', '"', '\\', 'b'};
             std::cout << input_escape(
                 std::string_view(input, sizeof input));
         },
         std::string_view("a\0\\\"\\\\b", 7));
    test("input escape owns its result",
         [] {
             std::string input = "a\"b";
             auto escaped = input_escape(input);
             input.assign("changed");
             std::cout << escaped;
         },
         "a\\\"b");
    test("input escape handles every byte",
         [] {
             std::string input;
             for (unsigned int byte = 0; byte <= 255; ++byte) {
                 input.push_back(static_cast<char>(byte));
             }

             auto const escaped = input_escape(input);
             auto position = std::size_t{0};
             auto valid = escaped.size() == input.size() + 2;
             for (char const byte : input) {
                 if (byte == '\\' || byte == '"') {
                     valid = valid && position < escaped.size() &&
                             escaped[position++] == '\\';
                 }
                 valid = valid && position < escaped.size() &&
                         escaped[position++] == byte;
             }
             valid = valid && position == escaped.size();
             std::cout << (valid ? "valid" : "invalid");
         },
         "valid");
    test("raw string literal operand", x("word"), "x word");
    test("raw std::string operand", x(std::string{"word"}), "x word");
    test("raw std::string_view operand",
         x(std::string_view{"word"}), "x word");
    test("raw C string pointer operand", x(raw_string_pointer), "x word");
    test("raw string operand owns its text", copied_raw_string, "x word");
    test("raw string_view operand owns its text", copied_raw_view, "x word");
    test("raw C array operand owns its text", copied_raw_array, "x word");
    test("one-byte raw string operand", x("Q"), "xQ");
    test("raw UTF-8 string uses name spacing",
         x("\xE2\x97\x8F"), "x " "\xE2\x97\x8F");
    test("explicit UTF-8 symbol remains compact",
         x(circle), "x" "\xE2\x97\x8F");
    test("quoted raw string", quote("word"), "word");
    test("quoted raw string application", quote("word")(x), "word x");
    test("adjacent raw strings",
         quote("left")(std::string{"right"}), "left right");
    test("K raw string", K(std::string{"word"}), "K word");
    test("K raw string remains callable", K("word")(x)(y), "word y");
    test("raw string followed by symbol", x("word")(y), "x word y");
    test("raw string before application",
         x("word")(y(z)), "x word(yz)");
    test("raw string inside application", x(y("word")), "x(y word)");
    test("raw string after application", x(y(z))("word"), "x(yz)word");
    test("basis before raw string", quote(Cstar)("word"), "Cstar word");
    test("raw string before basis", quote("word")(Cstar), "word Cstar");
    test("raw string application as operand",
         x(quote("word")(y)), "x(word y)");
    test("I makes raw strings symbolic", I("word")(x), "word x");
    test("S duplicates a raw string symbolically",
         S(I)(I)("word"), "word word");
    test("raw strings as S functions",
         S("left")("right")("value"),
         "left value(right value)");
    test("Y raw string", Y("word"), "word <deferred Y(word)>");
    test("basis argument normalizes raw string",
         M("word"), "word word");
    test("basis body normalizes raw strings",
         raw_string_basis(x), "word x");
    test("undersaturated basis raw string", T("word"), "T word");
    test("single step I raw string",
         single_step(quote(I)(std::string{"word"})), "word");
    test("single step K raw string",
         single_step(quote(K)(std::string{"word"})(x)), "word");
    test("single step Y raw string",
         single_step(quote(Y)("word")),
         "word <deferred Y(word)>");
    test("single step raw string basis expansion",
         single_step(quote(raw_string_basis)(x)), "word x");
    test("single step S raw strings",
         single_step(quote(S)("left")("right")("value")),
         "left value(right value)");
    test("parsed raw string basis expansion",
         single_step(parse("RawText x")), "word x");
    test("native S with raw string",
         S(K(x))(K(std::string{"word"}))(y), "x word");
    test("S preserves an unrelated reference result",
         [] {
             auto external = std::make_unique<int>(42);
             auto make_reference_returner = [&external](auto const&) {
                 return [&external](auto const&) -> std::unique_ptr<int>& {
                     return external;
                 };
             };
             auto make_argument = [](auto const&) { return 0; };

             auto& result =
                 S(make_reference_returner)(make_argument)("word");
             std::cout << (&result == &external ? "same" : "different");
         },
         "same");
    copy_only_named copy_only_operand;
    test("copy-only host operand", x(copy_only_operand), "x<copy-only>");
    test("raw strings preserve arbitrary bytes",
         quote(std::string("\xC2\x20", 2)), "\xC2\x20");
    test("empty raw string rejected",
         [] {
             try {
                 static_cast<void>(I(std::string{}));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("null raw string rejected",
         [&] {
             try {
                 static_cast<void>(x(null_raw_string));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("K(x)", K(x), "Kx");
    test("S(x)(y)", S(x)(y), "Sxy");
    test("S(x)(y)(z)", S(x)(y)(z), "xz(yz)");
    test("x(y)(z)", x(y)(z), "xyz");
    test("x(y(z))", x(y(z)), "x(yz)");
    test("K(x(y))", K(x(y)), "K(xy)");
    test("x(42)", x(42), "x<42>");
    test(
        "S(x)(y)(deferred)",
        S(x)(y)(symbolic_deferred),
        "x<deferred>(y<deferred>)");
    test("S(K(x(y)))(z)", S(K(x(y)))(z), "S(K(xy))z");
    test("M", M, "M");
    test("K(M)", K(M), "KM");
    test("S(M)(x)", S(M)(x), "SMx");
    test("x(M)", x(M), "xM");
    test("T", T, "T");
    test("T(x)", T(x), "Tx");
    test("T(x)(y)", T(x)(y), "yx");
    test("parse SKI", parse("SKI"), "SKI");
    test("parse SKIx", parse("SKIx"), "SKIx");
    test("parse symbols", parse("uvwxyz"), "uvwxyz");
    test("parse lowercase primitive names as symbols", parse("skiy"), "skiy");
    test("parse whitespace", parse(" \tS \n K\r\n I \v x\f"), "SKIx");
    test("parse left associative", parse("xyz"), "xyz");
    test("parse right nested operand", parse("x(yz)"), "x(yz)");
    test("parse grouped operand", parse("S ( K I ) x"), "S(KI)x");
    test("parse redundant groups", parse("((SK)I)x"), "SKIx");
    test("set defines a zero-arity basis",
         parse("set SetK=K"), "SetK");
    test("set basis expands with trailing arguments",
         single_step(parse("SetK x y")), "Kxy");
    test("set accepts parser whitespace",
         parse(" \tset\nWsSet \r=\v I\f"), "WsSet");
    test("whitespace-defined set basis expands",
         single_step(parse("WsSet x")), "Ix");
    test("set accepts a compound combinator expression",
         parse("set SetM = S(I)(I)"), "SetM");
    test("compound set basis expands as one zero-arity step",
         single_step(parse("SetM x")), "SIIx");
    test("set without required whitespace remains symbols",
         parse("setx"), "setx");
    test("bare set remains symbols", parse("set"), "set");
    test("first set definition",
         parse("set DynDup=I"), "DynDup");
    test("later duplicate set definition remains usable",
         single_step(parse("set DynDup=K")), "K");
    test("first set definition wins parser lookup",
         single_step(parse("DynDup x")), "Ix");
    test("set primitive definition remains usable for that parse",
         single_step(parse("set K=I")), "I");
    test("set cannot replace a primitive", parse("K"), "K");
    test("parse left association reduction", single_step(parse("KIxy")),
         "Iy");
    test("parse parentheses override association",
         single_step(parse("K(Ix)y")), "Ix");
    test("parse escaped word", parse("\\\"word\\\""), "word");
    test("parse escaped word example",
         parse("x \\\"word\\\" y \\\"mid\\\\dle\\\" z"),
         "x word y mid\\dle z");
    test("parse input_escape output",
         parse(input_escape("x \"word\" y \"mid\\dle\" z")),
         "x word y mid\\dle z");
    test("parse adjacent escaped words",
         parse("\\\"left\\\"\\\"right\\\""), "left right");
    test("parse escaped word inside parentheses",
         parse("x(\\\"word\\\"y)"), "x(word y)");
    test("parse escaped word preserves parser characters",
         parse(input_escape("x \"a b()\" y")), "x a b() y");
    test("parse escaped backslash as an operand",
         parse("\\\\"), "\\");
    test("parse escaped backslash between symbols",
         parse("x\\\\y"), "x\\y");
    test("unregistered double backslash remains an operand",
         single_step(parse("\\\\x")), "\\x");
    const auto embedded_double_backslash_basis =
        basis("A\\\\B", 1, I);
    test("double backslash inside basis name",
         single_step(parse("A\\\\B x")), "x");
    test("basis step exposes double backslash basis definition",
         single_step(parse("A\\\\B x"), true), "Ix");
    test_parse_failure("unseparated double backslash basis is unknown",
                       "A\\\\Bx", 0);
    test("parse escaped backslash inside word",
         parse("\\\"a\\\\b\\\""), "a\\b");
    test("parse UTF-8 escaped word",
         parse("\\\"\xE2\x97\x8F\\\"x"),
         "\xE2\x97\x8F x");
    {
        char const source[] = {'\\', '"', 'a', '"', 'b', '\\', '"'};
        test("parse escaped word preserves bare quote",
             parse(std::string_view(source, sizeof source)), "a\"b");
    }
    {
        char const source[] = {'\\', '"', 'a', '\0', 'b', '\\', '"'};
        test("parse escaped word preserves embedded null",
             parse(std::string_view(source, sizeof source)),
             std::string_view("a\0b", 3));
    }
    test("parsed escaped word owns its contents",
         [] {
             std::string source = "x\\\"word\\\"";
             auto expression = parse(source);
             source.assign("changed");
             expression();
         },
         "x word");
    test("single step parsed I escaped word",
         single_step(parse("I\\\"word\\\"")), "word");
    test("single step parsed K escaped word",
         single_step(parse("K\\\"left\\\"x")), "left");
    test("single step parsed S escaped words",
         single_step(parse(
             "S\\\"left\\\"\\\"right\\\"\\\"value\\\"")),
         "left value(right value)");
    test("single step parsed Y escaped word",
         single_step(parse("Y\\\"word\\\"")),
         "word <deferred Y(word)>");
    test("parse escaped word beside undersaturated basis",
         parse("T\\\"word\\\""), "T word");
    test("escaped basis spelling remains a word",
         single_step(parse("\\\"M\\\"x")), "Mx");

    constexpr std::string_view expected_registered_basis_names[] = {
        "M", "W", "B", "O", "T", "N", "R", "C", "P", "V", "D",
        "L", "Z", "A", "Cstar", "Vstar", "V4", "G2", "G", "bazTest",
        "Hprime", "H",
    };
    for (auto const name : expected_registered_basis_names) {
        auto title = std::string("parse named basis ");
        title += name;
        test(title, parse(name), name);
    }

    test("parse single-character M without a delimiter",
         single_step(parse("Mx")), "xx");
    test("basis step exposes parsed M definition",
         single_step(parse("Mx"), true), "SIIx");
    test("parse single-character T without a delimiter",
         parse("Tx"), "Tx");
    test("parse single-character C without a delimiter",
         parse("Cx"), "Cx");
    test("parse separated Cstar", single_step(parse("Cstar x")),
         "Cstar x");
    test("escaped word opener delimits Cstar",
         parse("Cstar\\\"word\\\""), "Cstar word");
    test_parse_failure("unseparated Cstar is an unknown operand",
                       "Cstarx", 0);
    test("parse separated Hprime", single_step(parse("Hprime x y")),
         "Hprime xy");
    test("parse G2 exact match", single_step(parse("G2")), "G2");
    test("parse separated V4", single_step(parse("V4 x y z")), "V4 xyz");
    test("basis automatically registers seven-character name",
         single_step(parse("1234567 x")), "x");
    test("basis registration copies mutable name",
         single_step(parse("Alias x")), "x");
    test("basis step exposes registered basis definition",
         single_step(parse("Alias x"), true), "Ix");
    test("basis registration uses visible null-terminated name",
         single_step(parse("Trimmed x")), "x");
    test("mutated basis source name is not registered", parse("otherx"),
         "otherx");
    test("registered basis outlives local handle",
         single_step(parse("Scope x")), "x");
    test("lowercase registered basis uses an exact token",
         single_step(parse("foo x")), "x");
    test("unseparated lowercase name remains symbols",
         parse("foox"), "foox");
    test("single-character registered basis", single_step(parse("Jx")),
         "x");
    test("registered bases use exact token lookup",
         single_step(parse("Jlong x")), "Kx");
    test("longer basis may start with primitive",
         single_step(parse("Sfoo x")), "x");
    test("primitive prefix wins when the longer token is not exact",
         single_step(parse("Sfoox")), "fo(oo)x");
    test("exact primitive name remains reserved",
         single_step(parse("Kxy")), "x");
    test("first duplicate basis registration wins",
         single_step(parse("Dupe x")), "x");
    test("later duplicate basis remains usable", duplicate_basis(x), "Kx");
    test("registered move-only basis contracts",
         single_step(parse("MovOnly x")), "<move-only>");
    test("basis step exposes registered move-only basis definition",
         single_step(parse("MovOnly x"), true), "K<move-only>x");
    test("second step leaves contracted registered move-only basis",
         single_step(single_step(parse("MovOnly x"))), "<move-only>");
    test("registered move-only basis remains reusable",
         single_step(parse("MovOnly y")), "<move-only>");
    test("basis registry is shared across translation units",
         single_step(parse("Extern x")), "x");

    test_parse_failure("parse empty input", "", 0);
    test_parse_failure("parse whitespace-only input", " \t", 2);
    test_parse_failure("parse empty parentheses", "()", 1);
    test_parse_failure("parse whitespace-only parentheses", "( )", 2);
    test_parse_failure("parse missing close", "(x", 2);
    test_parse_failure("parse nested missing close", "x(y", 3);
    test_parse_failure("parse unexpected close", "x)", 1);
    test_parse_failure("parse extra nested close", "x(y))", 4);
    test_parse_failure("parse uppercase symbol", "Q", 0);
    test_parse_failure("parse numeric symbol", "x2", 1);
    test_parse_failure("parse punctuation symbol", "@", 0);
    test_parse_failure("parse UTF-8 symbol", "\xCE\xBB", 0);
    test_parse_failure("parse escaped word opener only", "\\\"", 2);
    test_parse_failure("parse empty escaped word", "\\\"\\\"", 2);
    test_parse_failure(
        "parse unterminated escaped word", "\\\"word", 6);
    test_parse_failure(
        "parse unterminated escaped word after atom", "x\\\"word", 7);
    test_parse_failure(
        "parse invalid atom after escaped word", "\\\"word\\\"@", 8);
    test_parse_failure(
        "parse invalid escape inside word", "\\\"a\\q\\\"", 3);
    {
        char const source[] = {'\\', '"', 'a', '\\'};
        test_parse_failure(
            "parse dangling backslash inside word",
            std::string_view(source, sizeof source), 4);
    }
    test_parse_failure("parse dangling backslash", "\\", 1);
    test_parse_failure("parse invalid backslash escape", "\\q", 0);
    test_parse_failure("parse bare quote", "\"word\"", 0);
    test_parse_failure("set requires a basis name", "set = I", 4);
    test_parse_failure("set requires an equals sign", "set NoEq I", 9);
    test_parse_failure("set requires an expression", "set Empty = \t", 13);
    test_parse_failure("set rejects an overlong basis name",
                       "set Eight888=I", 11);
    test_parse_failure("set rejects an invalid basis name",
                       "set (Bad=I", 4);
    test_parse_failure("set name ends at a left parenthesis",
                       "set Qparen(I=K", 10);
    test_parse_failure("parenthesized set name is not registered",
                       "Qparen", 0);
    test_parse_failure("set rejects an invalid expression",
                       "set Qbad = K@", 12);
    test_parse_failure("failed set does not register its name", "Qbad", 0);
    test_parse_failure("set rejects a trailing close parenthesis",
                       "set Qtail=I)", 11);
    test_parse_failure("trailing set error does not register its name",
                       "Qtail", 0);
    test("basis name beginning with single backslash rejected",
         [] {
             try {
                 static_cast<void>(basis("\\bad", 1, I));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("basis name beginning with double backslash rejected",
         [] {
             try {
                 static_cast<void>(basis("\\\\bad", 1, I));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    test("basis name beginning with double quote rejected",
         [] {
             try {
                 static_cast<void>(basis("\"bad", 1, I));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");

    test("parse eval", [&] { parse_eval("K(Ix)y"); }, "x\n");
    test("parse eval set only registers its definition",
         [&] { parse_eval("set EvalK=K"); }, "");
    test("parse eval uses a silently registered set basis",
         [&] { parse_eval("EvalK x y"); }, "x\n");
    test("parse eval does not mistake setx for a definition",
         [&] { parse_eval("setx"); }, "setx\n");
    test("parse eval uses a set basis",
         [&] { parse_eval("SetK x y"); }, "x\n");
    test("parse eval treats q as a symbol", [&] { parse_eval("q"); }, "q\n");
    test("read parse eval",
         [&] {
             std::istringstream input("K(Ix)y\nignored\n");
             std::ostringstream output;
             read_parse_eval(input, output);

             std::string remaining;
             std::getline(input, remaining);
             std::cout << output.str() << remaining;
         },
         "x\nignored");
    test("read parse eval defaults to standard streams",
         [&] {
             std::istringstream input("SKIx\n");
             input_redirect redirect(input);
             read_parse_eval();
         },
         "x\n");
    test("read parse eval does nothing at end of input",
         [&] {
             std::istringstream input;
             read_parse_eval(input);
         },
         "");
    test("read parse eval propagates parse errors",
         [&] {
             std::istringstream input("@\n");
             try {
                 read_parse_eval(input);
             } catch (parse_error const& error) {
                 std::cout << error.position();
             }
         },
         "0");
    test("read parse eval preserves set definitions between lines",
         [] {
             std::istringstream input(
                 "set ReplK=K\n"
                 "ReplK x y\n");
             std::ostringstream output;
             read_parse_eval(input, output);
             read_parse_eval(input, output);
             std::cout << output.str();
         },
         "x\n");
    test("read parse eval uses its input to resume",
         [&] {
             static_cast<void>(
                 basis("Eint", 1, interrupting_identity()));
             std::istringstream input("Eint(Ix)\n\n");
             std::ostringstream output;
             read_parse_eval(input, output);
             std::cout << output.str();
         },
         "Ix\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "x\n");

    test("parse and step",
         [&] { parse_and_step("SKIx"); },
         "Kx(Ix)\n"
         "x\n");
    test("parse and step set only registers its definition",
         [&] { parse_and_step("set StepI=I"); }, "");
    test("parse and step uses a silently registered set basis",
         [&] { parse_and_step("StepI x"); },
         "Ix\n"
         "x\n");
    test("parse and step custom streams",
         [&] {
             std::istringstream input;
             std::ostringstream output;
             parse_and_step("K(Ix)y", output, input);
             std::cout << output.str();
         },
         "Ix\n"
         "x\n");
    test("parse and step defaults to std::cin",
         [&] {
             std::istringstream input("\n");
             input_redirect redirect(input);
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             parse_and_step("SKIx", output);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "Kx(Ix)\n"
         "x\n");
    const auto quoted_ski_x = quote(S)(K)(I)(x);
    test("quote SKIx", quoted_ski_x, "SKIx");
    test("single step I", single_step(quote(I)(x)), "x");
    test("single step I with trailing argument", single_step(quote(I)(x)(y)),
         "xy");
    test("single step undersaturated I", single_step(quote(I)), "I");
    test("single step undersaturated K", single_step(quote(K)(x)), "Kx");
    test("single step K", single_step(quote(K)(x)(y)), "x");
    test("single step K with trailing argument", single_step(quote(K)(x)(y)(z)),
         "xz");
    test("single step undersaturated S", single_step(quote(S)(x)(y)), "Sxy");
    test("single step S", single_step(quote(S)(x)(y)(z)), "xz(yz)");
    test("single step S with trailing argument",
         single_step(quote(S)(x)(y)(z)(w)), "xz(yz)w");
    test("single step SKIx", single_step(quoted_ski_x), "Kx(Ix)");
    test("two steps SKIx", single_step(single_step(quoted_ski_x)), "x");
    test("single step native SKI partial", single_step(quote(S(K)(I))(x)),
         "Kx(Ix)");
    test("single step undersaturated Y", single_step(quote(Y)), "Y");
    test("single step Y", single_step(quote(Y)(x)), "x<deferred Y(x)>");
    test("single step Y with trailing argument", single_step(quote(Y)(x)(w)),
         "x<deferred Y(x)>w");
    const auto quoted_y_i = single_step(quote(Y)(I));
    test("single step YI", quoted_y_i, "I<deferred Y(I)>");
    test("two steps YI", single_step(quoted_y_i), "<deferred Y(I)>");
    test("three steps YI", single_step(single_step(quoted_y_i)),
         "I<deferred Y(I)>");
    test("single step symbol head", single_step(quote(x)(y)(z)), "xyz");
    test("single step reduces inside head normal form",
         single_step(quote(x)(quote(I)(y))), "xy");
    test("single step gives head reduction priority",
         single_step(quote(K)(quote(x)(quote(I)(u)))(v)), "x(Iu)");
    test("single step reduces outer nested redex first",
         single_step(quote(x)(quote(K)(quote(I)(u))(v))), "x(Iu)");
    test("single step enters undersaturated combinator argument",
         single_step(quote(K)(quote(I)(u))), "Ku");
    const auto nested_y_step =
        single_step(quote(x)(quote(Y)(u)));
    test("single step expands nested Y", nested_y_step,
         "x(u<deferred Y(u)>)");
    test("single step expands nested deferred Y",
         single_step(nested_y_step),
         "x(u(u<deferred Y(u)>))");
    test("single step contracts nested basis",
         single_step(quote(x)(quote(M)(u))), "x(uu)");
    test("basis step exposes nested basis definition",
         single_step(quote(x)(quote(M)(u)), true), "x(SIIu)");
    test("single step expands zero-arity basis without arguments",
         single_step(quote(zero_arity_basis)), "K");
    test("single step expands zero-arity basis with trailing arguments",
         single_step(quote(zero_arity_basis)(x)(y)(z)), "Kxyz");
    test("basis step leaves zero-arity basis behavior unchanged",
         single_step(quote(zero_arity_basis)(x)(y)(z), true), "Kxyz");
    test("two steps reduce zero-arity basis with trailing arguments",
         single_step(single_step(
             quote(zero_arity_basis)(x)(y)(z))), "xz");
    test("quoted SK operand prints I", quote(x)(quote(S)(K)(y)), "xI");
    test("quoted native SK operand prints I", quote(x(S(K)(y))), "xI");
    test("quote preserves nested quoted application", quote(x(quote(I)(y))),
         "x(Iy)");
    test("single step preserves nested quoted reduction",
         single_step(single_step(quote(K(quote(I)(x)))(y))), "x");
    auto quoted_self_head = quote(x);
    test("quote self application",
         quoted_self_head(std::move(quoted_self_head)), "xx");
    test("single step shares move-only S argument",
         single_step(quote(S)(x)(y)(move_only_named{})),
         "x<move-only>(y<move-only>)");
    test("single step undersaturated M", single_step(quote(M)), "M");
    test("basis step leaves undersaturated M unchanged",
         single_step(quote(M), true), "M");
    test("single step contracts M", single_step(quote(M)(x)), "xx");
    test("explicit default basis mode contracts M",
         single_step(quote(M)(x), false), "xx");
    test("basis step exposes M definition",
         single_step(quote(M)(x), true), "SIIx");
    test("second basis step reduces exposed M definition",
         single_step(single_step(quote(M)(x), true), true), "Ix(Ix)");
    test("single step contracts native M application",
         single_step(quote(M(x))), "xx");
    test("single step M with trailing argument",
         single_step(quote(M)(x)(y)), "xxy");
    test("basis step exposes M definition with trailing argument",
         single_step(quote(M)(x)(y), true), "SIIxy");
    test("single step preserves reducible basis argument",
         single_step(quote(M)(quote(I)(u))), "Iu(Iu)");
    test("single step preserves reducible trailing argument",
         single_step(quote(M)(x)(quote(I)(u))), "xx(Iu)");
    test("second default step leaves contracted M unchanged",
         single_step(single_step(quote(M)(x))), "xx");
    test("single step undersaturated T", single_step(quote(T)(x)), "Tx");
    test("basis step leaves undersaturated T unchanged",
         single_step(quote(T)(x), true), "Tx");
    test("single step contracts T", single_step(quote(T)(x)(y)), "yx");
    test("basis step exposes T definition",
         single_step(quote(T)(x)(y), true), "S(K(SI))Kxy");
    test("single step contracts basis containing another basis",
         single_step(quote(C)(x)(y)(z)), "xzy");
    const auto fixed_point_basis = basis("FY", 1, Y);
    test("single step stops at deferred Y in a basis result",
         single_step(quote(fixed_point_basis)(x)),
         "x<deferred Y(x)>");
    test("basis step exposes fixed-point basis definition",
         single_step(quote(fixed_point_basis)(x), true), "Yx");
    const auto quoted_move_only_basis = [] {
        auto move_only_basis = basis("QB", 1, K(move_only_named{}));
        return quote(std::move(move_only_basis));
    }();
    test("single step contracts move-only basis",
         single_step(quoted_move_only_basis(x)), "<move-only>");
    test("basis step exposes move-only basis definition",
         single_step(quoted_move_only_basis(x), true), "K<move-only>x");
    test("second default step leaves contracted move-only basis",
         single_step(single_step(quoted_move_only_basis(x))), "<move-only>");
    test("eval prints only reduced expression",
         [&] { eval(quoted_ski_x); },
         "x\n");
    test("eval reduces inside head normal form",
         [&] { eval(quote(x)(quote(I)(y))); },
         "xy\n");
    test("eval reduces nested S K I left to right",
         [&] {
             eval(quote(x)(quote(I)(u))(quote(K)(v)(w))(
                 quote(S)(u)(v)(w)));
         },
         "xuv(uw(vw))\n");
    test("eval expands terminating nested Y",
         [&] { eval(quote(x)(quote(Y)(quote(K)(u)))); },
         "xu\n");
    test("eval expands nested basis",
         [&] { eval(quote(x)(quote(M)(u))); },
         "x(uu)\n");
    test("eval compares structure rather than printed text",
         [&] { eval(quote(basis("K", 1, K))(x)); },
         "Kx\n");
    test("eval supports a custom output stream",
         [&] {
             std::ostringstream output;
             eval(quoted_ski_x, output);
             std::cout << output.str();
         },
         "x\n");
    test("eval accepts parsed expressions",
         [&] { eval(parse("SKIx")); },
         "x\n");
    test("eval prints current expression on SIGINT and restores handler",
         [&] {
             auto const previous =
                 std::signal(SIGINT, test_sigint_handler);
             if (previous == SIG_ERR) {
                 std::cout << "setup failed";
                 return;
             }

             test_sigint_received = 0;
             std::istringstream input("q\n");
             std::ostringstream output;
             eval(interrupting_expression(), output, input);

             std::ostringstream reset_output;
             eval(quote(x), reset_output);

             auto const restored = std::signal(SIGINT, previous);
             std::cout
                 << (output.str() ==
                             "current\n"
                             "Interrupted. Press Enter to resume; type q or Q "
                             "then Enter to quit.\n"
                         ? "current"
                         : "wrong") << '/'
                 << (reset_output.str() == "x\n" ? "reset" : "stale") << '/'
                 << (restored == test_sigint_handler
                         ? "restored"
                         : "changed") << '/'
                 << (test_sigint_received == 0
                         ? "not-forwarded"
                         : "forwarded");
         },
         "current/reset/restored/not-forwarded");
    test("eval prints a completed step when SIGINT interrupts reduction",
         [&] {
             auto const previous =
                 std::signal(SIGINT, test_sigint_handler);
             if (previous == SIG_ERR) {
                 std::cout << "setup failed";
                 return;
             }

             test_sigint_received = 0;
             std::istringstream input("q\n");
             std::ostringstream output;
             eval(interrupting_identity_expression(quote(x)), output, input);

             auto const restored = std::signal(SIGINT, previous);
             std::cout << output.str();
             if (restored != test_sigint_handler) {
                 std::cout << "handler changed";
             }
             if (test_sigint_received != 0) {
                 std::cout << "signal forwarded";
             }
         },
         "x\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("eval resumes after SIGINT",
         [&] {
             auto const previous =
                 std::signal(SIGINT, test_sigint_handler);
             if (previous == SIG_ERR) {
                 std::cout << "setup failed";
                 return;
             }

             test_sigint_received = 0;
             std::istringstream input("\n");
             std::ostringstream output;
             eval(interrupting_identity_expression(quoted_ski_x), output,
                  input);
             auto const restored = std::signal(SIGINT, previous);
             std::cout << output.str();
             if (restored != test_sigint_handler) {
                 std::cout << "handler changed";
             }
             if (test_sigint_received != 0) {
                 std::cout << "signal forwarded";
             }
         },
         "SKIx\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "x\n");
    test("eval does not repeat a completed interrupted normal form",
         [&] {
             std::istringstream input("\n");
             std::ostringstream output;
             eval(interrupting_identity_expression(quote(x)), output, input);
             std::cout << output.str();
         },
         "x\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("eval accepts uppercase quit after SIGINT",
         [&] {
             std::istringstream input("Q\n");
             std::ostringstream output;
             eval(interrupting_identity_expression(quoted_ski_x), output,
                  input);
             std::cout << output.str();
         },
         "SKIx\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("eval quits at end of input after SIGINT",
         [&] {
             std::istringstream input;
             std::ostringstream output;
             eval(interrupting_identity_expression(quoted_ski_x), output,
                  input);
             std::cout << output.str();
         },
         "SKIx\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("eval defaults to standard input after SIGINT",
         [&] {
             std::istringstream input("\n");
             input_redirect redirect(input);
             std::ostringstream output;
             eval(interrupting_identity_expression(quoted_ski_x), output);
             std::cout << output.str();
         },
         "SKIx\n"
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "x\n");
    test("single step loop",
         [&] {
             std::istringstream input("\n\n\n");
             single_step_loop(quoted_ski_x, input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "SKIx\n"
         "Kx(Ix)\n"
         "x\n");
    test("single step loop quit",
         [&] {
             std::istringstream input("q\n");
             single_step_loop(quoted_ski_x, input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "SKIx\n");
    test("single step loop ignores other input",
         [&] {
             std::istringstream input("not a command\n\nQ\n");
             single_step_loop(quoted_ski_x, input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "SKIx\n"
         "Kx(Ix)\n");
    test("single step loop reduces inside head normal form",
         [&] {
             std::istringstream input("\n\n");
             single_step_loop(quote(x)(quote(I)(y)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Iy)\n"
         "xy\n");
    test("single step loop reduces nested S K I left to right",
         [&] {
             std::istringstream input("\n\n\n\n");
             single_step_loop(
                 quote(x)(quote(I)(u))(quote(K)(v)(w))(
                     quote(S)(u)(v)(w)),
                 input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Iu)(Kvw)(Suvw)\n"
         "xu(Kvw)(Suvw)\n"
         "xuv(Suvw)\n"
         "xuv(uw(vw))\n");
    test("single step loop reduces outer nested redex first",
         [&] {
             std::istringstream input("\n\n\n");
             single_step_loop(
                 quote(x)(quote(K)(quote(I)(u))(v)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(K(Iu)v)\n"
         "x(Iu)\n"
         "xu\n");
    test("single step loop skips undersaturated nested combinator",
         [&] {
             std::istringstream input("\n\n");
             single_step_loop(
                 quote(x)(quote(K)(u))(quote(I)(v)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Ku)(Iv)\n"
         "x(Ku)v\n");
    test("single step loop enters undersaturated combinator argument",
         [&] {
             std::istringstream input("\n\n");
             single_step_loop(quote(K)(quote(I)(u)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "K(Iu)\n"
         "Ku\n");
    test("single step loop expands nested Y",
         [&] {
             std::istringstream input("\nq\n");
             single_step_loop(quote(x)(quote(Y)(u)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Yu)\n"
         "x(u<deferred Y(u)>)\n");
    test("single step loop contracts nested basis",
         [&] {
             std::istringstream input("\nq\n");
             single_step_loop(quote(x)(quote(M)(u)), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Mu)\n"
         "x(uu)\n");
    test("single step loop exposes nested basis with basis step",
         [&] {
             std::istringstream input("\nq\n");
             single_step_loop(
                 quote(x)(quote(M)(u)), input, std::cout, true);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "x(Mu)\n"
         "x(SIIu)\n");
    test("single step loop transitions from head to nested reduction",
         [&] {
             std::istringstream input("\n\n\n");
             single_step_loop(
                 quote(K)(quote(x)(quote(I)(u)))(v), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "K(x(Iu))v\n"
         "x(Iu)\n"
         "xu\n");
    test("single step loop stops without nested redex",
         [&] {
             std::istringstream input("\n");
             single_step_loop(quote(x)(y), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "xy\n");
    test("single step loop compares structure, not output",
         [&] {
             std::istringstream input("\n\n");
             single_step_loop(quote(basis("K", 1, K))(x), input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "Kx\n"
         "Kx\n");
    test("single step run",
         [&] { single_step_run(quoted_ski_x); },
         "Kx(Ix)\n"
         "x\n");
    test("single step run omits an unreduced expression",
         [&] { single_step_run(quote(x)); },
         "");
    test("single step run reduces inside head normal form",
         [&] { single_step_run(quote(x)(quote(I)(y))); },
         "xy\n");
    test("single step run reduces nested S K I left to right",
         [&] {
             single_step_run(
                 quote(x)(quote(I)(u))(quote(K)(v)(w))(
                     quote(S)(u)(v)(w)));
         },
         "xu(Kvw)(Suvw)\n"
         "xuv(Suvw)\n"
         "xuv(uw(vw))\n");
    test("single step run expands terminating nested Y",
         [&] {
             single_step_run(quote(x)(quote(Y)(quote(K)(u))));
         },
         "x(Ku<deferred Y(Ku)>)\n"
         "xu\n");
    test("single step run contracts nested basis",
         [&] { single_step_run(quote(x)(quote(M)(u))); },
         "x(uu)\n");
    test("single step run exposes nested basis with basis step",
         [&] {
             std::istringstream input;
             single_step_run(
                 quote(x)(quote(M)(u)), std::cout, input, true);
         },
         "x(SIIu)\n"
         "x(Iu(Iu))\n"
         "x(u(Iu))\n"
         "x(uu)\n");
    test("single step run compares structure, not output",
         [&] { single_step_run(quote(basis("K", 1, K))(x)); },
         "Kx\n");
    test("color step prints before and after each call",
         [&] {
             auto expression = color_step(quoted_ski_x);
             static_cast<void>(color_step(std::move(expression)));
         },
         std::string{"  S"} +
             red_argument("K") +
             green_argument("I") +
             blue_argument("x") +
             "\n->" +
             red_argument("K") +
             blue_argument("x") +
             "(" +
             green_argument("I") +
             blue_argument("x") +
             ")\n  K" +
             red_argument("x") +
             green_argument("(Ix)") +
             "\n->" +
             red_argument("x") +
             "\n");
    test("color step prints an unchanged normal form",
         [&] { static_cast<void>(color_step(quote(x))); },
         "  x\n"
         "->x\n");
    test("color step colors I argument",
         [&] { static_cast<void>(color_step(quote(I)(x))); },
         std::string{"  I"} +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             "\n");
    test("color step colors K arguments and preserves trailing operand",
         [&] {
             static_cast<void>(
                 color_step(quote(K)(x)(y)(w)));
         },
         std::string{"  K"} +
             red_argument("x") +
             green_argument("y") +
             "w\n->" +
             red_argument("x") +
             "w\n");
    test("color step carries S argument colors through reduction",
         [&] {
             static_cast<void>(
                 color_step(quote(S)(x)(y)(z)));
         },
         std::string{"  S"} +
             red_argument("x") +
             green_argument("y") +
             blue_argument("z") +
             "\n->" +
             red_argument("x") +
             blue_argument("z") +
             "(" +
             green_argument("y") +
             blue_argument("z") +
             ")\n");
    test("color step carries Y argument color into deferred Y",
         [&] { static_cast<void>(color_step(quote(Y)(x))); },
         std::string{"  Y"} +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             "&lt;deferred Y(" +
             red_argument("x") +
             ")&gt;\n");
    test("color step carries deferred Y generator color",
         [&] {
             static_cast<void>(
                 color_step(single_step(quote(Y)(x))));
         },
         std::string{"  x&lt;deferred Y("} +
             red_argument("x") +
             ")&gt;\n->x(" +
             red_argument("x") +
             "&lt;deferred Y(" +
             red_argument("x") +
             ")&gt;)\n");
    test("color step colors only the selected nested redex",
         [&] {
             static_cast<void>(
                 color_step(quote(q)(quote(I)(x))));
         },
         std::string{"  q(I"} +
             red_argument("x") +
             ")\n->q" +
             red_argument("x") +
             "\n");
    test("color step carries unary basis argument through duplication",
         [&] { static_cast<void>(color_step(quote(M)(x))); },
         std::string{"  M"} +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             red_argument("x") +
             "\n");
    test("color step carries binary basis arguments through reordering",
         [&] { static_cast<void>(color_step(quote(T)(x)(y))); },
         std::string{"  T"} +
             red_argument("x") +
             green_argument("y") +
             "\n->" +
             green_argument("y") +
             red_argument("x") +
             "\n");
    test("color step carries ternary basis arguments through reordering",
         [&] {
             static_cast<void>(
                 color_step(quote(C)(x)(y)(z)));
         },
         std::string{"  C"} +
             red_argument("x") +
             green_argument("y") +
             blue_argument("z") +
             "\n->" +
             red_argument("x") +
             blue_argument("z") +
             green_argument("y") +
             "\n");
    test("color step carries fourth basis argument color",
         [&] {
             static_cast<void>(
                 color_step(quote(D)(u)(v)(w)(x)));
         },
         std::string{"  D"} +
             red_argument("u") +
             green_argument("v") +
             blue_argument("w") +
             dark_orange_argument("x") +
             "\n->" +
             red_argument("u") +
             green_argument("v") +
             "(" +
             blue_argument("w") +
             dark_orange_argument("x") +
             ")\n");
    test("color step colored spacing precedes multicharacter basis",
         [&] {
             static_cast<void>(
                 color_step(quote(I)(x)(Cstar)));
         },
         std::string{"  I"} +
             red_argument("x") +
             "Cstar\n->" +
             red_argument("x") +
             "Cstar\n");
    test("color step compares structure rather than output",
         [&] {
             static_cast<void>(
                 color_step(quote(basis("K", 1, K))(x)));
         },
         std::string{"  K"} +
             red_argument("x") +
             "\n->K" +
             red_argument("x") +
             "\n");
    test("color step HTML-escapes expression text",
         [&] {
             static_cast<void>(
                 color_step(quote(I)(std::string{"<&>\"'"})));
         },
         std::string{"  I"} +
             red_argument("&lt;&amp;&gt;&quot;&#39;") +
             "\n->" +
             red_argument("&lt;&amp;&gt;&quot;&#39;") +
             "\n");
    test("color step preserves stream formatting",
         [&] {
             std::ostringstream output;
             output << std::hex;
             static_cast<void>(
                 color_step(quote(I)(255), output));
             std::cout << output.str();
         },
         std::string{"  I"} +
             red_argument("&lt;ff&gt;") +
             "\n->" +
             red_argument("&lt;ff&gt;") +
             "\n");
    test("color step leaves a failed output stream untouched",
         [&] {
             std::ostringstream output;
             output.setstate(std::ios_base::failbit);
             auto result = color_step(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("color step accepts an output stream without a buffer",
         [&] {
             std::ostream output(nullptr);
             auto result = color_step(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("color step does not color zero-arity basis expansion",
         [&] {
             static_cast<void>(
                 color_step(quote(zero_arity_basis)));
         },
         "  Qzero\n"
         "->K\n");
    test("color step supports a custom output stream",
         [&] {
             std::ostringstream output;
             auto expression = color_step(quoted_ski_x, output);
             std::cout << output.str() << "returned: ";
             expression();
         },
         std::string{"  S"} +
             red_argument("K") +
             green_argument("I") +
             blue_argument("x") +
             "\n->" +
             red_argument("K") +
             blue_argument("x") +
             "(" +
             green_argument("I") +
             blue_argument("x") +
             ")\nreturned: Kx(Ix)");
    test("color step forwards basis step",
         [&] {
             static_cast<void>(color_step(quote(M)(x), true));
         },
         std::string{"  M"} +
             red_argument("x") +
             "\n->SII" +
             red_argument("x") +
             "\n");
    test("single step run resumes after SIGINT",
         [&] {
             std::istringstream input("\n");
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             single_step_run(quoted_ski_x, output, input);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "Kx(Ix)\n"
         "x\n");
    test("single step run defaults to std::cin",
         [&] {
             std::istringstream input("\n");
             input_redirect redirect(input);
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             single_step_run(quoted_ski_x, output);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "Kx(Ix)\n"
         "x\n");
    test("single step run quits after SIGINT",
         [&] {
             std::istringstream input("q\n");
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             single_step_run(quoted_ski_x, output, input);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("single step run accepts uppercase quit",
         [&] {
             std::istringstream input("Q\n");
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             single_step_run(quoted_ski_x, output, input);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n");
    test("single step run restores SIGINT handler",
         [&] {
             auto const previous = std::signal(SIGINT, test_sigint_handler);
             if (previous == SIG_ERR) {
                 std::cout << "setup failed";
                 return;
             }

             std::istringstream input;
             std::ostringstream output;
             single_step_run(quote(x), output, input);

             auto const restored = std::signal(SIGINT, previous);
             std::cout << (restored == test_sigint_handler
                               ? "restored"
                               : "changed");
         },
         "restored");
    test("seven-character basis", seven_character_basis, "1234567");
    test("zero-arity basis without arguments", zero_arity_basis, "Qzero");
    test("zero-arity basis with one argument",
         zero_arity_basis(x), "Kx");
    test("zero-arity basis with enough arguments",
         zero_arity_basis(x)(y), "x");
    test("zero-arity basis preserves trailing arguments",
         zero_arity_basis(x)(y)(z), "xz");
    test("copied basis name", copied_name_basis, "Alias");
    test("deferred basis", deferred_basis, "D");
    test("null-terminated basis", null_terminated_basis, "Trimmed");
    test("multi-character basis after primitive",
         K(copied_name_basis), "K Alias");
    test("multi-character basis after symbol",
         x(copied_name_basis), "x Alias");
    test("multi-character basis after quoted basis",
         quote(M)(copied_name_basis), "M Alias");
    test("adjacent multi-character quoted bases",
         quote(copied_name_basis)(null_terminated_basis),
         "Alias Trimmed");
    test("multi-character basis immediately after left parenthesis",
         x(Cstar(y)), "x(Cstar y)");
    test("multi-character basis after right parenthesis",
         x(Cstar(y))(copied_name_basis), "x(Cstar y)Alias");
    test("native token after multi-character basis",
         Cstar(y), "Cstar y");
    test("quoted token after multi-character basis",
         quote(Cstar)(y), "Cstar y");
    test("parsed token after multi-character basis",
         parse("Cstar y"), "Cstar y");
    test("parsed nested token after multi-character basis",
         parse("x(Cstar y)"), "x(Cstar y)");
    test("left parenthesis after multi-character basis",
         Cstar(y(z)), "Cstar(yz)");
    test("quoted left parenthesis after multi-character basis",
         quote(Cstar)(quote(y)(z)), "Cstar(yz)");
    test("parsed left parenthesis after multi-character basis",
         parse("Cstar(yz)"), "Cstar(yz)");
    test("nested multi-character basis application",
         x(Cstar(y(z))), "x(Cstar(yz))");
    test("right parenthesis follows multi-character basis directly",
         x(y(Cstar)), "x(y Cstar)");
    test("right parenthesis clears previous basis token",
         x(y(Cstar))(z), "x(y Cstar)z");
    test("token follows trailing multi-character basis operand",
         x(copied_name_basis)(y), "x Alias y");
    test("left parenthesis follows trailing multi-character basis operand",
         x(copied_name_basis)(y(z)), "x Alias(yz)");
    test("multi-character basis follows right parenthesis directly",
         x(y(z))(Cstar), "x(yz)Cstar");
    test("only first argument follows multi-character basis directly",
         Cstar(y)(z), "Cstar yz");
    test("adjacent multi-character bases use one space",
         quote(Cstar)(Vstar)(x), "Cstar Vstar x");
    test("parsed adjacent multi-character bases use one space",
         parse("Cstar Vstar x"), "Cstar Vstar x");
    test("deferred Y token after multi-character basis",
         quote(Cstar)(single_step(quoted_y_i)),
         "Cstar <deferred Y(I)>");
    test("opaque token after multi-character basis",
         quote(Cstar)(operand_named_value{}), "Cstar <operand>");
    test("single-character basis does not separate following token",
         quote(T)(x), "Tx");
    test("nested single-character basis remains compact",
         x(T(y)), "x(Ty)");
    test("multi-character basis after an existing space",
         [&] {
             std::cout << "x ";
             copied_name_basis();
         },
         "x Alias");
    test("single-character basis after primitive", K(M), "KM");
    test("single-character basis after symbol", x(M), "xM");
    test("opaque print_as_operand_to remains opaque",
         x(operand_named_value{}), "x<operand>");
    test("spaced primitive and lowercase basis", parse("S foo"), "S foo");
    test("longer basis remains atomic", parse("Sfoo"), "Sfoo");
    test("multi-character basis separates only its next token",
         quote(S)(parse("foo"))(x)(y), "S foo xy");
    test("multi-character basis output round trips through parser",
         [&] {
             auto expression = quote(S)(parse("foo"))(x)(y);
             std::ostringstream rendered;
             expression.print_to(rendered);

             auto reduced = parse(rendered.str());
             reduced = single_step(reduced);
             reduced = single_step(reduced);
             reduced = single_step(reduced);
             reduced();
         },
         "y(xy)");
    test("x", (x), "x");
    test("xy", (x)(y), "xy");
    test("xyz", (x)(y)(z), "xyz");
    test("(xyz)w", (x(y)(z))(w), "xyzw");
    test("xy(zuv)w", (x)(y)(z(u)(v))(w), "xy(zuv)w");
    test("y((zu))w", (y)((z(u)))(w), "y(zu)w");
    test("((zu))", ((z(u))), "zu");
    test("((zu))w", ((z(u)))(w), "zuw");
    test("(zu)", (z(u)), "zu");
    test("(zu)w", (z(u))(w), "zuw");
    test("I", (I), "I");
    test("Ixyz [identity combinator]", (I)(x)(y)(z), "xyz");
    test("I(xyz)", (I)(x(y)(z)), "xyz");
    test("I(xy)z", (I)(x(y))(z), "xyz");
    test("xIy", (x)(I)(y), "xIy");
    test("xI(yz)", (x)(I)(y(z)), "xI(yz)");
    test("II", (I)(I), "I");
    test("IIxyz", (I)(I)(x)(y)(z), "xyz");
    test("IIIxyz", (I)(I)(I)(x)(y)(z), "xyz");
    test("K", (K), "K");
    test("(K)", ((K)), "K");
    test("Kx", (K)(x), "Kx");
    test("Kxyw [deletes y]", (K)(x)(y)(w), "xw");
    test("Ky(xy)w", (K)(y)(x(y))(w), "yw");
    test("KIxyw", (K)(I)(x)(y)(w), "yw");
    test("x(Ky)w", (x)(K(y))(w), "x(Ky)w");
    test("I(Ky)", (I)(K(y)), "Ky");
    test("I(Ky)w", (I)(K(y))(w), "y");
    test("S", (S), "S");
    test("Sx", (S)(x), "Sx");
    test("Sxy", (S)(x)(y), "Sxy");
    test("Sxyzw [combines (yz) after xz]", (S)(x)(y)(z)(w), "xz(yz)w");
    test("SIyzw", (S)(I)(y)(z)(w), "z(yz)w");
    test("S(SKx)yzw", (S)(S(K)(x))(y)(z)(w), "z(yz)w");
    test("S(SKy)(SK)(zu)w", (S)(S(K)(y))(S(K))(z(u))(w), "zuIw");
    test("S(SK)yzw", (S)(S(K))(y)(z)(w), "yzw");
    test("S(SK)(Ky)zw", (S)(S(K))(K(y))(z)(w), "yw");
    test("S(SK)(zu)yw", (S)(S(K))(z(u))(y)(w), "zuyw");
    test("S(SK)yI", (S)(S(K))(y)(I), "yI");
    test("S(SK)y(SKz)", (S)(S(K))(y)(S(K)(z)), "yI");
    test("S(KI)yzw", (S)(K(I))(y)(z)(w), "yzw");
    test("S(KI)(Ky)zw", (S)(K(I))(K(y))(z)(w), "yw");
    test("S(KI)(zu)yw", (S)(K(I))(z(u))(y)(w), "zuyw");
    test("S(K(SKx))yzw", (S)(K(S(K)(x)))(y)(z)(w), "yzw");
    test("S(K(SKx))(Ky)zw", (S)(K(S(K)(x)))(K(y))(z)(w), "yw");
    test("S(K(SKx))(zu)yw", (S)(K(S(K)(x)))(z(u))(y)(w), "zuyw");
    test("SxIzw", (S)(x)(I)(z)(w), "xzzw");
    test("Sx(SKy)zw", (S)(x)(S(K)(y))(z)(w), "xzzw");
    test("Sxy(SKz)w", (S)(x)(y)(S(K)(z))(w), "xI(yI)w");
    test("SI(SKx)zw", (S)(I)(S(K)(x))(z)(w), "zzw");
    test("SKyzw", (S)(K)(y)(z)(w), "zw");
    test("SKKzw", (S)(K)(K)(z)(w), "zw");
    test("Sx(KS)zw", (S)(x)(K(S))(z)(w), "xzSw");
    test("Sx(SK)zw", (S)(x)(S(K))(z)(w), "xzIw");
    test("SI(SK)zw", (S)(I)(S(K))(z)(w), "zIw");
    test("SI(SK)(zu)w", (S)(I)(S(K))(z(u))(w), "zuIw");
    test("SI(SK)(SKx)w", (S)(I)(S(K))(S(K)(x))(w), "w");
    test("SxKyw", (S)(x)(K)(y)(w), "xy(Ky)w");
    test("Sxy(zy)w", (S)(x)(y)(z(y))(w), "x(zy)(y(zy))w");
    test("SIy(zy)w", (S)(I)(y)(z(y))(w), "zy(y(zy))w");
    test("Sxy(My)w", (S)(x)(y)(M(y))(w), "x(yy)(y(yy))w");
    test("SIy((zu))w", (S)(I)(y)((z(u)))(w), "zu(y(zu))w");
    test("Sy((zu))xw", (S)(y)((z(u)))(x)(w), "yx(zux)w");
    test("Sy(K(zu))xw", (S)(y)(K(z(u)))(x)(w), "yx(zu)w");
    test("Sx(y)zw", (S)(x)(y)(z)(w), "xz(yz)w");
    test("Sx(zu)yw", (S)(x)(z(u))(y)(w), "xy(zuy)w");
    test("Sx((y))zw", (S)(x)((y))(z)(w), "xz(yz)w");
    test("Sx(Ky)zw", (S)(x)(K(y))(z)(w), "xzyw");
    test("SI(Ky)zw", (S)(I)(K(y))(z)(w), "zyw");
    test("x(Sy)w", (x)(S(y))(w), "x(Sy)w");
    test("I(Sy)w", (I)(S(y))(w), "Syw");
    test("x(Syz)w", (x)(S(y)(z))(w), "x(Syz)w");
    test("x(SKy)w", (x)(S(K)(y))(w), "xIw");
    test("I(Syz)", (I)(S(y)(z)), "Syz");
    test("I(Syz)w", (I)(S(y)(z))(w), "yw(zw)");
    test("SxI(zu)w", (S)(x)(I)(z(u))(w), "x(zu)(zu)w");
    test("Sx(SKx)(zu)w", (S)(x)(S(K)(x))(z(u))(w), "x(zu)(zu)w");
    test("Sx(Sy)zw", (S)(x)(S(y))(z)(w), "xz(Syz)w");
    test("Sx(Syz)uw", (S)(x)(S(y)(z))(u)(w), "xu(yu(zu))w");
    test("S(Sx)y(zu)", (S)(S(x))(y)(z(u)), "x(y(zu))(zu(y(zu)))");
    test("S(Sx)I(zu)", (S)(S(x))(I)(z(u)), "x(zu)(zu(zu))");
    test("S(Sx)yI", (S)(S(x))(y)(I), "x(yI)(yI)");
    test("S(Sx)(SK)(Kz)u", (S)(S(x))(S(K))(K(z))(u), "xIzu");
    test("SSy(SK)uw", (S)(S)(y)(S(K))(u)(w), "y(SK)uw");
    test("S(S(Sx)(SK))yz", S(S(S(x))(S(K)))(y)(z), "xI(zI)(yz)");
    test("S(Sx)y(SK)u", S(S(x))(y)(S(K))(u), "x(y(SK))Iu");
    test("S(Sx)y(SKz)u", S(S(x))(y)(S(K)(z))(u), "x(yI)(yI)u");
    test("S(Sx)y(Kv)", (S)(S(x))(y)(K(v)), "x(y(Kv))v");
    test("S(Sx)(SK)z", (S)(S(x))(S(K))(z), "xI(zI)");
    test("S(S(Sx))(SK)z", (S)(S(S(x)))(S(K))(z), "x(zI)(zI)");
    test("SSI(zu)y", (S)(S)(I)(z(u))(y), "zuy(zuy)");
    test("SS(SKx)(zu)y", (S)(S)(S(K)(x))(z(u))(y), "zuy(zuy)");
    test("SS(K(SKx))(zu)w", (S)(S)(K(S(K)(x)))(z(u))(w),"zuww");
    test("SYy(zu)", (S)(Y)(y)(z(u)), "zu<deferred Y(zu)>(y(zu))");
    test("Y", (Y), "Y");
    test("Yxw [combines (Yx) after x]", (Y)(x)(w), "x<deferred Y(x)>w");
    test("Yxyw", (Y)(x)(y)(w), "x<deferred Y(x)>yw");
    test("Yxyzw", (Y)(x)(y)(z)(w), "x<deferred Y(x)>yzw");
    test("x(Yy)w", (x)(Y(y))(w), "x(y<deferred Y(y)>)w");
    test("xYIw", (x)(Y)(I)(w), "xYIw");
    test("x(YI)w", (x)(Y(I))(w), "x<deferred Y(I)>w");
    test("Y(Kx)w", (Y)(K(x))(w), "xw");
    test("x(Y(Kx))w", (x)(Y(K(x)))(w), "xxw");
    test("YB", (Y)(B), "B<deferred Y(B)>");
    test("YBw", (Y)(B)(w), "B<deferred Y(B)>w");
    test("YBxy", (Y)(B)(x)(y), "B<deferred Y(B)>(xy)");
    test("YBxyz", (Y)(B)(x)(y)(z), "B<deferred Y(B)>(xyz)");
    test("YBwxyz", (Y)(B)(w)(x)(y)(z), "B<deferred Y(B)>(wxyz)");
    test("M", (M), "M");
    test("Mxw [duplicates x]", (M)(x)(w), "xxw");
    test("W", (W), "W");
    test("Wxyw [duplicates y after y]", (W)(x)(y)(w), "xyyw");
    test("B", (B), "B");
    test("Bxyzw [combines (yz) after x]", (B)(x)(y)(z)(w), "x(yz)w");
    test("O", (O), "O");
    test("Oxyw [combines (xy) after y]", (O)(x)(y)(w), "y(xy)w");
    test("T", (T), "T");
    test("Txyw [swaps x and y]", (T)(x)(y)(w), "yxw");
    test("N", (N), "N");
    test("Nxyw [duplicates x after y]", (N)(x)(y)(w), "xyxw");
    test("R", (R), "R");
    test("Rxyzw [moves x after yz (opposite of V)]", (R)(x)(y)(z)(w), "yzxw");
    test("C", (C), "C");
    test("Cxyzw [swaps y and z after x]", (C)(x)(y)(z)(w), "xzyw");
    test("P", (P), "P");
    test("Pxyzw [combines (xz) after y]", (P)(x)(y)(z)(w), "y(xz)w");
    test("V", (V), "V");
    test("Vxyzw [moves z in front of xy (opposite of R)]", (V)(x)(y)(z)(w), "zxyw");
    test("D", (D), "D");
    test("Dwxyzv [like B, but combines (yz) after wx]", (D)(w)(x)(y)(z)(v), "wx(yz)v");
    test("L", (L), "L");
    test("Lxyw [combines (yy) after x]", (L)(x)(y)(w), "x(yy)w");
    test("Z", (Z), "Z");
    test("Zxyw [combines (xy) after x]", (Z)(x)(y)(w), "x(xy)w");
    test("A", (A), "A");
    test("Axyw [combines (yx) after x]", (A)(x)(y)(w), "x(yx)w");
    test("Cstar", (Cstar), "Cstar");
    test("Cstar wxyzv [swaps y and z after wx]", (Cstar)(w)(x)(y)(z)(v), "wxzyv");
    test("Vstar", (Vstar), "Vstar");
    test("Vstar wxyzv [like V, but moves z after w and before xy]", (Vstar)(w)(x)(y)(z)(v), "wzxyv");
    test("V4", (V4), "V4");
    test("V4 wxyzv [like V, but moves z before wxy]", (V4)(w)(x)(y)(z)(v), "zwxyv");
    test("G", (G), "G");
    test("Gxw", (G)(x)(w), "xSTK(KK)(SK)w");
    test("GK", (G)(K), "SK");
    test("GB", (G)(B), "K");
    test("GD", (G)(D), "K");
    test("G2", (G2), "G2");
    test("G2 xw", (G2)(x)(w), "xSTK(KK)(SK)w");
    test("bazTest zxy", (bazTest)(z)(x)(y), "uy(z(yx))x");
    test("Hprime zxy", (Hprime)(z)(x)(y), "ySTK(KK)(SK)(z(yx))x");
    test("HxK", (H)(x)(K), "x");
    test("HxBKyz", (H)(x)(B)(K)(y)(z), "x(yz)");
    test("HwBBKxyz", (H)(w)(B)(B)(K)(x)(y)(z), "w(xyz)");
    test("HvBBBKwxyz", (H)(v)(B)(B)(B)(K)(w)(x)(y)(z), "v(wxyz)");
    test("HwDKxyz", (H)(w)(D)(K)(x)(y)(z), "wx(yz)");
    test("HvDDKwxyz", (H)(v)(D)(D)(K)(w)(x)(y)(z), "vw(xyz)");
    test("HuDDDKvwxyz", (H)(u)(D)(D)(D)(K)(v)(w)(x)(y)(z), "uv(wxyz)");

    check(printed_lambda_calls == 0);
    check(printed_deferred_evaluations == 0);
    check(symbolic_deferred_evaluations == 0);
    check(basis_deferred_evaluations == 0);
    static_assert(combdsl::detail::is_deferred_value_v<decltype(M)>);
    check(&force(M) == &force(M));

    auto preserved_expression = K(std::make_unique<int>(29));
    bool overlong_basis_rejected = false;
    try {
        static_cast<void>(basis(
            "12345678", 1, std::move(preserved_expression)));
    } catch (std::length_error const&) {
        overlong_basis_rejected = true;
    }
    check(overlong_basis_rejected);
    auto preserved_pointer = std::move(preserved_expression)(0);
    check(*preserved_pointer == 29);

    bool empty_basis_rejected = false;
    try {
        static_cast<void>(basis("", 1, I));
    } catch (std::invalid_argument const&) {
        empty_basis_rejected = true;
    }
    check(empty_basis_rejected);

    bool leading_null_basis_rejected = false;
    try {
        static_cast<void>(basis(std::string_view("\0A", 2), 1, I));
    } catch (std::invalid_argument const&) {
        leading_null_basis_rejected = true;
    }
    check(leading_null_basis_rejected);

    for (auto const invalid_name : {" Foo", "(Foo", ")Foo"}) {
        bool parser_syntax_basis_rejected = false;
        try {
            static_cast<void>(basis(invalid_name, 1, I));
        } catch (std::invalid_argument const&) {
            parser_syntax_basis_rejected = true;
        }
        check(parser_syntax_basis_rejected);
    }

    test("parsed zero-arity basis expands without arguments",
         single_step(parse("Qzero")), "K");
    test("parsed zero-arity basis expands with trailing arguments",
         single_step(parse("Qzero x y z")), "Kxyz");

    auto move_only_basis = basis("P", 1, K(std::make_unique<int>(31)));
    using move_only_basis_type = decltype(move_only_basis);
    static_assert(!std::invocable<move_only_basis_type&, int>);
    static_assert(std::invocable<move_only_basis_type&&, int>);
    auto basis_pointer = force(std::move(move_only_basis)(0));
    check(*basis_pointer == 31);

    check(I(42) == 42);
    check(K(7)("ignored") == 7);
    check(S(K)(K)(9) == I(9));
    check(S(add)(I)(21) == 42);
    check(S(K(square_number))(increment)(4) == 25);

    check(&force(I) == &force(I));
    check(&force(K) == &force(K));
    check(&force(S) == &force(S));
    combdsl::identity& forced_identity = I;
    check(&forced_identity == &force(I));

    int identity_argument_evaluations = 0;
    auto lazy_identity_argument = defer([&identity_argument_evaluations] {
        ++identity_argument_evaluations;
        return 42;
    });
    auto through_identity = I(lazy_identity_argument);
    check(identity_argument_evaluations == 0);
    check(force(through_identity) == 42);
    check(force(through_identity) == 42);
    check(identity_argument_evaluations == 1);

    int discarded_argument_evaluations = 0;
    auto discarded_argument = defer([&discarded_argument_evaluations] {
        ++discarded_argument_evaluations;
        return 99;
    });
    check(K(7)(discarded_argument) == 7);
    check(discarded_argument_evaluations == 0);

    int deferred_function_evaluations = 0;
    int deferred_argument_evaluations = 0;
    auto deferred_add = defer([&deferred_function_evaluations] {
        ++deferred_function_evaluations;
        return add;
    });
    auto deferred_identity = defer([&deferred_argument_evaluations] {
        ++deferred_argument_evaluations;
        return combdsl::identity{};
    });
    auto deferred_twice = S(deferred_add)(deferred_identity);
    check(deferred_function_evaluations == 0);
    check(deferred_argument_evaluations == 0);
    check(deferred_twice(21) == 42);
    check(deferred_twice(10) == 20);
    check(deferred_function_evaluations == 1);
    check(deferred_argument_evaluations == 1);

    int returned_function_evaluations = 0;
    auto returns_deferred_function = [&returned_function_evaluations](int left) {
        return defer([&returned_function_evaluations, left] {
            ++returned_function_evaluations;
            return [left](int right) { return left + right; };
        });
    };
    check(S(returns_deferred_function)(I)(10) == 20);
    check(returned_function_evaluations == 1);

    auto safely_forwarded_temporary =
        S(K(I))(K(owned_text{"still alive"}))(0);
    check(safely_forwarded_temporary.value == "still alive");

    int value = 10;
    I(value) = 12;
    check(value == 12);

    auto moved_through_identity = I(std::make_unique<int>(17));
    check(*moved_through_identity == 17);

    auto keep = K(owned_text{"kept"});
    check(keep(0).value == "kept");
    check(keep(false).value == "kept");

    auto move_only_constant = K(std::make_unique<int>(23));
    auto kept_pointer = std::move(move_only_constant)("discarded");
    check(*kept_pointer == 23);

    auto with_add = S(add);
    auto twice = with_add(I);
    check(twice(8) == 16);
    check(twice(11) == 22);

    int z_evaluations = 0;
    auto evaluate_z = [&z_evaluations] {
        ++z_evaluations;
        return 21;
    };
    check(S(add)(I)(evaluate_z()) == 42);
    check(z_evaluations == 1);

    int unused_z_evaluations = 0;
    auto ignore_argument = [](auto const&) { return 7; };
    auto arbitrary_function = [] {};
    check(S(K(ignore_argument))(K(arbitrary_function))(
              defer([&unused_z_evaluations] {
                  ++unused_z_evaluations;
                  return 99;
              })) == 7);
    check(unused_z_evaluations == 0);

    int needed_z_evaluations = 0;
    auto lazy_needed_z = defer([&needed_z_evaluations] {
        ++needed_z_evaluations;
        return 21;
    });
    check(S(add)(I)(lazy_needed_z) == 42);
    check(needed_z_evaluations == 1);
    check(force(lazy_needed_z) == 21);
    check(force(lazy_needed_z) == 21);
    check(needed_z_evaluations == 1);

    auto copied_lazy_z = lazy_needed_z;
    check(force(copied_lazy_z) == 21);
    check(needed_z_evaluations == 1);

    check(Y(K(square_number))(5) == 25);

    int factorial_unfoldings = 0;
    auto factorial = Y([&factorial_unfoldings](auto self) {
        ++factorial_unfoldings;
        return [self = std::move(self)](auto n) -> unsigned long long {
            return n < 2 ? 1 : n * force(self)(n - 1);
        };
    });

    check(factorial_unfoldings == 1);
    check(factorial(0) == 1);
    check(factorial(1) == 1);
    check(factorial(5) == 120);
    check(factorial(10) == 3'628'800);
    check(factorial_unfoldings == 10);
    check(factorial(10) == 3'628'800);
    check(factorial_unfoldings == 10);

    auto fibonacci = Y([](auto self) {
        return [self = std::move(self)](auto n) -> unsigned long long {
            return n < 2 ? n
                         : force(self)(n - 1) + force(self)(n - 2);
        };
    });

    check(fibonacci(0) == 0);
    check(fibonacci(1) == 1);
    check(fibonacci(10) == 55);

    auto gcd = Y([](auto self) {
        return [self = std::move(self)](auto values) -> int {
            auto const [left, right] = values;
            return right == 0 ? left
                              : force(self)(std::pair{right, left % right});
        };
    });

    check(gcd(std::pair{48, 18}) == 6);
    check(gcd(std::pair{270, 192}) == 6);

    const auto copied_factorial = factorial;
    check(copied_factorial(6) == 720);

    std::weak_ptr<int> generator_lifetime;
    {
        auto lifetime_token = std::make_shared<int>(0);
        generator_lifetime = lifetime_token;

        auto recursive_depth = Y([lifetime_token](auto self) {
            return [self = std::move(self)](auto n) -> int {
                return n == 0 ? 0 : 1 + force(self)(n - 1);
            };
        });

        lifetime_token.reset();
        check(!generator_lifetime.expired());
        check(recursive_depth(8) == 8);
    }
    check(generator_lifetime.expired());

    std::cout << tests_run << " test(s) run, "
              << test_failures << " failed\n";

    return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
