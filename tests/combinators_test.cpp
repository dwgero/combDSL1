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

#include <combdsl/combinators.hpp>
#include <combdsl/color_step_ansi.hpp>

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
#include <vector>

using combdsl::I;
using combdsl::K;
using combdsl::S;
using combdsl::Y;
using combdsl::basis;
using combdsl::color_step_html;
using combdsl::color_step_ansi;
using combdsl::defer;
using combdsl::eval;
using combdsl::evaluation_progress_callback;
using combdsl::force;
using combdsl::input_escape;
using combdsl::is_single_utf8_char;
using combdsl::parse;
using combdsl::parse_and_key_step;
using combdsl::parse_and_step;
using combdsl::parse_eval;
using combdsl::parse_error;
using combdsl::quote;
using combdsl::quoted_atomic;
using combdsl::read_parse_eval;
using combdsl::set_list;
using combdsl::single_step;
using combdsl::single_step_loop;
using combdsl::single_step_run;
using combdsl::symbol;
using combdsl::takeout;
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
using combdsl::U;
using combdsl::N;
using combdsl::R;
using combdsl::C;
using combdsl::C_star;
using combdsl::C_star_star;
using combdsl::W_star;
using combdsl::W_star_star;
using combdsl::Q;
using combdsl::Q1;
using combdsl::Q3;
using combdsl::V;
using combdsl::D;
using combdsl::L;
using combdsl::W1;
using combdsl::Z;
using combdsl::A;
using combdsl::E;
using combdsl::F;
using combdsl::G;
using combdsl::H;
using combdsl::J;

void ensure_external_basis_registered();

namespace {

const auto Cstar = basis("Cstar", 4, S(K(C)));
const auto Vstar = basis("Vstar", 4, S(K(Cstar))(C));
const auto V4 = basis("V4", 4, S(K(Vstar))(T));
const auto G1 = basis(
    "G1",
    1,
    S(S(S(S(S(I)(K(S)))(K(T)))(K(K)))(K(K(K))))(K(S(K))));
const auto Hprime = basis(
    "Hprime",
    3,
    S(K(W))(S(K(S(K(C))))(S(K(S(K(S(G1)))))(S(S(K(B))(B))(K(T))))));
const auto H1 = basis("H1", 1, Y(Hprime));
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

[[nodiscard]] combdsl::quoted_expression
repeated_identity_expression(std::size_t reductions) {
    auto expression = quote(x);
    for (std::size_t index = 0; index < reductions; ++index) {
        expression = quote(I)(expression);
    }
    return expression;
}

static_assert(std::is_same_v<decltype(I(std::declval<int&>())), int&>);
static_assert(std::is_same_v<decltype(parse("x")),
                             combdsl::quoted_expression>);
static_assert(std::is_same_v<
              decltype(takeout(quoted_atomic{x}, quote(x))),
              combdsl::quoted_expression>);
static_assert(
    combdsl::search_for_xy_subexp_candidate_count == 129'958);
static_assert(std::is_same_v<
              decltype(combdsl::search_for_xy_subexp(S(B)(T))),
              std::optional<combdsl::subexpression_search_match>>);
static_assert(
    combdsl::search_for_xyz_subexp_candidate_count == 3'137'844);
static_assert(std::is_same_v<
              decltype(combdsl::search_for_xyz_subexp(B(K)(K))),
              std::optional<combdsl::subexpression_search_match>>);
static_assert(
    combdsl::search_for_subexp_candidate_count == 3'267'802);
static_assert(std::is_same_v<
              decltype(combdsl::search_for_subexp(S(B)(T))),
              std::optional<combdsl::subexpression_search_match>>);
static_assert(
    combdsl::check_for_pairs_match_candidate_count == 808);
static_assert(
    combdsl::check_for_trips_match_candidate_count == 45'186);
static_assert(
    combdsl::check_for_match_left_trip_candidate_count == 22'562);
static_assert(
    combdsl::check_for_match_right_trip_candidate_count == 22'624);
static_assert(
    combdsl::check_for_quads_match_tuple_count == 707'281);
static_assert(
    combdsl::check_for_quads_match_candidate_count == 3'228'466);
static_assert(
    combdsl::check_for_quads_match_candidate_count ==
    combdsl::check_for_match_combinator_count *
        combdsl::check_for_match_left_trip_candidate_count +
    combdsl::check_for_pairs_match_candidate_count *
        combdsl::check_for_pairs_match_candidate_count +
    (combdsl::check_for_match_combinator_count - 1) *
        combdsl::check_for_match_combinator_count *
        combdsl::check_for_pairs_match_candidate_count +
    (combdsl::check_for_match_combinator_count - 1) *
        combdsl::check_for_match_left_trip_candidate_count +
    (combdsl::check_for_match_combinator_count - 1) *
        (combdsl::check_for_match_combinator_count - 1) *
        combdsl::check_for_pairs_match_candidate_count);
using combinator_match_symbol_span =
    std::span<combdsl::quoted_atomic const>;
static_assert(std::is_same_v<
              decltype(combdsl::check_for_match(
                  std::declval<combdsl::quoted_expression>(),
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>())),
              bool>);
static_assert(std::is_same_v<
              decltype(combdsl::check_for_singles_match(
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>())),
              std::vector<combdsl::quoted_expression>>);
static_assert(std::is_same_v<
              decltype(combdsl::find_combinator_matches(
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>(),
                  std::declval<combdsl::combinator_find_options>())),
              combdsl::combinator_find_result>);
static_assert(
    combdsl::combinator_find_options{}.maximum_size == 3);
static_assert(
    !combdsl::combinator_find_options{}.all_sizes);
static_assert(std::is_same_v<
              decltype(combdsl::check_for_pairs_match(
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>())),
              std::vector<combdsl::quoted_expression>>);
static_assert(std::is_same_v<
              decltype(combdsl::check_for_trips_match(
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>())),
              std::vector<combdsl::quoted_expression>>);
static_assert(std::is_same_v<
              decltype(combdsl::check_for_quads_match(
                  std::declval<combinator_match_symbol_span>(),
                  std::declval<combdsl::quoted_expression const&>())),
              std::vector<combdsl::quoted_expression>>);
static_assert(is_single_utf8_char(
    std::string_view("\xF0\x9F\x98\x80", 4)));
static_assert(!is_single_utf8_char(
    std::string_view("\xF4\x90\x80\x80", 4)));
constexpr auto constexpr_utf8_symbol = symbol("\xCE\xBB");
static_assert(std::is_same_v<
              decltype(input_escape(std::declval<std::string_view>())),
              std::string>);
static_assert(std::is_same_v<decltype(set_list()), std::string>);
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
using eval_without_progress_signature = void (*)(
    combdsl::quoted_expression,
    std::ostream&,
    std::istream&,
    bool);
using eval_with_progress_signature = void (*)(
    combdsl::quoted_expression,
    std::ostream&,
    std::istream&,
    bool,
    evaluation_progress_callback const&);
static_assert(std::is_same_v<
              decltype(static_cast<eval_without_progress_signature>(&eval)),
              eval_without_progress_signature>);
static_assert(std::is_same_v<
              decltype(static_cast<eval_with_progress_signature>(&eval)),
              eval_with_progress_signature>);
using parse_eval_without_progress_signature = void (*)(
    std::string_view,
    std::ostream&,
    std::istream&,
    bool);
using parse_eval_with_progress_signature = void (*)(
    std::string_view,
    std::ostream&,
    std::istream&,
    bool,
    evaluation_progress_callback const&);
static_assert(std::is_same_v<
              decltype(static_cast<
                       parse_eval_without_progress_signature>(
                  &parse_eval)),
              parse_eval_without_progress_signature>);
static_assert(std::is_same_v<
              decltype(static_cast<
                       parse_eval_with_progress_signature>(
                  &parse_eval)),
              parse_eval_with_progress_signature>);
using single_step_run_without_progress_signature = void (*)(
    combdsl::quoted_expression,
    std::ostream&,
    std::istream&,
    bool);
using single_step_run_with_progress_signature = void (*)(
    combdsl::quoted_expression,
    std::ostream&,
    std::istream&,
    bool,
    evaluation_progress_callback const&);
static_assert(std::is_same_v<
              decltype(static_cast<
                       single_step_run_without_progress_signature>(
                  &single_step_run)),
              single_step_run_without_progress_signature>);
static_assert(std::is_same_v<
              decltype(static_cast<
                       single_step_run_with_progress_signature>(
                  &single_step_run)),
              single_step_run_with_progress_signature>);

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
    std::string_view class_name,
    std::string_view argument) {
    std::string result;
    result.reserve(
        class_name.size() + argument.size() + 22);
    result += "<span class=\"";
    result += class_name;
    result += "\">";
    result += argument;
    result += "</span>";
    return result;
}

[[nodiscard]] std::string red_argument(std::string_view argument) {
    return colored_argument("wor", argument);
}

[[nodiscard]] std::string green_argument(std::string_view argument) {
    return colored_argument("wog", argument);
}

[[nodiscard]] std::string blue_argument(std::string_view argument) {
    return colored_argument("wob", argument);
}

[[nodiscard]] std::string dark_orange_argument(
    std::string_view argument) {
    return colored_argument("woo", argument);
}

[[nodiscard]] std::string munsell_purple_argument(
    std::string_view argument) {
    return colored_argument("wop", argument);
}

[[nodiscard]] std::string terminal_colored_argument(
    std::string_view background,
    std::string_view argument) {
    std::string result = "\x1b[38;2;255;255;255m";
    result += background;
    result += argument;
    result += "\x1b[0m";
    return result;
}

[[nodiscard]] std::string
terminal_red_argument(std::string_view argument) {
    return terminal_colored_argument(
        "\x1b[48;2;255;000;000m", argument);
}

[[nodiscard]] std::string
terminal_green_argument(std::string_view argument) {
    return terminal_colored_argument(
        "\x1b[48;2;000;204;000m", argument);
}

[[nodiscard]] std::string
terminal_blue_argument(std::string_view argument) {
    return terminal_colored_argument(
        "\x1b[48;2;000;000;255m", argument);
}

[[nodiscard]] std::string terminal_dark_orange_argument(
    std::string_view argument) {
    return terminal_colored_argument(
        "\x1b[48;2;255;140;000m", argument);
}

[[nodiscard]] std::string terminal_munsell_purple_argument(
    std::string_view argument) {
    return terminal_colored_argument(
        "\x1b[48;2;204;000;255m", argument);
}

void test_parse_failure(
    std::string_view title,
    std::string_view source,
    std::size_t expected_position,
    std::string_view expected_detail = {}) {
    ++tests_run;

    try {
        static_cast<void>(parse(source));
    } catch (parse_error const& error) {
        auto expected_message =
            std::string("Parse error at position ");
        expected_message += std::to_string(expected_position + 1);
        expected_message += ": ";
        expected_message += expected_detail;
        if (error.position() != expected_position ||
            (!expected_detail.empty() &&
             error.what() != expected_message)) {
            std::cerr << "FAILED:   " << title << '\n'
                      << "expected position: " << expected_position << '\n'
                      << "actual position:   " << error.position() << '\n';
            if (!expected_detail.empty()) {
                std::cerr << "expected message:  " << expected_message << '\n'
                          << "actual message:    " << error.what() << '\n';
            }
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
    const auto fifth_argument_projection =
        basis("Fifth", 5, K(K(K(K(I)))));
    auto seven_character_basis = basis("1234567", 1, I);
    auto fifteen_character_basis =
        basis("123456789012345", 1, I);
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
    test("U", U, "U");
    test("U(x)", U(x), "Ux");
    test("U(x)(y)", U(x)(y), "y(xxy)");
    test("parse SKI", parse("SKI"), "SKI");
    test("parse SKIx", parse("SKIx"), "SKIx");
    test("parse symbols", parse("uvwxyz"), "uvwxyz");
    test("parse lowercase primitive names as symbols", parse("skiy"), "skiy");
    test("parse whitespace", parse(" \tS \n K\r\n I \v x\f"), "SKIx");
    test("parse left associative", parse("xyz"), "xyz");
    test("parse right nested operand", parse("x(yz)"), "x(yz)");
    test("parse grouped operand", parse("S ( K I ) x"), "S(KI)x");
    test("parse redundant groups", parse("((SK)I)x"), "SKIx");
    test_parse_failure(
        "spaced unknown lowercase basis",
        "x foo y", 2, "unknown operand");
    test_parse_failure(
        "unknown lowercase basis before spaced argument",
        "x(foo y)", 2, "unknown operand");
    test_parse_failure(
        "unknown lowercase basis after spaced argument",
        "x(y foo)", 4, "unknown operand");
    test("parentheses preserve an unspaced compact symbol run",
         parse("x(foo)y"), "x(foo)y");
    test("outer whitespace remains padding",
         parse(" \tfoo \n"), "foo");
    test("parenthetical whitespace remains padding",
         parse("x( foo )y"), "x(foo)y");
    test("compact symbols after a single-character basis remain valid",
         parse("Cxyz w"), "Cxyzw");
    static_cast<void>(basis("foo", 1, I));
    test("registered lowercase basis wins between spaces",
         parse("x foo y"), "x foo y");
    test("registered lowercase basis wins before a spaced argument",
         parse("x(foo y)"), "x(foo y)");
    test("registered lowercase basis wins after a spaced argument",
         parse("x(y foo)"), "x(y foo)");
    test("registered lowercase basis wins inside parentheses",
         parse("x(foo)y"), "x foo y");
    test("show exposes a named basis definition",
         parse("show M"), "arity:1 SII");
    test("show exposes a registered lowercase basis definition",
         parse("show foo"), "arity:1 I");
    test("show identifies S as fundamental",
         parse("show S"), "S is a fundamental name with arity:3");
    test("show identifies K as fundamental",
         parse("show K"), "K is a fundamental name with arity:2");
    test("show identifies I as fundamental",
         parse("show I"), "I is a fundamental name with arity:1");
    test("show identifies Y as fundamental",
         parse("show Y"), "Y is a fundamental name with arity:1");
    test("show accepts parser whitespace",
         parse(" \tshow\nM\f"), "arity:1 SII");
    test_parse_failure(
        "show rejects a symbol", "show x", 5,
        "x is not a defined name");
    test_parse_failure(
        "show rejects a primitive application", "show SKI", 5,
        "SKI is not a defined name");
    test_parse_failure(
        "show rejects an applied basis", "show Mx", 5,
        "Mx is not a defined name");
    test_parse_failure(
        "show rejects a parenthesized basis", "show (M)", 5,
        "(M) is not a defined name");
    test_parse_failure(
        "show rejects an escaped word",
        input_escape("show \"word\""), 5,
        "\"word\" is not a defined name");
    test("set list initially excludes C++ bases",
         [] { std::cout << set_list(); }, "");
    test("show all reports an empty set list",
         parse("show all"), "Nothing to show");
    test("show commands identify show all separately",
         [] {
             auto const all = combdsl::detail::parse_input("show all");
             auto const one = combdsl::detail::parse_input("show M");
             std::cout << (all.is_show_all ? "all" : "name") << ' '
                       << (one.is_show_all ? "all" : "name");
         },
         "all name");
    test("set list registers a default zero arity",
         parse(input_escape("set LZero = I")), "LZero");
    test("set list shows a default zero arity",
         [] { std::cout << set_list(); }, "set LZero = 0 I");
    test("set list registers a dependent basis",
         parse(input_escape("set LUse = 1 LZero")), "LUse");
    test("set list registers a binary basis",
         parse(input_escape("set LPair = 2 K")), "LPair");
    test("set list registers a double-backslash name",
         parse(input_escape("set Q\\R = 1 I")), "Q\\\\R");
    test("set list registers a double-backslash body reference",
         parse(input_escape("set LSlash = 1 Q\\R")), "LSlash");
    test("set list registers a multicharacter basis body",
         parse(input_escape("set LMulti = 1 Cstar x")), "LMulti");
    test("set list registers a raw word body",
         parse(input_escape("set LRaw = 1 K \"a b()\\c\"")), "LRaw");

    const std::string expected_set_list =
        "set LZero = 0 I\n"
        "set LUse = 1 LZero\n"
        "set LPair = 2 K\n"
        "set Q\\R = 1 I\n"
        "set LSlash = 1 Q\\R\n"
        "set LMulti = 1 Cstar x\n"
        "set LRaw = 1 K \"a b()\\c\"";
    test("set list preserves definitions in replay order",
         [] { std::cout << set_list(); }, expected_set_list);
    test("show all displays the entire set list",
         parse("show all"), expected_set_list);
    test("set list double-backslash basis replays",
         single_step(parse(input_escape("Q\\R x"))), "x");
    test("set list raw word basis replays",
         single_step(parse(input_escape("LRaw x"))), "a b()\\c");

    test("set list accepts a later duplicate definition",
         parse(input_escape("set LPair = 1 I")), "LPair");
    const std::string expected_redefined_set_list =
        "set LZero = 0 I\n"
        "set LUse = 1 LZero\n"
        "set Q\\R = 1 I\n"
        "set LSlash = 1 Q\\R\n"
        "set LMulti = 1 Cstar x\n"
        "set LRaw = 1 K \"a b()\\c\"\n"
        "set LPair = 1 I";
    test("set list replaces an unreferenced old definition",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test_parse_failure(
        "set rejects a primitive definition",
        input_escape("set K = I"),
        4,
        "K is a pre-defined basis and cannot be redefined");
    test("set list excludes rejected primitive definitions",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test_parse_failure(
        "set rejects a predefined basis definition",
        input_escape("set M = I"),
        4,
        "M is a pre-defined basis and cannot be redefined");
    test("set list excludes rejected predefined definitions",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test_parse_failure("set list rejects a malformed definition",
                       "set LBad = K@", 12);
    test("set list excludes malformed definitions",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test("set list output can be inspected without changes",
         [&expected_redefined_set_list] {
             std::istringstream definitions(
                 expected_redefined_set_list);
             std::string definition;
             while (std::getline(definitions, definition)) {
                 static_cast<void>(combdsl::detail::parse_input(
                     input_escape(definition),
                     combdsl::detail::parser_definition_mode::
                         inspect_definitions));
             }
             std::cout << set_list();
         },
         expected_redefined_set_list);

    test("define registers a replayable basis",
         parse("define DefSave x y z = x(y z)"), "DefSave");
    const std::string expected_definition_list =
        expected_redefined_set_list +
        "\ndefine DefSave xyz = x(y z)";
    test("set list includes a canonical define command",
         [] { std::cout << set_list(); }, expected_definition_list);
    test("canonical define command can be reparsed",
         [] {
             static_cast<void>(
                 parse(input_escape("define DefSave xyz = x(y z)")));
             std::cout << set_list();
         },
         expected_definition_list);

    test("set registers a self-redefinition base",
         parse("set SelfReplay = 0 I"), "SelfReplay");
    test("set may capture its previous definition",
         parse("set SelfReplay = 0 SelfReplay"), "SelfReplay");
    test("set list retains a required same-name snapshot",
         [] {
             constexpr std::string_view suffix =
                 "set SelfReplay = 0 I\n"
                 "set SelfReplay = 0 SelfReplay";
             std::cout << (set_list().ends_with(suffix)
                 ? "retained"
                 : "missing");
         },
         "retained");
    test("a retained same-name snapshot remains usable",
         single_step(single_step(single_step(
             parse("SelfReplay x")))),
         "x");
    test("an otherwise unused self-history can still be removed",
         [] {
             static_cast<void>(parse("remove SelfReplay"));
             std::cout << (set_list().find("SelfReplay") ==
                     std::string::npos
                 ? "omitted"
                 : "retained");
         },
         "omitted");

    test("define registers an initial replaceable definition",
         parse("define DefReplace x = x"), "DefReplace");
    test("define replaces the unreferenced definition",
         parse("define DefReplace x = Kx"), "DefReplace");
    test("set list keeps only the current unreferenced define",
         [] {
             auto const definitions = set_list();
             auto const replacement = definitions.find(
                 "define DefReplace x = Kx");
             std::cout <<
                 (replacement != std::string::npos &&
                  definitions.find("define DefReplace x = x") ==
                      std::string::npos
                     ? "compacted"
                     : "not compacted");
         },
         "compacted");

    test("remove inspection accepts a user basis",
         parse("set RemoveInspect = 0 I"), "RemoveInspect");
    test("remove inspection does not change the registry",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "remove RemoveInspect",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << (inspected.is_definition
                 ? "definition/"
                 : "expression/");
             parse("show RemoveInspect").print_to(std::cout);
         },
         "definition/arity:0 I");
    test("remove returns the removed basis snapshot",
         parse("remove RemoveInspect"), "RemoveInspect");
    test_parse_failure(
        "show rejects a removed basis",
        "show RemoveInspect",
        5,
        "RemoveInspect is not a defined name");
    test("an unreferenced removed basis leaves no saved commands",
         [] {
             std::cout << (set_list().find("RemoveInspect") ==
                     std::string::npos
                 ? "omitted"
                 : "retained");
         },
         "omitted");

    test("remove can discard an unreferenced self-history",
         [] {
             static_cast<void>(parse("set RemoveSolo = 0 I"));
             static_cast<void>(
                 parse("set RemoveSolo = 0 RemoveSolo"));
             static_cast<void>(parse("remove RemoveSolo"));
             std::cout << (set_list().find("RemoveSolo") ==
                     std::string::npos
                 ? "omitted"
                 : "retained");
         },
         "omitted");

    test("set registers a basis that will be referred to",
         parse("set RemoveBase = 0 I"), "RemoveBase");
    auto remove_base_snapshot = parse("RemoveBase x");
    test("set records a reference to another user basis",
         parse("set RemoveUse = 0 RemoveBase"), "RemoveUse");
    test("remove accepts a referred-to basis",
         parse("remove RemoveBase"), "RemoveBase");
    test("a removed basis snapshot remains usable",
         single_step(single_step(remove_base_snapshot)), "x");
    test("a dependent basis keeps the removed snapshot",
         parse("show RemoveUse"), "arity:0 RemoveBase");
    test("set list keeps a referred definition and its removal",
         [] {
             constexpr std::string_view suffix =
                 "set RemoveBase = 0 I\n"
                 "set RemoveUse = 0 RemoveBase\n"
                 "remove RemoveBase";
             std::cout << (set_list().ends_with(suffix)
                 ? "retained"
                 : "missing");
         },
         "retained");
    test("a removed name can be defined again",
         parse("set RemoveBase = 0 K"), "RemoveBase");
    test("an unreferenced later definition is omitted when removed",
         [] {
             static_cast<void>(parse("remove RemoveBase"));
             auto const definitions = set_list();
             auto const first = definitions.find("remove RemoveBase");
             auto const second = first == std::string::npos
                 ? std::string::npos
                 : definitions.find("remove RemoveBase", first + 1);
             std::cout <<
                 (first != std::string::npos &&
                  second == std::string::npos &&
                  definitions.find("set RemoveBase = 0 K") ==
                      std::string::npos
                     ? "one retained removal"
                     : "unexpected history");
         },
         "one retained removal");

    test("a reference propagates through same-name history",
         [] {
             static_cast<void>(parse("set RemoveChain = 0 I"));
             static_cast<void>(
                 parse("set RemoveChain = 0 RemoveChain"));
             static_cast<void>(
                 parse("set RemoveChainUse = 0 RemoveChain"));
             static_cast<void>(parse("remove RemoveChain"));
             constexpr std::string_view suffix =
                 "set RemoveChain = 0 I\n"
                 "set RemoveChain = 0 RemoveChain\n"
                 "set RemoveChainUse = 0 RemoveChain\n"
                 "remove RemoveChain";
             std::cout << (set_list().ends_with(suffix)
                 ? "retained"
                 : "missing");
         },
         "retained");

    test("was referred to remains true after the referring basis is removed",
         [] {
             static_cast<void>(parse("set StickyA = 0 I"));
             static_cast<void>(parse("set StickyB = 0 StickyA"));
             static_cast<void>(parse("remove StickyB"));
             static_cast<void>(parse("remove StickyA"));
             auto const definitions = set_list();
             std::cout <<
                 (definitions.ends_with(
                      "set StickyA = 0 I\nremove StickyA") &&
                  definitions.find("StickyB") == std::string::npos
                     ? "sticky"
                     : "not sticky");
         },
         "sticky");

    test_parse_failure(
        "remove requires a name", "remove ", 7,
        "missing combinator name");
    test_parse_failure(
        "remove rejects an undefined name", "remove RemoveMissing", 7,
        "RemoveMissing is not a defined name");
    test_parse_failure(
        "remove rejects a primitive", "remove S", 7,
        "S is a pre-defined basis and cannot be removed");
    test_parse_failure(
        "remove rejects a predefined basis", "remove M", 7,
        "M is a pre-defined basis and cannot be removed");
    test_parse_failure(
        "remove rejects trailing input", "remove RemoveUse extra", 17,
        "unexpected input after name");
    test("remove without required whitespace remains symbols",
         parse("removex"), "removex");
    test_parse_failure(
        "bare set requires a combinator name", "set", 3,
        "missing combinator name");
    test_parse_failure(
        "bare define requires a combinator name", "define", 6,
        "missing combinator name");
    test_parse_failure(
        "bare show requires a combinator name", "show", 4,
        "missing combinator name");
    test_parse_failure(
        "bare remove requires a combinator name", "remove", 6,
        "missing combinator name");
    test_parse_failure(
        "bare load requires a filename", "load", 4,
        "missing filename");
    test_parse_failure(
        "bare save requires a filename", "save", 4,
        "missing filename");
    test_parse_failure(
        "whitespace-only load argument is missing", "load  ", 6,
        "missing filename");
    test_parse_failure(
        "whitespace-only save argument is missing", "save  ", 6,
        "missing filename");

    test_parse_failure(
        "define cannot replace a predefined basis",
        "define M x = x",
        7,
        "M is a pre-defined basis and cannot be redefined");
    test("define registers a replaceable user basis",
         parse("define DefReplace x = x"), "DefReplace");
    test("equivalent set form does not require replacement",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set DefReplace = 1 I",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << (inspected.replaced_definition.empty()
                 ? "unchanged"
                 : inspected.replaced_definition);
         },
         "unchanged");
    test("changed define identifies its existing stored definition",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "define DefReplace x = Kx",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "DefReplace=1 I");
    test("changed define replaces a user definition",
         parse("define DefReplace x = Kx"), "DefReplace");
    test("show exposes a replaced define body",
         parse("show DefReplace"), "arity:1 K");
    test("set can replace a define and its arity",
         parse("set DefReplace = 2 I"), "DefReplace");
    test("show exposes the cross-form replacement",
         parse("show DefReplace"), "arity:2 I");
    test("cross-form replacement uses its new arity",
         single_step(parse("DefReplace a b")), "ab");

    test("define infers arity from its symbols",
         parse("define Def3 xyz = xyz"), "Def3");
    test("define basis remains named while undersaturated",
         single_step(parse("Def3 a b")), "Def3ab");
    test("basis step exposes a define basis body",
         single_step(parse("Def3 a b c"), true), "Iabc");
    test("define basis contracts when saturated",
         single_step(parse("Def3 a b c")), "abc");
    test("define preprocessing applies saturated bases",
         combdsl::detail::reduce_saturated_bases(
             parse("C(CB)xyz")),
         "x(yz)");
    test("define preprocessing applies nested saturated bases",
         combdsl::detail::reduce_saturated_bases(
             parse("u(C(CB)xyz)")),
         "u(x(yz))");
    test("define preprocessing preserves undersaturated bases",
         combdsl::detail::reduce_saturated_bases(
             parse("C(CB)x")),
         "C(CB)x");
    test("define preprocesses bases before takeout",
         parse("define PrepB xyz = C(CB)xyz"), "PrepB");
    test("show exposes preprocessed Bluebird definition",
         parse("show PrepB"), "arity:3 B");
    test("preprocessed Bluebird preserves behavior",
         single_step(parse("PrepB a b c")), "a(bc)");
    test("set creates a user basis for define preprocessing",
         parse("set PrepAlias = 1 K"), "PrepAlias");
    test("define preprocessing applies a user basis",
         parse("define PrepK x = PrepAlias x"), "PrepK");
    test("show exposes the applied user basis",
         parse("show PrepK"), "arity:1 K");
    test("applied user basis preserves behavior",
         single_step(parse("PrepK a")), "Ka");
    test("define preprocessing leaves a user basis undersaturated",
         parse("define PrepKeep x = PrepAlias"), "PrepKeep");
    test("show preserves the undersaturated user basis",
         parse("show PrepKeep"), "arity:1 K PrepAlias");
    test("undersaturated user basis remains named",
         single_step(parse("PrepKeep a")), "PrepAlias");
    test("set creates a zero-arity preprocessing basis",
         parse("set PrepZero = 0 I"), "PrepZero");
    test("define preprocessing applies a zero-arity basis",
         parse("define PrepI x = PrepZero x"), "PrepI");
    test("show exposes the applied zero-arity basis",
         parse("show PrepI"), "arity:1 I");
    test("applied zero-arity basis preserves behavior",
         single_step(parse("PrepI a")), "a");
    test("define preprocessing leaves primitives unchanged",
         combdsl::detail::reduce_saturated_bases(
             parse("Sxyz")),
         "Sxyz");
    test("define preprocessing preserves a partial K argument",
         combdsl::detail::reduce_saturated_bases(
             parse("K(Ma)")),
         "K(Ma)");
    test("define preprocessing preserves a discarded K argument",
         combdsl::detail::reduce_saturated_bases(
             parse("Kx(Ma)")),
         "Kx(Ma)");
    test("define preprocessing does not enter a Y generator",
         combdsl::detail::reduce_saturated_bases(
             parse("Y(Ma)")),
         "Y(Ma)");
    test("set creates a zero-arity Y preprocessing boundary",
         parse("set PrepY = 0 Y(Ma)"), "PrepY");
    test("define preprocessing expands but does not enter Y",
         combdsl::detail::reduce_saturated_bases(
             parse("PrepY")),
         "Y(Ma)");
    test("set creates a preprocessing snapshot",
         parse("set CycleSnap = 1 K"), "CycleSnap");
    test("set creates a same-name zero-arity snapshot",
         parse("set CycleSnap = 0 CycleSnap"), "CycleSnap");
    test("define preprocessing distinguishes same-name snapshots",
         single_step(
             combdsl::detail::reduce_saturated_bases(
                 parse("CycleSnap"))(a)),
         "Ka");
    test("define preprocessing stops at a repeating basis redex",
         combdsl::detail::reduce_saturated_bases(
             parse("MM")),
         "MM");
    test("define accepts a repeating saturated body",
         parse("define PrepOmega x = MM"), "PrepOmega");
    test("show preserves the repeating saturated body",
         parse("show PrepOmega"), "arity:1 K(MM)");
    test("define abstracts symbols from right to left",
         parse("define DefE xyz = exp"), "DefE");
    test("basis step exposes right-to-left abstraction",
         single_step(parse("DefE a b c"), true),
         "BK(BK(Cep))abc");
    test("right-to-left abstraction preserves behavior",
         single_step(parse("DefE a b c")), "eap");
    test("define applies contextual Queer selection",
         parse("define DefContext xyz = xy(wz)"), "DefContext");
    test("show exposes contextual Queer selection",
         parse("show DefContext"), "arity:3 B(Qw)");
    test("contextual Queer selection preserves behavior",
         single_step(parse("DefContext a b c")), "ab(wc)");
    test("define keeps Bluebird when qfun has no pending atom",
         parse("define DefNoQfun xy = w(zy)"), "DefNoQfun");
    test("show exposes the qfun-dependent Bluebird choice",
         parse("show DefNoQfun"), "arity:2 K(Bwz)");
    test("qfun-dependent Bluebird choice preserves behavior",
         single_step(parse("DefNoQfun a b")), "w(zb)");
    test("define uses Cardinal when only qarg has pending atoms",
         parse("define DefCContext xy = wyx"), "DefCContext");
    test("show exposes contextual Cardinal selection",
         parse("show DefCContext"), "arity:2 Cw");
    test("contextual Cardinal selection preserves behavior",
         single_step(parse("DefCContext a b")), "wba");
    test("define uses Cardinal when pending counts tie",
         parse("define DefCTie xy = wyz"), "DefCTie");
    test("show exposes contextual Cardinal tie selection",
         parse("show DefCTie"), "arity:2 K(Cwz)");
    test("contextual Cardinal tie selection preserves behavior",
         single_step(parse("DefCTie a b")), "wbz");
    test("define creates the Thrush from reverse application",
         parse("define Flip xy = yx"), "Flip");
    test("basis step exposes the defined Thrush",
         single_step(parse("Flip a b"), true), "Tab");
    test("defined Thrush reverses its arguments",
         single_step(parse("Flip a b")), "ba");
    test("show exposes one level of a defined basis",
         parse("show Flip"), "arity:2 T");
    test("define chains BC and C*T optimizations to V",
         parse("define DefV x = C(Tx)"), "DefV");
    test("show exposes the optimized Vireo",
         parse("show DefV"), "arity:1 V");
    test("optimized Vireo preserves behavior",
         single_step(single_step(parse("DefV a b c"))), "cab");
    test("define optimizes BB to D",
         parse("define DefD x = BBx"), "DefD");
    test("show exposes the optimized Dove",
         parse("show DefD"), "arity:1 D");
    test("optimized Dove preserves behavior",
         single_step(single_step(parse("DefD a b c d"))), "ab(cd)");
    test("define recursively chains BC and C*T to V",
         parse("define DefKV x = BCT"), "DefKV");
    test("show exposes nested Vireo optimization",
         parse("show DefKV"), "arity:1 KV");
    test("define recursively optimizes nested C*T",
         parse("define DefKCstarT x = C*T"), "DefKCstarT");
    test("show exposes direct C*T Vireo optimization",
         parse("show DefKCstarT"), "arity:1 KV");
    test("define optimizes BC to Cardinal star",
         parse("define OptCstar xyzw = xywz"), "OptCstar");
    test("show exposes optimized Cardinal star",
         parse("show OptCstar"), "arity:4 C*");
    test("define recursively optimizes nested BC",
         parse("define DefKCstar x = BC"), "DefKCstar");
    test("show exposes nested Cardinal star optimization",
         parse("show DefKCstar"), "arity:1 K C*");
    test("define optimizes B C* to Cardinal star star",
         parse("define OptCstarstar xyzwv = xyzvw"), "OptCstarstar");
    test("show exposes optimized Cardinal star star",
         parse("show OptCstarstar"), "arity:5 C**");
    test("define recursively optimizes nested B C*",
         parse("define DefKCstarstar x = B C*"), "DefKCstarstar");
    test("show exposes nested Cardinal star star optimization",
         parse("show DefKCstarstar"), "arity:1 K C**");
    test("define optimizes B(QT)R to Finch",
         parse("define OptF xyz = zyx"), "OptF");
    test("show exposes optimized Finch",
         parse("show OptF"), "arity:3 F");
    test("optimized Finch preserves behavior",
         single_step(single_step(parse("OptF a b c"))), "cba");
    test("define recursively optimizes nested B(QT)R",
         parse("define DefKF x = B(QT)R"), "DefKF");
    test("show exposes nested Finch optimization",
         parse("show DefKF"), "arity:1 KF");
    test("define optimizes B(QT)B to Quixotic bird",
         parse("define OptQ1 xyz = x(zy)"), "OptQ1");
    test("show exposes optimized Quixotic bird",
         parse("show OptQ1"), "arity:3 Q1");
    test("optimized Quixotic bird preserves behavior",
         single_step(single_step(parse("OptQ1 a b c"))), "a(cb)");
    test("define recursively optimizes nested B(QT)B",
         parse("define DefKQ1 x = B(QT)B"), "DefKQ1");
    test("show exposes nested Quixotic optimization",
         parse("show DefKQ1"), "arity:1 K Q1");
    test("define optimizes BDD to Eagle",
         parse("define OptE xyzwv = xy(zwv)"), "OptE");
    test("show exposes optimized Eagle",
         parse("show OptE"), "arity:5 E");
    test("define recursively optimizes nested BDD",
         parse("define DefKE x = BDD"), "DefKE");
    test("show exposes nested Eagle optimization",
         parse("show DefKE"), "arity:1 KE");
    test("define optimizes BOM to Turing bird",
         parse("define OptU xy = y(xxy)"), "OptU");
    test("show exposes optimized Turing bird",
         parse("show OptU"), "arity:2 U");
    test("define recursively optimizes nested BOM",
         parse("define DefKU x = BOM"), "DefKU");
    test("show exposes nested Turing bird optimization",
         parse("show DefKU"), "arity:1 KU");
    test("define recursively optimizes nested BT",
         parse("define DefKQ3 x = BT"), "DefKQ3");
    test("show exposes nested Quirky optimization",
         parse("show DefKQ3"), "arity:1 K Q3");
    test("define recursively optimizes nested BW",
         parse("define DefKWstar x = BW"), "DefKWstar");
    test("show exposes nested Warbler star optimization",
         parse("show DefKWstar"), "arity:1 K W*");
    test("define preserves nested QTC",
         parse("define DefKQV x = QTC"), "DefKQV");
    test("show exposes nested unoptimized QTC",
         parse("show DefKQV"), "arity:1 K(QTC)");
    test("define recursively optimizes nested BB",
         parse("define DefKD x = BB"), "DefKD");
    test("show exposes nested Dove optimization",
         parse("show DefKD"), "arity:1 KD");
    test("define produces R without the CC optimizer",
         parse("define DefR xyz = yzx"), "DefR");
    test("show exposes the takeout-produced Robin",
         parse("show DefR"), "arity:3 R");
    test("takeout-produced Robin preserves behavior",
         single_step(single_step(parse("DefR a b c"))), "bca");
    test("define preserves nested CC",
         parse("define DefKR x = CC"), "DefKR");
    test("show exposes nested unoptimized CC",
         parse("show DefKR"), "arity:1 K(CC)");
    test("define optimizes SBT to A",
         parse("define DefA xy = x(yx)"), "DefA");
    test("show exposes the optimized Albatross",
         parse("show DefA"), "arity:2 A");
    test("optimized Albatross preserves behavior",
         single_step(single_step(parse("DefA a b"))), "a(ba)");
    test("define recursively optimizes nested SBT",
         parse("define DefKA x = SBT"), "DefKA");
    test("show exposes nested Albatross optimization",
         parse("show DefKA"), "arity:1 KA");
    test("define recursively optimizes nested SR",
         parse("define DefKH x = SR"), "DefKH");
    test("show exposes nested H optimization",
         parse("show DefKH"), "arity:1 KH");
    test("define optimizes DC to Goldfinch",
         parse("define OptG xyzw = xw(yz)"), "OptG");
    test("show exposes optimized Goldfinch",
         parse("show OptG"), "arity:4 G");
    test("define recursively optimizes nested DC",
         parse("define DefKG x = DC"), "DefKG");
    test("show exposes nested Goldfinch optimization",
         parse("show DefKG"), "arity:1 KG");
    test("define optimizes QM to L",
         parse("define DefL xy = x(yy)"), "DefL");
    test("show exposes the optimized Lark",
         parse("show DefL"), "arity:2 L");
    test("optimized Lark preserves behavior",
         single_step(single_step(parse("DefL a b"))), "a(bb)");
    test("define recursively optimizes nested QM",
         parse("define DefKL x = QM"), "DefKL");
    test("show exposes nested Lark optimization",
         parse("show DefKL"), "arity:1 KL");
    test("define optimizes WC to N",
         parse("define DefN xy = xyx"), "DefN");
    test("show exposes the optimized Nightingale",
         parse("show DefN"), "arity:2 N");
    test("optimized Nightingale preserves behavior",
         single_step(single_step(parse("DefN a b"))), "aba");
    test("define recursively optimizes nested WC",
         parse("define DefKN x = WC"), "DefKN");
    test("show exposes nested Nightingale optimization",
         parse("show DefKN"), "arity:1 KN");
    test("define optimizes WV to Converse warbler",
         parse("define OptW1 xy = yxx"), "OptW1");
    test("show exposes optimized Converse warbler",
         parse("show OptW1"), "arity:2 W1");
    test("define recursively optimizes nested WV",
         parse("define DefKW1 x = WV"), "DefKW1");
    test("show exposes nested Converse warbler optimization",
         parse("show DefKW1"), "arity:1 K W1");
    test("define preserves nested WR",
         parse("define DefKNR x = WR"), "DefKNR");
    test("show exposes nested unoptimized WR",
         parse("show DefKNR"), "arity:1 K(WR)");
    test("define produces Q without the CB optimizer",
         parse("define DefQ xyz = y(xz)"), "DefQ");
    test("show exposes the takeout-produced Queer",
         parse("show DefQ"), "arity:3 Q");
    test("takeout-produced Queer preserves behavior",
         single_step(single_step(parse("DefQ a b c"))), "b(ac)");
    test("define preserves nested CB",
         parse("define DefKQ x = CB"), "DefKQ");
    test("show exposes nested unoptimized CB",
         parse("show DefKQ"), "arity:1 K(CB)");
    test("define optimizes WB to Z",
         parse("define DefZ xy = x(xy)"), "DefZ");
    test("show exposes the optimized Zazu",
         parse("show DefZ"), "arity:2 Z");
    test("optimized Zazu preserves behavior",
         single_step(single_step(parse("DefZ a b"))), "a(ab)");
    test("define recursively optimizes nested WB",
         parse("define DefKZ x = WB"), "DefKZ");
    test("show exposes nested Zazu optimization",
         parse("show DefKZ"), "arity:1 KZ");
    test("define optimizes BW* to Warbler star star",
         parse("define OptWstarstar xyzw = xyzww"),
         "OptWstarstar");
    test("show exposes optimized Warbler star star",
         parse("show OptWstarstar"), "arity:4 W**");
    test("define recursively optimizes nested BW*",
         parse("define DefKWss x = B W*"), "DefKWss");
    test("show exposes nested Warbler star star optimization",
         parse("show DefKWss"), "arity:1 K W**");
    test("define optimizes S(D(BQC))D to Jay",
         parse("define OptJ xyzw = xy(xwz)"), "OptJ");
    test("show exposes the optimized Jay",
         parse("show OptJ"), "arity:4 J");
    test("optimized Jay preserves behavior",
         single_step(single_step(parse("OptJ a b c d"))),
         "ab(adc)");
    test("define recursively optimizes nested S(D(BQC))D",
         parse("define DefKJ x = S(D(BQC))D"), "DefKJ");
    test("show exposes the nested Jay optimization",
         parse("show DefKJ"), "arity:1 KJ");
    test("define preprocessing creates the Bluebird",
         parse("define DefB xyz = Qyxz"), "DefB");
    test("show exposes the preprocessed Bluebird",
         parse("show DefB"), "arity:3 B");
    test("preprocessed Bluebird preserves behavior",
         single_step(parse("DefB a b c")), "a(bc)");
    test("define leaves nested CQ unoptimized",
         parse("define DefKCQ x = CQ"), "DefKCQ");
    test("show exposes nested unoptimized CQ",
         parse("show DefKCQ"), "arity:1 K(CQ)");
    test("define preserves CB within C",
         parse("define DefChainB x = C(CB)x"), "DefChainB");
    test("show exposes the resulting C(CB)",
         parse("show DefChainB"), "arity:1 C(CB)");
    test("define creates the Starling",
         parse("define DefS xyz = xz(yz)"), "DefS");
    test("basis step exposes the defined Starling",
         single_step(parse("DefS a b c"), true), "Sabc");
    test("defined Starling applies both branches",
         single_step(parse("DefS a b c")), "ac(bc)");
    test("define accepts whitespace-separated symbols",
         parse(" \tdefine\nDws \tx y\nz =\v xyz\f"), "Dws");
    test("whitespace-separated define symbols preserve their order",
         single_step(parse("Dws a b c"), true), "Iabc");
    test("define accepts a symbol adjacent to a one-letter name",
         parse("define Xx = xSTK(KK)(SK)"), "X");
    test("compact one-letter define preserves its behavior",
         single_step(
             parse("define Xx=xSTK(KK)(SK)")(w)),
         "wSTK(KK)(SK)");
    test("compact one-letter define infers its adjacent symbol",
         parse("define Xx = x"), "X");
    test("compact one-letter define registers its basis",
         single_step(parse("Xa")), "a");
    test("compact one-letter define canonicalizes its signature",
         [] {
             auto definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define X x = x");
    test("compact define accepts a single UTF-8 character name",
         parse("define \xE2\x96\xB2x = x"), "\xE2\x96\xB2");
    test("compact UTF-8 define registers its basis",
         single_step(parse("\xE2\x96\xB2 a")), "a");
    test("compact UTF-8 define accepts multiple adjacent symbols",
         parse("define \xE2\x96\xA0xyz = x(yz)"), "\xE2\x96\xA0");
    test("compact UTF-8 multi-symbol define registers its basis",
         single_step(parse("\xE2\x96\xA0 a b c")), "a(bc)");
    test("compact multi-symbol define canonicalizes its signature",
         [] {
             auto definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define \xE2\x96\xA0 xyz = x(yz)");
    test("compact one-letter define recognizes recursion",
         parse("define Xx = x(Xx)"), "X");
    test("compact recursive define is wrapped in Y",
         parse("show X"), "arity:1 Y");
    test("two-character define names retain their spaced symbol",
         parse("define Gx y = y"), "Gx");
    test("two-character define name registers without becoming G",
         single_step(parse("Gx a")), "a");
    test("compact define accepts multiple adjacent symbols",
         parse("define Xxyz = x(yz)"), "X");
    test("compact multiple-symbol define preserves its behavior",
         single_step(
             parse("define Xxyz=x(yz)")(a)(b)(c)),
         "a(bc)");
    test("an overlong compact signature is not an overlong name",
         parse("define ~abcdefghijklmno = a"), "~");
    test("define keeps symbols distinct from symbolic strings",
         parse(input_escape("define DRaw x = \"x\"x")), "DRaw");
    test("defined symbolic string is not abstracted as a symbol",
         single_step(parse("DRaw y")), "xy");
    test("set list preserves a define symbolic string",
         [] {
             auto definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define DRaw x = \"x\"x");
    test("pre-defined Eagle is registered", parse("E"), "E");
    test("show exposes the pre-defined Eagle",
         parse("show E"), "arity:5 BDD");
    test("show exposes the pre-defined Jay",
         parse("show J"), "arity:4 C**(HE)");
    test("pre-defined Turing is registered", parse("U"), "U");
    test("show exposes the pre-defined Turing",
         parse("show U"), "arity:2 BOM");
    test("pre-defined Turing reduces",
         single_step(parse("Uxy")), "y(xxy)");
    test("pre-defined Cardinal star is registered",
         parse("C*"), "C*");
    test("show exposes pre-defined Cardinal star",
         parse("show C*"), "arity:4 BC");
    test("pre-defined Cardinal star reduces compact input",
         single_step(parse("C*wxyz")), "wxzy");
    test("pre-defined Cardinal star star is registered",
         parse("C**"), "C**");
    test("show exposes pre-defined Cardinal star star",
         parse("show C**"), "arity:5 B C*");
    test("Cardinal star star wins the longest compact match",
         parse("C**x"), "C**x");
    test("pre-defined Cardinal star star reduces compact input",
         single_step(parse("C**vwxyz")), "vwxzy");
    test("pre-defined Warbler star is registered",
         parse("W*"), "W*");
    test("show exposes pre-defined Warbler star",
         parse("show W*"), "arity:3 BW");
    test("pre-defined Warbler star reduces compact input",
         single_step(parse("W*wxy")), "wxyy");
    test("pre-defined Warbler star star is registered",
         parse("W**"), "W**");
    test("show exposes pre-defined Warbler star star",
         parse("show W**"), "arity:4 B W*");
    test("Warbler star star wins the longest compact match",
         parse("W**x"), "W**x");
    test("pre-defined Warbler star star reduces compact input",
         single_step(parse("W**vwxy")), "vwxyy");
    test("pre-defined Converse warbler is registered",
         parse("W1"), "W1");
    test("show exposes pre-defined Converse warbler",
         parse("show W1"), "arity:2 CW");
    test("pre-defined Converse warbler prints compactly before a symbol",
         parse("W1x"), "W1x");
    test("pre-defined Converse warbler reduces compact input",
         single_step(parse("W1xy")), "yxx");
    test("pre-defined Quixotic is registered", parse("Q1"), "Q1");
    test("show exposes the pre-defined Quixotic",
         parse("show Q1"), "arity:3 BCB");
    test("pre-defined Quixotic reduces",
         single_step(parse("Q1 x y z")), "x(zy)");
    test("pre-defined Quixotic prints compactly before a symbol",
         parse("Q1x"), "Q1x");
    test("pre-defined Quixotic needs no trailing delimiter",
         single_step(parse("Q1xyz")), "x(zy)");
    test("pre-defined Quirky is registered", parse("Q3"), "Q3");
    test("show exposes the pre-defined Quirky",
         parse("show Q3"), "arity:3 BT");
    test("pre-defined Quirky reduces",
         single_step(parse("Q3 x y z")), "z(xy)");
    test("pre-defined Quirky prints compactly before a symbol",
         parse("Q3x"), "Q3x");
    test("pre-defined Quirky needs no trailing delimiter",
         single_step(parse("Q3xyz")), "z(xy)");
    test("define recognizes a recursive name",
         parse("define Repeat x = x(Repeat x)"), "Repeat");
    test("recursive define optimizes YO to Y",
         parse("show Repeat"), "arity:1 Y");
    test("set list preserves a recursive define",
         [] {
             auto definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define Repeat x = x(Repeat x)");
    test("define recursively optimizes nested YO",
         parse("define DefKY x = YO"), "DefKY");
    test("show exposes nested Sage optimization",
         parse("show DefKY"), "arity:1 KY");
    test("recursive define can reach a terminating result",
         parse("define Recur x = Kx Recur"), "Recur");
    test("show exposes recursive abstraction",
         parse("show Recur"), "arity:1 Y(CK)");
    test("recursive basis evaluates through Y",
         single_step(parse("Recur a")), "a");
    test("recursive abstraction precedes optimization",
         parse("define RV x = C(T RV)x"), "RV");
    test("recursive abstraction optimization is wrapped in Y",
         parse("show RV"), "arity:1 YV");
    test("recursive function abstraction chooses Bluebird",
         parse("define RecursiveB x = u(v RecursiveB)x"),
         "RecursiveB");
    test("show exposes recursive Bluebird abstraction",
         parse("show RecursiveB"), "arity:1 Y(Buv)");
    test("recursive function abstraction uses Cardinal on an empty tie",
         parse("define RecCtx x = y RecCtx z"), "RecCtx");
    test("show exposes recursive contextual Cardinal abstraction",
         parse("show RecCtx"), "arity:1 Y(BK(Cyz))");
    test("a quoted word matching the definition name is not recursive",
         parse(input_escape(
             "define WordRec x = \"WordRec\"")), "WordRec");
    test("show keeps the matching quoted word nonrecursive",
         parse("show WordRec"), "arity:1 K WordRec");
    test("a single-character recursive name may be adjacent",
         parse("define X x = Xx"), "X");
    test("single-character recursive adjacency stores YI",
         parse("show X"), "arity:1 YI");
    test("define accepts a nonterminating recursive body",
         parse("define Loop x = Loop x"), "Loop");
    test("show exposes the nonterminating recursive body",
         parse("show Loop"), "arity:1 YI");
    test("single step stops at a recursive Y boundary",
         single_step(parse("Loop a")), "<deferred Y(I)>a");
    test("ordinary stepping may unfold the recursive boundary",
         single_step(single_step(parse("Loop a"))),
         "I<deferred Y(I)>a");
    test("nonterminating recursion remains step-responsive",
         single_step(single_step(single_step(parse("Loop a")))),
         "<deferred Y(I)>a");
    test_parse_failure(
        "a lowercase-ending recursive name requires a delimiter",
        "define BadRec x = BadRecx", 18);
    test("a recursive name ending in a digit needs no delimiter",
         parse("define Loop1 x = Loop1x"), "Loop1");
    test("compact recursive adjacency stores YI",
         parse("show Loop1"), "arity:1 YI");
    test("set registers a reducible body for show",
         parse("set ShRed = 0 Kxy"), "ShRed");
    test("show exposes a reducible stored body without reducing it",
         parse("show ShRed"), "arity:0 Kxy");
    test_parse_failure(
        "show rejects an applied user basis", "show ShRed z", 5,
        "ShRed z is not a defined name");
    test("show does not change the definition list",
         [] {
             auto const before = set_list();
             static_cast<void>(parse("show ShRed"));
             std::cout << (set_list() == before ? "unchanged" : "changed");
         },
         "unchanged");

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
    test("set accepts an explicit zero arity",
         parse("set SetK0 = 0 K"), "SetK0");
    test("explicit zero-arity set basis expands without arguments",
         single_step(parse("SetK0")), "K");
    test("set accepts a unary arity",
         parse("set SetI1 = 1 I"), "SetI1");
    test("unary set basis remains named while undersaturated",
         single_step(parse("SetI1")), "SetI1");
    test("unary set basis contracts when saturated",
         single_step(parse("SetI1 x")), "x");
    test("basis step exposes a unary set basis definition",
         single_step(parse("SetI1 x"), true), "Ix");
    test("set accepts a binary arity",
         parse("set SetK2 = 2 K"), "SetK2");
    test("binary set basis remains named while undersaturated",
         single_step(parse("SetK2 x")), "SetK2x");
    test("binary set basis contracts when saturated",
         single_step(parse("SetK2 x y")), "x");
    test("binary set basis preserves trailing arguments",
         single_step(parse("SetK2 x y z")), "xz");
    test("set accepts parser whitespace around an explicit arity",
         parse(" \tset\nWsAr2 \r=\v2\f K"), "WsAr2");
    test("whitespace-defined binary set basis contracts",
         single_step(parse("WsAr2 x y")), "x");
    test("set without required whitespace remains symbols",
         parse("setx"), "setx");
    test("first set definition",
         parse("set DynDup=I"), "DynDup");
    test("identical set definition needs no replacement",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set DynDup = 0 I",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << (inspected.replaced_definition.empty()
                 ? "unchanged"
                 : inspected.replaced_definition);
         },
         "unchanged");
    test("changed set definition identifies what it replaces",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set DynDup=K",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "DynDup=0 I");
    auto dyn_dup_snapshot = parse("DynDup x");
    test("inspection does not replace the existing definition",
         single_step(parse("DynDup x")), "Ix");
    test("later changed set definition remains usable",
         single_step(parse("set DynDup=K")), "K");
    test("changed set definition wins future parser lookup",
         single_step(parse("DynDup x")), "Kx");
    test("an earlier parsed expression keeps its definition snapshot",
         single_step(dyn_dup_snapshot), "Ix");
    auto const dyn_dup_set_list = set_list();
    test("identical replacement does not add saved history",
         [&dyn_dup_set_list] {
             static_cast<void>(parse("set DynDup = 0 K"));
             std::cout << (set_list() == dyn_dup_set_list
                 ? "unchanged"
                 : "changed");
         },
         "unchanged");
    test_parse_failure(
        "set cannot replace a primitive",
        "set K=I",
        4,
        "K is a pre-defined basis and cannot be redefined");
    test("failed primitive replacement leaves K unchanged",
         parse("K"), "K");
    test("history registers an initial dependency",
         parse("set HistA=I"), "HistA");
    test("history registers a basis that captures the dependency",
         parse("set HistB=HistA"), "HistB");
    test("history replaces the dependency for future parsing",
         parse("set HistA=K"), "HistA");
    test("captured dependency keeps its earlier snapshot",
         single_step(single_step(parse("HistB x"))), "Ix");
    test("the replaced dependency uses its new definition",
         single_step(parse("HistA x")), "Kx");
    test("same source can change after a captured dependency changes",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set HistB=HistA",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "HistB=0 HistA");
    test("saved history preserves dependency replacement order",
         [] {
             constexpr std::string_view suffix =
                 "set HistA = 0 I\n"
                 "set HistB = 0 HistA\n"
                 "set HistA = 0 K";
             std::cout << (set_list().ends_with(suffix)
                 ? "ordered"
                 : "not ordered");
         },
         "ordered");
    test("a deeply nested equivalent definition is unchanged",
         [] {
             std::string definition = "set DeepEq = ";
             definition.append(300, 'x');
             static_cast<void>(parse(definition));
             auto inspected = combdsl::detail::parse_input(
                 definition,
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << (inspected.replaced_definition.empty()
                 ? "unchanged"
                 : "changed");
         },
         "unchanged");
    test("a user definition can precede a C++ basis registration",
         parse("set LateCpp=I"), "LateCpp");
    test("a C++ basis cannot take an existing user name",
         [] {
             try {
                 static_cast<void>(basis("LateCpp", 0, K));
                 std::cout << "accepted";
             } catch (std::invalid_argument const&) {
                 std::cout << "rejected";
             }
         },
         "rejected");
    test("rejected C++ registration leaves the user definition",
         parse("show LateCpp"), "arity:0 I");
    test("rejected C++ registration preserves user save history",
         [] {
             std::cout << (set_list().find(
                 "set LateCpp = 0 I") != std::string::npos
                 ? "preserved"
                 : "missing");
         },
         "preserved");
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
    test("basis ending uppercase needs no trailing delimiter",
         single_step(parse("A\\\\Bx")), "x");
    test("basis ending uppercase prints compactly before a symbol",
         parse("A\\\\Bx"), "A\\\\Bx");
    static_cast<void>(basis("Tail+", 1, I));
    test("basis ending punctuation needs no trailing delimiter",
         single_step(parse("Tail+x")), "x");
    test("basis ending punctuation prints compactly before a symbol",
         parse("Tail+x"), "Tail+x");
    static_cast<void>(
        basis("Utf\xE2\x97\x8F", 1, I));
    test("basis ending UTF-8 needs no trailing delimiter",
         single_step(parse("Utf\xE2\x97\x8F" "x")), "x");
    test("basis ending UTF-8 prints compactly before a symbol",
         parse("Utf\xE2\x97\x8F" "x"),
         "Utf\xE2\x97\x8F" "x");
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
        "M", "W", "B", "O", "T", "U", "N", "R", "C", "C*", "C**",
        "W*", "W**", "Q", "Q1", "Q3", "V", "D", "L", "W1", "Z", "A", "E",
        "F", "G", "H", "J", "Cstar", "Vstar", "V4", "G2", "G1",
        "bazTest", "Hprime", "H1",
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
    test("parse compact Turing",
         single_step(parse("Uxy")), "y(xxy)");
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
    test("parse G2 without a trailing delimiter", parse("G2x"), "G2x");
    test("parse separated V4", single_step(parse("V4 x y z")), "V4xyz");
    test("parse V4 without a trailing delimiter",
         parse("V4x"), "V4x");
    test("maximum-length digit-ending basis prints compactly",
         parse("123456789012345x"), "123456789012345x");
    test("maximum-length digit-ending basis needs no delimiter",
         single_step(parse("123456789012345x")), "x");
    static_cast<void>(basis("Long1", 1, K));
    static_cast<void>(basis("Long12", 1, I));
    test("longest eligible basis prefix wins",
         single_step(parse("Long12x")), "x");
    static_cast<void>(basis("Exact1", 1, K));
    static_cast<void>(basis("Exact1x", 0, I));
    test("exact longer basis wins over an eligible prefix",
         single_step(parse("Exact1x")), "I");
    test("basis automatically registers seven-character name",
         single_step(parse("1234567 x")), "x");
    test("basis automatically registers fifteen-character name",
         single_step(parse("123456789012345 x")), "x");
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
    test("parse compact Eagle",
         single_step(parse("Exyzwv")), "xy(zwv)");
    test("parse compact Finch",
         single_step(parse("Fxyz")), "zyx");
    test("show Finch exposes CV",
         parse("show F"), "arity:3 CV");
    test("basis step exposes CV for Finch",
         single_step(parse("Fxyz"), true), "CVxyz");
    test("parse Goldfinch",
         single_step(parse("G x y z w")), "xw(yz)");
    test("parse compact Hummingbird",
         single_step(parse("Hxyz")), "xyzy");
    test("parse compact Jay",
         single_step(parse("Jxyzw")), "xy(xwz)");
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
    test_parse_failure("parse uppercase symbol", "P", 0);
    test_parse_failure("parse numeric symbol", "x2", 1);
    test_parse_failure(
        "parse punctuation symbol", "@", 0, "unknown operand");
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
    test_parse_failure(
        "show requires a combinator name", "show ", 5,
        "missing combinator name");
    test_parse_failure(
        "show rejects an invalid name", "show @", 5,
        "@ is not a defined name");
    test_parse_failure(
        "show rejects a trailing close parenthesis", "show M)", 5,
        "M) is not a defined name");
    test_parse_failure(
        "find requires a question-mark marker", "find", 4,
        "expected '?'");
    test_parse_failure(
        "find all requires a question-mark marker", "find all", 8,
        "expected '?'");
    test_parse_failure(
        "find 4 requires a question-mark marker", "find 4", 6,
        "expected '?'");
    test_parse_failure(
        "find requires symbols after its marker", "find ?", 6,
        "expected at least one symbol");
    test_parse_failure(
        "find does not allow whitespace after its marker",
        "find ? x = x", 6, "expected at least one symbol");
    test_parse_failure(
        "find rejects a missing marker", "find x = x", 5,
        "expected '?'");
    test_parse_failure(
        "find requires an equals sign", "find ?xy", 8,
        "expected '='");
    test_parse_failure(
        "find requires an expression", "find ?xy = ", 11,
        "expected an expression");
    test_parse_failure(
        "find rejects uppercase symbols", "find ?xY = x", 7,
        "expected a lowercase symbol or '='");
    test_parse_failure(
        "find size requires separating whitespace", "find 4?x = x", 6,
        "expected whitespace after find maximum size");
    test_parse_failure(
        "find all requires separating whitespace",
        "find all4 ?x = x", 8,
        "expected whitespace after 'all'");
    test_parse_failure(
        "find options have a fixed order", "find 4 all ?x = x", 7,
        "expected '?'");
    test_parse_failure(
        "find rejects a zero maximum", "find 0 ?x = x", 5,
        "find maximum size must be from 1 to 4");
    test_parse_failure(
        "find rejects a maximum above four", "find 5 ?x = x", 5,
        "find maximum size must be from 1 to 4");
    test_parse_failure(
        "find parses a multi-digit maximum before rejecting it",
        "find all 10 ?x = x", 9,
        "find maximum size must be from 1 to 4");
    constexpr std::string_view reserved_definition_names[] = {
        "all", "set", "define", "show", "single", "key", "basis",
        "colorize", "about", "birds", "find", "help", "load", "remove",
        "save", "quit", "exit"};
    for (auto const name : reserved_definition_names) {
        auto const detail =
            std::string(name) + " is a reserved word";
        auto const set_source =
            std::string("set ") + std::string(name) + " = I";
        auto const set_title =
            std::string("set rejects reserved word ") +
            std::string(name);
        test_parse_failure(set_title, set_source, 4, detail);

        auto const define_source =
            std::string("define ") + std::string(name) + " x = x";
        auto const define_title =
            std::string("define rejects reserved word ") +
            std::string(name);
        test_parse_failure(define_title, define_source, 7, detail);
    }
    test_parse_failure("set requires a basis name", "set = I", 4);
    test_parse_failure("set requires an equals sign", "set NoEq I", 9);
    test_parse_failure("set requires an expression", "set Empty = \t", 13);
    constexpr std::string_view arity_without_expression =
        "set ArOnly = 2";
    test_parse_failure(
        "set arity requires an expression",
        arity_without_expression,
        arity_without_expression.size());
    constexpr std::string_view glued_arity = "set Glued = 2I";
    test_parse_failure(
        "set arity requires whitespace before its expression",
        glued_arity,
        glued_arity.find('2'));
    constexpr std::string_view negative_arity = "set NegAr = -1 I";
    test_parse_failure(
        "set rejects a negative arity",
        negative_arity,
        negative_arity.find('-'));
    constexpr std::string_view fractional_arity = "set FracAr = 1.5 I";
    test_parse_failure(
        "set rejects a fractional arity",
        fractional_arity,
        fractional_arity.find('1'));
    constexpr std::string_view bracketed_arity = "set Brack = [2] K";
    test_parse_failure(
        "set rejects a bracketed arity",
        bracketed_arity,
        bracketed_arity.find('['));
    constexpr std::string_view overflowing_arity =
        "set PovAr = 999999999999999999999999999999999999999 I";
    test_parse_failure(
        "set rejects an overflowing arity",
        overflowing_arity,
        overflowing_arity.find('9'));
    test_parse_failure(
        "overflowing arity does not register its name", "PovAr", 0);
    constexpr std::string_view overlong_set_name =
        "set 1234567890123456=I";
    test_parse_failure(
        "set rejects an overlong basis name",
        overlong_set_name,
        overlong_set_name.find("1234567890123456") + 15);
    test_parse_failure("set rejects an invalid basis name",
                       "set (Bad=I", 4);
    test_parse_failure("set name ends at a left parenthesis",
                       "set Pparen(I=K", 10);
    test_parse_failure("parenthesized set name is not registered",
                       "Pparen", 0);
    test_parse_failure("set rejects an invalid expression",
                       "set Pbad = K@", 12);
    test_parse_failure("failed set does not register its name", "Pbad", 0);
    test_parse_failure("set rejects a trailing close parenthesis",
                       "set Ptail=I)", 11);
    test_parse_failure("trailing set error does not register its name",
                       "Ptail", 0);
    test_parse_failure(
        "define requires a basis name", "define = x", 7);
    constexpr std::string_view define_without_symbols =
        "define NoArgs = x";
    test_parse_failure(
        "define requires at least one symbol",
        define_without_symbols,
        define_without_symbols.find('='));
    constexpr std::string_view uppercase_define_symbol =
        "define PUp X = x";
    test_parse_failure(
        "define rejects an uppercase symbol",
        uppercase_define_symbol,
        uppercase_define_symbol.find('X'));
    constexpr std::string_view numeric_define_symbol =
        "define PDg x2 = x";
    test_parse_failure(
        "define rejects a numeric symbol",
        numeric_define_symbol,
        numeric_define_symbol.find('2'));
    constexpr std::string_view quoted_define_symbol =
        "define BadRaw \\\"x\\\" = x";
    test_parse_failure(
        "define rejects a symbolic-string parameter",
        quoted_define_symbol,
        quoted_define_symbol.find('\\'));
    constexpr std::string_view define_without_equals =
        "define NoEqD x";
    test_parse_failure(
        "define requires an equals sign",
        define_without_equals,
        define_without_equals.size());
    constexpr std::string_view define_without_expression =
        "define EmptyD x = \t";
    test_parse_failure(
        "define requires an expression",
        define_without_expression,
        define_without_expression.size());
    constexpr std::string_view overlong_define_name =
        "define 1234567890123456 x = I";
    test_parse_failure(
        "define rejects an overlong basis name",
        overlong_define_name,
        overlong_define_name.find("1234567890123456") + 15);
    constexpr std::string_view invalid_define_body =
        "define PdBody x = K@";
    test_parse_failure(
        "define rejects an invalid expression",
        invalid_define_body,
        invalid_define_body.find('@'));
    test_parse_failure(
        "failed define does not register its name", "PdBody", 0);
    constexpr std::string_view trailing_define_close =
        "define PTailD x = I)";
    test_parse_failure(
        "define rejects a trailing close parenthesis",
        trailing_define_close,
        trailing_define_close.find(')'));
    test_parse_failure(
        "trailing define error does not register its name", "PTailD", 0);
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
    test("parse eval reports completed reductions",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             parse_eval(
                 "IIIx",
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 });
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "123/x\n");
    test("parse eval set only registers its definition",
         [&] { parse_eval("set EvalK=K"); }, "");
    test("parse eval uses a silently registered set basis",
         [&] { parse_eval("EvalK x y"); }, "x\n");
    test("parse eval positive-arity set only registers its definition",
         [&] { parse_eval("set EvalI1 = 1 I"); }, "");
    test("parse eval uses a silently registered positive-arity basis",
         [&] { parse_eval("EvalI1 x"); }, "x\n");
    test("parse eval does not mistake setx for a definition",
         [&] { parse_eval("setx"); }, "setx\n");
    test("parse eval define only registers its definition",
         [&] { parse_eval("define EvalD x = x"); }, "");
    test("parse eval uses a silently registered define basis",
         [&] { parse_eval("EvalD y"); }, "y\n");
    test("parse eval remove only changes its definition state",
         [] {
             parse_eval("set EvalRemove = 0 I");
             parse_eval("remove EvalRemove");
         },
         "");
    test_parse_failure(
        "parse eval remove makes the name undefined",
        "show EvalRemove",
        5,
        "EvalRemove is not a defined name");
    test("parse and step define only registers its definition",
         [] {
             std::istringstream input;
             std::ostringstream output;
             parse_and_step(
                 "define StepD x = x", output, input);
             std::cout << output.str();
         },
         "");
    test("parse eval uses the parse-and-step define basis",
         [&] { parse_eval("StepD z"); }, "z\n");
    test("parse eval does not mistake definex for a definition",
         [&] { parse_eval("definex"); }, "definex\n");
    test("parse eval reports a missing define name",
         [&] {
             try {
                 parse_eval("define");
             } catch (parse_error const& error) {
                 std::cout << error.what();
             }
         },
         "Parse error at position 7: missing combinator name");
    test("parse eval show displays a definition without reducing it",
         [&] { parse_eval("show ShRed"); }, "arity:0 Kxy\n");
    test("parse eval show identifies a fundamental name",
         [&] { parse_eval("show I"); },
         "I is a fundamental name with arity:1\n");
    test(
        "parse eval show rejects an undefined name",
        [&] {
            try {
                parse_eval("show Ix");
            } catch (parse_error const& error) {
                std::cout << error.what();
            }
        },
        "Parse error at position 6: Ix is not a defined name");
    test("parse eval does not mistake showx for a command",
         [&] { parse_eval("showx"); }, "showx\n");
    test("parse eval reports a missing show name",
         [&] {
             try {
                 parse_eval("show");
             } catch (parse_error const& error) {
                 std::cout << error.what();
             }
         },
         "Parse error at position 5: missing combinator name");
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
         "Ix\n"
         "x\n");
    test("parse and step set only registers its definition",
         [&] { parse_and_step("set StepI=I"); }, "");
    test("parse and step uses a silently registered set basis",
         [&] { parse_and_step("StepI x"); },
         "Ix\n"
         "x\n");
    test("parse and step show displays without stepping",
         [&] { parse_and_step("show ShRed"); }, "arity:0 Kxy\n");
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
         "Ix\n"
         "x\n");
    test("parse and key step",
         [&] {
             std::istringstream input("\n\n\n");
             parse_and_key_step("SKIx", std::cout, input);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "SKIx\n"
         "Ix\n"
         "x\n");
    test("parse and key step set only registers its definition",
         [&] {
             std::istringstream input;
             parse_and_key_step(
                 "set KeyStepI=I", std::cout, input);
         },
         "");
    test("parse and key step show displays without stepping",
         [&] {
             std::istringstream input;
             parse_and_key_step("show ShRed", std::cout, input);
         },
         "arity:0 Kxy\n");
    test("parse and key step forwards basis step",
         [&] {
             std::istringstream input("\n\n\n\n\n");
             parse_and_key_step("Mx", std::cout, input, true);
         },
         "Press Enter for one reduction step; type q then Enter to quit.\n"
         "Mx\n"
         "SIIx\n"
         "Ix(Ix)\n"
         "x(Ix)\n"
         "xx\n");
    const auto quoted_ski_x = quote(S)(K)(I)(x);
    test("quote SKIx", quoted_ski_x, "SKIx");
    test("takeout equal symbol",
         takeout(quoted_atomic{x}, quote(x)), "I");
    test("takeout separately parsed equal symbol",
         takeout(quoted_atomic{quote(x)}, parse("x")), "I");
    test("takeout equal symbolic string",
         takeout(quoted_atomic{"x"}, quote("x")), "I");
    test("takeout separately parsed equal symbolic string",
         takeout(quoted_atomic{quote("x")}, parse("\\\"x\\\"")), "I");
    test("takeout symbol does not equal symbolic string",
         takeout(quoted_atomic{x}, quote("x")), "Kx");
    test("takeout symbolic string does not equal symbol",
         takeout(quoted_atomic{"x"}, quote(x)), "Kx");
    test("takeout equal UTF-8 symbol",
         takeout(quoted_atomic{circle}, quote(symbol("\xE2\x97\x8F"))),
         "I");
    test("takeout unequal symbol is constant",
         takeout(quoted_atomic{x}, quote(y)), "Ky");
    test("takeout absent primitive is constant",
         takeout(quoted_atomic{x}, quote(K)), "KK");
    test("takeout absent from application is constant",
         takeout(quoted_atomic{x}, quote(y)(z)), "K(yz)");
    test("takeout absent from nested application is constant",
         takeout(quoted_atomic{x}, quote(y)(quote(z)(w))),
         "K(y(zw))");
    test("takeout category distinction inside application",
         takeout(quoted_atomic{x}, quote(y)("x")), "K(yx)");
    test("takeout repeated symbol is Mockingbird",
         takeout(quoted_atomic{x}, quote(x)(x)), "M");
    test("takeout repeated symbolic string is Mockingbird",
         takeout(quoted_atomic{"x"}, quote("x")("x")), "M");
    test("takeout matching function with constant argument is Thrush",
         takeout(quoted_atomic{x}, quote(x)(y)), "Ty");
    test("takeout matching function with compound constant argument",
         takeout(quoted_atomic{x}, quote(x)(quote(y)(z))),
         "T(yz)");
    test("takeout matching function respects atom categories",
         takeout(quoted_atomic{x}, quote(x)("x")), "Tx");
    test("takeout matching argument returns function",
         takeout(quoted_atomic{x}, quote(y)(x)), "y");
    test("takeout matching argument returns compound function",
         takeout(quoted_atomic{x}, quote(y)(z)(x)), "yz");
    test("takeout matching argument respects atom categories",
         takeout(quoted_atomic{"x"}, quote(x)("x")), "x");
    test("takeout matching argument recursively uses Warbler",
         takeout(quoted_atomic{x}, quote(y)(x)(x)), "Wy");
    test("takeout Warbler recursion reaches Mockingbird",
         takeout(quoted_atomic{x}, quote(x)(x)(x)), "WM");
    test("takeout Warbler recursion reaches Thrush",
         takeout(quoted_atomic{x}, quote(x)(y)(x)),
         "W(Ty)");
    test("takeout Warbler recursion reaches Owl",
         takeout(
             quoted_atomic{x},
             quote(x)(quote(y)(x))(x)),
         "W(Oy)");
    test("takeout nested Warbler recursion",
         takeout(
             quoted_atomic{x},
             quote(y)(x)(x)(x)),
         "W(Wy)");
    test("takeout matching function recursively uses Owl",
         takeout(quoted_atomic{x}, quote(x)(quote(y)(x))), "Oy");
    test("takeout Owl recursion reaches Mockingbird",
         takeout(quoted_atomic{x}, quote(x)(quote(x)(x))), "OM");
    test("takeout Owl recursion reaches Thrush",
         takeout(quoted_atomic{x}, quote(x)(quote(x)(y))),
         "O(Ty)");
    test("takeout nested Owl recursion",
         takeout(
             quoted_atomic{x},
             quote(x)(quote(x)(quote(y)(x)))),
         "O(Oy)");
    test("takeout function-dependent application uses Cardinal",
         takeout(quoted_atomic{x}, quote(y)(x)(z)), "Cyz");
    test("takeout Cardinal recursion reaches Mockingbird",
         takeout(quoted_atomic{x}, quote(x)(x)(y)), "CMy");
    test("takeout Cardinal recursion reaches Thrush",
         takeout(quoted_atomic{x}, quote(x)(y)(z)),
         "C(Ty)z");
    test("takeout Cardinal recursion reaches Owl",
         takeout(
             quoted_atomic{x},
             quote(x)(quote(y)(x))(z)),
         "C(Oy)z");
    test("takeout Cardinal recursion reaches Warbler",
         takeout(quoted_atomic{x}, quote(y)(x)(x)(z)),
         "C(Wy)z");
    test("takeout nested Cardinal recursion",
         takeout(quoted_atomic{x}, quote(y)(x)(z)(w)),
         "C(Cyz)w");
    const std::vector<quoted_atomic> pending_cardinal_z{
        quoted_atomic{z}};
    test("contextual Cardinal when only qarg contains next pending atom",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(x)(z),
             pending_cardinal_z),
         "Cyz");
    test("contextual Robin when only t contains next pending atom",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(x)(y),
             pending_cardinal_z),
         "Ryz");
    const std::vector<quoted_atomic> pending_cardinal_w_x_y{
        quoted_atomic{w},
        quoted_atomic{x},
        quoted_atomic{y}};
    test("next atom in qarg takes priority over its lower count",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(w)(x)(z)(y),
             pending_cardinal_w_x_y),
         "C(wx)y");
    test("next atom in t takes priority over its lower count",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(y)(z)(quote(w)(x)),
             pending_cardinal_w_x_y),
         "R(wx)y");
    test("contextual Cardinal when both contain next and counts tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(x)(z),
             pending_cardinal_z),
         "Czz");
    const std::vector<quoted_atomic> pending_cardinal_w_z{
        quoted_atomic{w},
        quoted_atomic{z}};
    test("contextual Cardinal when both contain next and qarg count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(x)(quote(w)(z)),
             pending_cardinal_w_z),
         "Cz(wz)");
    test("contextual Robin when both contain next and t count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(z)(x)(z),
             pending_cardinal_w_z),
         "Rz(wz)");
    test("contextual Cardinal when neither contains next and counts tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(x)(v),
             pending_cardinal_w_z),
         "Cyv");
    test("contextual Cardinal when neither contains next and qarg count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(x)(w),
             pending_cardinal_w_z),
         "Cyw");
    test("contextual Robin when neither contains next and t count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(x)(y),
             pending_cardinal_w_z),
         "Ryw");
    const std::vector<quoted_atomic> pending_cardinal_x_y{
        quoted_atomic{x},
        quoted_atomic{y}};
    test("pending count counts atoms rather than occurrences",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(x)(x)(z)(x),
             pending_cardinal_x_y),
         "C(xx)x");
    const std::vector<quoted_atomic> pending_cardinal_w{
        quoted_atomic{w}};
    test("contextual Cardinal and Robin selection propagates recursively",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(x)(z)(w),
             pending_cardinal_w),
         "C(Cyz)w");
    test("takeout argument-dependent application uses Bluebird",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(z)(x))),
         "Byz");
    test("takeout Bluebird recursion reaches Mockingbird",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(x)(x))),
         "ByM");
    test("takeout Bluebird recursion reaches Thrush",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(x)(z))),
         "By(Tz)");
    test("takeout Bluebird recursion reaches Owl",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(x)(quote(z)(x)))),
         "By(Oz)");
    test("takeout Bluebird recursion reaches Warbler",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(z)(x)(x))),
         "By(Wz)");
    test("takeout Bluebird recursion reaches Cardinal",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(z)(x)(w))),
         "By(Czw)");
    test("takeout nested Bluebird recursion",
         takeout(
             quoted_atomic{x},
             quote(y)(quote(z)(quote(w)(x)))),
         "By(Bzw)");
    test("contextual Bluebird when no pending atoms tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(z)(x)),
             {}),
         "Byz");
    const std::vector<quoted_atomic> pending_y{
        quoted_atomic{y}};
    test("contextual Queer when only qfun contains next pending atom",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(z)(x)),
             pending_y),
         "Qzy");
    test("contextual Bluebird when neither contains next and counts tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(quote(z)(x)),
             pending_y),
         "Bwz");
    const std::vector<quoted_atomic> pending_y_z{
        quoted_atomic{y},
        quoted_atomic{z}};
    test("contextual Bluebird when only t contains next pending atom",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(z)(x)),
             pending_y_z),
         "Byz");
    const std::vector<quoted_atomic> pending_x_y{
        quoted_atomic{x},
        quoted_atomic{y}};
    test("contextual Queer tests next pending atom after abstraction",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(z)(x)),
             pending_x_y),
         "Qzy");
    const std::vector<quoted_atomic> pending_queer_w_x_y{
        quoted_atomic{w},
        quoted_atomic{x},
        quoted_atomic{y}};
    test("next atom in qfun takes priority over its lower count",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(y)(quote(w)(x)(z)),
             pending_queer_w_x_y),
         "Q(wx)y");
    test("next atom in t takes priority over its lower count",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(w)(x)(quote(y)(z)),
             pending_queer_w_x_y),
         "B(wx)y");
    const std::vector<quoted_atomic> pending_queer_w_y{
        quoted_atomic{w},
        quoted_atomic{y}};
    test("contextual Queer when both contain next and qfun count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(y)(quote(y)(x)),
             pending_queer_w_y),
         "Qy(wy)");
    test("contextual Bluebird when both contain next and t count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(w)(y)(x)),
             pending_queer_w_y),
         "By(wy)");
    test("contextual Bluebird when both contain next and counts tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(y)(quote(y)(x)),
             pending_queer_w_y),
         "Byy");
    test("contextual Queer when neither contains next and qfun count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(quote(z)(x)),
             pending_queer_w_y),
         "Qzw");
    test("contextual Bluebird when neither contains next and t count wins",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(quote(w)(x)),
             pending_queer_w_y),
         "Bzw");
    test("contextual Bluebird when neither contains next and counts tie",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(quote(v)(x)),
             pending_queer_w_y),
         "Bzv");
    test("Queer count counts atoms rather than occurrences",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(w)(w)(quote(w)(x)),
             pending_queer_w_y),
         "B(ww)w");
    test("contextual takeout propagates Queer selection recursively",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(z)(quote(y)(quote(w)(x))),
             pending_y),
         "Bz(Qwy)");
    const auto contextual_recursive_foo =
        combdsl::detail::make_quoted_rec_func(
            combdsl::detail::basis_label("foo"));
    const std::vector<quoted_atomic> pending_foo_x_y{
        quoted_atomic{contextual_recursive_foo},
        quoted_atomic{x},
        quoted_atomic{y}};
    test("z abstraction uses Queer when only qfun has pending atoms",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(y)(quote(w)(z)),
             pending_foo_x_y),
         "Qwy");
    test("z abstraction prioritizes qfun containing next pending atom",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{z},
             quote(y)(contextual_recursive_foo(z)),
             pending_foo_x_y),
         "Q foo y");
    const std::vector<quoted_atomic> pending_foo_x{
        quoted_atomic{contextual_recursive_foo},
        quoted_atomic{x}};
    test("y abstraction uses Queer when only qfun has pending atoms",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{y},
             quote(x)(quote(w)(y)),
             pending_foo_x),
         "Qwx");
    test("y abstraction keeps Bluebird when t contains x",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{y},
             quote(u)(quote(x)(y)),
             pending_foo_x),
         "Bux");
    const std::vector<quoted_atomic> pending_foo{
        quoted_atomic{contextual_recursive_foo}};
    test("x abstraction uses Queer when only qfun has pending atoms",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             contextual_recursive_foo(quote(z)(x)),
             pending_foo),
         "Qz foo");
    test("x abstraction keeps Bluebird when t contains foo",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(u)(contextual_recursive_foo(x)),
             pending_foo),
         "Bu foo");
    test("recursive function abstraction keeps Bluebird",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{contextual_recursive_foo},
             quote(y)(quote(z)(contextual_recursive_foo)),
             {}),
         "Byz");
    test("takeout leaves BCT unoptimized",
         takeout(quoted_atomic{x}, quote(C)(quote(T)(x))),
         "BCT");
    test("takeout leaves BB unoptimized",
         takeout(quoted_atomic{x}, quote(B)(B)(x)),
         "BB");
    test("takeout both-dependent application uses Starling",
         takeout(
             quoted_atomic{x},
             quote(y)(x)(quote(z)(x))),
         "Syz");
    test("takeout Starling recursion reaches Mockingbirds",
         takeout(
             quoted_atomic{x},
             quote(x)(x)(quote(x)(x))),
         "SMM");
    test("takeout Starling recursion reaches Thrush",
         takeout(
             quoted_atomic{x},
             quote(x)(y)(quote(z)(x))),
         "S(Ty)z");
    test("takeout Starling recursion reaches Owl and Warbler",
         takeout(
             quoted_atomic{x},
             quote(x)(quote(y)(x))(quote(z)(x)(x))),
         "S(Oy)(Wz)");
    test("takeout Starling respects symbolic strings",
         takeout(
             quoted_atomic{"x"},
             quote(y)("x")(quote(z)("x"))),
         "Syz");
    test("takeout nested Starling recursion",
         takeout(
             quoted_atomic{x},
             quote(y)(x)(quote(z)(x))(
                 quote(w)(x)(quote(v)(x)))),
         "S(Syz)(Swv)");
    test("xy subexpression search enumerates 129,958 candidates",
         [] {
             std::size_t count = 0;
             std::size_t labeling_count = 2;
             auto const& shapes =
                 combdsl::detail::symbol_application_shapes();
             for (std::size_t leaf_count = 1;
                  leaf_count <= 8;
                  ++leaf_count) {
                 count += shapes[leaf_count].size() *
                          labeling_count;
                 labeling_count *= 2;
             }
             std::cout << count;
         },
         "129958");
    test("xyz subexpression search enumerates 3,137,844 candidates",
         [] {
             std::size_t count = 0;
             std::size_t labeling_count = 3;
             auto const& shapes =
                 combdsl::detail::symbol_application_shapes();
             for (std::size_t leaf_count = 1;
                  leaf_count <= 8;
                  ++leaf_count) {
                 count += shapes[leaf_count].size() *
                          labeling_count;
                 labeling_count *= 3;
             }
             std::cout << count;
         },
         "3137844");
    test("subexpression search uses all Catalan tree shapes",
         [] {
             auto const& shapes =
                 combdsl::detail::symbol_application_shapes();
             for (std::size_t leaf_count = 1;
                  leaf_count <= 8;
                  ++leaf_count) {
                 if (leaf_count != 1) {
                     std::cout << ' ';
                 }
                 std::cout << shapes[leaf_count].size();
             }
         },
         "1 1 2 5 14 42 132 429");
    const auto xy_subexpression_match =
        combdsl::search_for_xy_subexp(S(B)(T));
    test("xy subexpression search finds a raw takeout result",
         [&] {
             if (xy_subexpression_match) {
                 xy_subexpression_match->source_expression();
             } else {
                 std::cout << "not found";
             }
         },
         "x(yx)");
    test("xy subexpression search returns unoptimized takeout",
         [&] {
             if (xy_subexpression_match) {
                 xy_subexpression_match->takeout_result();
             } else {
                 std::cout << "not found";
             }
         },
         "SBT");
    test("xy subexpression search reports candidates examined",
         [&] {
             if (xy_subexpression_match) {
                 std::cout <<
                     xy_subexpression_match->
                         examined_expression_count;
             }
         },
         "9");
    const auto combined_xy_subexpression_match =
        combdsl::search_for_subexp(S(B)(T));
    test("combined subexpression search tries xy first",
         [&] {
             if (combined_xy_subexpression_match) {
                 combined_xy_subexpression_match->
                     source_expression();
             } else {
                 std::cout << "not found";
             }
         },
         "x(yx)");
    test("combined xy search returns unoptimized takeout",
         [&] {
             if (combined_xy_subexpression_match) {
                 combined_xy_subexpression_match->
                     takeout_result();
             } else {
                 std::cout << "not found";
             }
         },
         "SBT");
    test("combined xy search reports its phase count",
         [&] {
             if (combined_xy_subexpression_match) {
                 std::cout <<
                     combined_xy_subexpression_match->
                         examined_expression_count;
             }
         },
         "9");
    const auto combined_xyz_subexpression_match =
        combdsl::search_for_subexp(
            B(Q(T))(B));
    test("combined subexpression search falls back to xyz",
         [&] {
             if (combined_xyz_subexpression_match) {
                 combined_xyz_subexpression_match->
                     source_expression();
             } else {
                 std::cout << "not found";
             }
         },
         "x(zy)");
    test("combined xyz search returns unoptimized takeout",
         [&] {
             if (combined_xyz_subexpression_match) {
                 combined_xyz_subexpression_match->
                     takeout_result();
             } else {
                 std::cout << "not found";
             }
         },
         "B(QT)B");
    test("combined xyz search reports cumulative count",
         [&] {
             if (combined_xyz_subexpression_match) {
                 std::cout <<
                     combined_xyz_subexpression_match->
                         examined_expression_count;
             }
         },
         "129978");
    const auto xyz_subexpression_match =
        combdsl::search_for_xyz_subexp(B(C)(T));
    test("xyz subexpression search finds a raw takeout result",
         [&] {
             if (xyz_subexpression_match) {
                 xyz_subexpression_match->source_expression();
             } else {
                 std::cout << "not found";
             }
         },
         "yxx");
    test("xyz subexpression search returns unoptimized takeout",
         [&] {
             if (xyz_subexpression_match) {
                 xyz_subexpression_match->takeout_result();
             } else {
                 std::cout << "not found";
             }
         },
         "B(BK)(W(BCT))");
    test("xyz subexpression search reports candidates examined",
         [&] {
             if (xyz_subexpression_match) {
                 std::cout <<
                     xyz_subexpression_match->
                         examined_expression_count;
             }
         },
         "49");
    test("subexpression matching includes the head",
         [] {
             std::cout <<
                 combdsl::detail::contains_quoted_subexpression(
                     quote(B),
                     quote(B)(K)(K));
         },
         "1");
    test("subexpression matching includes an argument",
         [] {
             std::cout <<
                 combdsl::detail::contains_quoted_subexpression(
                     quote(K),
                     quote(B)(K)(K));
         },
         "1");
    test("subexpression matching rejects an absent expression",
         [] {
             std::cout <<
                 combdsl::detail::contains_quoted_subexpression(
                     quote(Y),
                     quote(B)(K)(K));
         },
         "0");
    std::array const j_match_symbols{
        quoted_atomic{x},
        quoted_atomic{y},
        quoted_atomic{z},
        quoted_atomic{w},
    };
    auto const j_match_expression =
        quote(x)(y)(quote(x)(w)(z));
    std::array const applicator_match_symbols{
        quoted_atomic{x},
        quoted_atomic{y},
    };
    auto const applicator_match_expression =
        quote(x)(quote(y)(x));
    auto const applicator_single_matches =
        combdsl::check_for_singles_match(
            applicator_match_symbols,
            applicator_match_expression);
    test("single matching finds the Applicator bird",
         [&] {
             for (auto const& match : applicator_single_matches) {
                 match.print_to(std::cout);
             }
         },
         "A");
    test("find default stops at the first matching size",
         parse("find ?xy = x(yx)"), "?=A");
    test("find size one restricts results to singles",
         parse("find 1 ?xy = x(yx)"), "?=A");
    test("find size two still stops after a single match",
         parse("find 2 ?xy = x(yx)"), "?=A");
    test("find all size two includes singles and pairs",
         [] {
             std::ostringstream output;
             parse("find all 2 ?xy = x(yx)").print_to(output);
             auto const padded = '\n' + output.str() + '\n';
             auto const contains_line = [&](std::string_view line) {
                 return padded.find(
                     '\n' + std::string(line) + '\n') !=
                     std::string::npos;
             };
             std::cout << contains_line("?=A")
                       << contains_line("?=CO")
                       << contains_line("?=HB")
                       << contains_line("?=SBT");
         },
         "1110");
    test("find all default continues through size three",
         [] {
             std::ostringstream output;
             parse("find all ?xy = x(yx)").print_to(output);
             auto const padded = '\n' + output.str() + '\n';
             auto const line_position = [&](std::string_view line) {
                 return padded.find(
                     '\n' + std::string(line) + '\n');
             };
             auto const single = line_position("?=A");
             auto const pair = line_position("?=CO");
             auto const triple = line_position("?=SBT");
             std::cout
                 << (single != std::string::npos)
                 << (pair != std::string::npos)
                 << (triple != std::string::npos)
                 << (single < pair && pair < triple);
         },
         "1111");
    test("find accepts a multi-digit maximum with leading zeros",
         [] {
             std::ostringstream ordinary;
             std::ostringstream leading_zeros;
             parse("find 2 ?xy = x(yx)").print_to(ordinary);
             parse("find 0002 ?xy = x(yx)").print_to(
                 leading_zeros);
             std::cout << (ordinary.str() == leading_zeros.str());
         },
         "1");
    test("find command accepts leading whitespace and size four",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 " \tfind 4 ?xy = x(yx)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             parsed.expression.print_to(std::cout);
         },
         "x(yx)");
    test("find command is display only",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 "find ?xy = x(yx)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << parsed.is_display_only
                       << parsed.is_find
                       << parsed.is_find_no_match
                       << parsed.is_definition;
         },
         "1100");
    test("find exposes no-match metadata",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 "find all 4 ?x = MM");
             parsed.expression.print_to(std::cout);
             std::cout << ' ' << parsed.is_find_no_match;
         },
         "No match within search bounds 1");
    test("check for match appends symbols in order",
         [&] {
             std::cout << combdsl::check_for_match(
                 I(J),
                 j_match_symbols,
                 j_match_expression);
         },
         "1");
    test("check for match rejects a different reduction",
         [&] {
             std::cout << combdsl::check_for_match(
                 I(K),
                 j_match_symbols,
                 j_match_expression);
         },
         "0");
    test("check for match safely rejects a reduction cycle",
         [&] {
             std::cout << combdsl::check_for_match(
                 M(M),
                 j_match_symbols,
                 j_match_expression);
         },
         "0");
    test("pair and trip search catalog excludes J and Y",
         [] {
             bool contains_j = false;
             bool contains_y = false;
             for (auto const& combinator :
                  combdsl::detail::predefined_bird_combinators()) {
                 contains_j =
                     contains_j ||
                     combdsl::detail::same_parser_definition_expression(
                         combinator, quote(J));
                 contains_y =
                     contains_y ||
                     combdsl::detail::same_parser_definition_expression(
                         combinator, quote(Y));
             }
             std::cout << contains_j << contains_y;
         },
         "00");
    test("fixed match pair exclusions include I anything",
         [] {
             std::size_t excluded_count = 0;
             auto const& combinators =
                 combdsl::detail::predefined_bird_combinators();
             for (auto const& function : combinators) {
                 for (auto const& argument : combinators) {
                     if (combdsl::detail::is_excluded_match_pair(
                             function, argument)) {
                         ++excluded_count;
                     }
                 }
             }
             std::cout
                 << excluded_count << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(I), quote(A))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(I), quote(Z))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(M))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(U))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(U), quote(M))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(U), quote(U))
                 << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(A), quote(I));
         },
         "33 111111 0");
    std::vector<combdsl::quoted_expression> first_pairs;
    first_pairs.reserve(2);
    std::size_t generated_pair_count = 0;
    combdsl::detail::for_each_predefined_bird_pair(
        [&](combdsl::quoted_expression pair) {
            if (first_pairs.size() < 2) {
                first_pairs.push_back(std::move(pair));
            }
            ++generated_pair_count;
        });
    test("pair generator skips fixed terrible twos",
         [&] {
             std::cout << generated_pair_count << ' ';
             if (first_pairs.size() >= 2) {
                 first_pairs[0].print_to(std::cout);
                 std::cout << ' ';
                 first_pairs[1].print_to(std::cout);
             } else {
                 std::cout << first_pairs.size();
             }
         },
         "808 AA AB");
    auto const j_pair_matches =
        combdsl::check_for_pairs_match(
            j_match_symbols,
            j_match_expression);
    test("pair matching searches 808 ordered pairs without J or Y",
         [&] {
             std::cout << j_pair_matches.size();
         },
         "0");
    std::vector<combdsl::quoted_expression> first_trips;
    first_trips.reserve(2);
    std::size_t generated_trip_count = 0;
    bool generated_saturated_k = false;
    bool generated_right_partial_k = false;
    bool generated_left_sk = false;
    bool generated_right_sk = false;
    bool generated_right_identity = false;
    combdsl::detail::for_each_predefined_bird_trip(
        [&](combdsl::quoted_expression trip) {
            generated_saturated_k =
                generated_saturated_k ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(K)(A)(A));
            generated_right_partial_k =
                generated_right_partial_k ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(K)(quote(A)(A)));
            generated_left_sk =
                generated_left_sk ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(S)(K)(A));
            generated_right_sk =
                generated_right_sk ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(S)(quote(K)(A)));
            generated_right_identity =
                generated_right_identity ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(I)(quote(H)(E)));
            if (first_trips.size() < 2) {
                first_trips.push_back(std::move(trip));
            }
            ++generated_trip_count;
        });
    test("trip generator skips fixed terrible twos",
         [&] {
             std::cout << generated_trip_count << ' '
                       << generated_saturated_k
                       << generated_right_partial_k
                       << generated_left_sk
                       << generated_right_sk
                       << generated_right_identity << ' ';
             if (first_trips.size() >= 2) {
                 first_trips[0].print_to(std::cout);
                 std::cout << ' ';
                 first_trips[1].print_to(std::cout);
             } else {
                 std::cout << first_trips.size();
             }
         },
         "45186 01010 AAA A(AA)");
    auto const a_trip_target = quote(A)(quote(A)(A));
    auto const parallel_trip_matches =
        combdsl::check_for_trips_match(
            std::span<quoted_atomic const>{},
            a_trip_target);
    test("native parallel trip matching preserves candidate order",
         [&] {
             std::cout << parallel_trip_matches.size();
             if (parallel_trip_matches.size() >= 2) {
                 std::cout << ' ';
                 parallel_trip_matches[0].print_to(std::cout);
                 std::cout << ' ';
                 parallel_trip_matches[1].print_to(std::cout);
             }
         },
         "30 AAA A(AA)");
    test("AAA and A(AA) both match the same target",
         [&] {
             std::cout << combdsl::check_for_match(
                              quote(A)(A)(A),
                              std::span<quoted_atomic const>{},
                              a_trip_target)
                       << combdsl::check_for_match(
                              quote(A)(quote(A)(A)),
                              std::span<quoted_atomic const>{},
                              a_trip_target);
         },
         "11");
    std::vector<combdsl::quoted_expression> first_quad_shapes;
    combdsl::detail::for_each_predefined_bird_quad_at(
        0,
        [&](std::size_t, combdsl::quoted_expression quad) {
            first_quad_shapes.push_back(std::move(quad));
        });
    test("quad shape construction does not run exhaustive search",
         [&] {
             std::cout << first_quad_shapes.size();
             for (auto const& quad : first_quad_shapes) {
                 std::cout << ' ';
                 quad.print_to(std::cout);
             }
         },
         "5 AAAA AA(AA) A(AA)A A(AAA) A(A(AA))");
    auto quad_shape_presence =
        [](std::size_t first,
           std::size_t second,
           std::size_t third,
           std::size_t fourth) {
            constexpr auto combinator_count =
                combdsl::check_for_match_combinator_count;
            auto const tuple_index =
                ((first * combinator_count + second) *
                     combinator_count +
                 third) *
                    combinator_count +
                fourth;
            std::array<
                bool,
                combdsl::check_for_quads_match_shape_count>
                present{};
            combdsl::detail::for_each_predefined_bird_quad_at(
                tuple_index,
                [&](std::size_t shape_index,
                    combdsl::quoted_expression) {
                    present[shape_index] = true;
                });
            return present;
        };
    constexpr std::size_t a_index = 0;
    constexpr std::size_t c_star_star_index = 4;
    constexpr std::size_t e_index = 6;
    constexpr std::size_t h_index = 9;
    constexpr std::size_t i_index = 10;
    constexpr std::size_t k_index = 11;
    constexpr std::size_t m_index = 13;
    constexpr std::size_t s_index = 20;
    constexpr std::size_t u_index = 22;
    test("quad exclusions cover every pair and triplet position",
         [&] {
             auto print_presence =
                 [](auto const& presence) {
                     for (auto const present : presence) {
                         std::cout << present;
                     }
                 };
             print_presence(quad_shape_presence(
                 i_index, a_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, i_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, a_index, i_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, a_index, a_index, i_index));
             std::cout << " | ";
             std::array const mockingbird_turing_pairs{
                 std::pair{m_index, m_index},
                 std::pair{m_index, u_index},
                 std::pair{u_index, m_index},
                 std::pair{u_index, u_index},
             };
             bool first_pair = true;
             for (auto const [function, argument] :
                  mockingbird_turing_pairs) {
                 if (!first_pair) {
                     std::cout << " | ";
                 }
                 first_pair = false;
                 print_presence(quad_shape_presence(
                     function,
                     argument,
                     a_index,
                     a_index));
                 std::cout << ' ';
                 print_presence(quad_shape_presence(
                     a_index,
                     function,
                     argument,
                     a_index));
                 std::cout << ' ';
                 print_presence(quad_shape_presence(
                     a_index,
                     a_index,
                     function,
                     argument));
             }
             std::cout << " | ";
             print_presence(quad_shape_presence(
                 k_index, a_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, k_index, a_index, a_index));
             std::cout << " | ";
             print_presence(quad_shape_presence(
                 s_index, k_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, s_index, k_index, a_index));
         },
         "00000 11000 10110 11111 | "
         "00111 11001 10110 | "
         "00111 11001 10110 | "
         "00111 11001 10110 | "
         "00111 11001 10110 | "
         "01111 11101 | "
         "01101 11101");
    test("quad exclusions reject I applied to composite arguments",
         [&] {
             std::cout
                 << !quad_shape_presence(
                        c_star_star_index,
                        i_index,
                        h_index,
                        e_index)[4]
                 << !quad_shape_presence(
                        i_index,
                        c_star_star_index,
                        h_index,
                        e_index)[4];
         },
         "11");
    auto const divergent_quad_matches =
        combdsl::check_for_quads_match(
            std::span<quoted_atomic const>{},
            quote(M)(M));
    test("quad matching rejects divergent target before search",
         [&] {
             std::cout << divergent_quad_matches.size();
         },
         "0");
    const auto recursive_x = combdsl::detail::make_quoted_rec_func(
        combdsl::detail::basis_label("x"));
    test("recursive function atom prints like its name",
         recursive_x, "x");
    test("takeout matches the same recursive function atom",
         takeout(quoted_atomic{recursive_x}, recursive_x), "I");
    test("recursive function atom does not match a symbol",
         takeout(quoted_atomic{recursive_x}, quote(x)), "Kx");
    test("symbol atom does not match a recursive function",
         takeout(quoted_atomic{x}, recursive_x), "Kx");
    test("recursive function atom does not match a symbolic string",
         takeout(quoted_atomic{recursive_x}, quote("x")), "Kx");
    test("quoted atomic rejects a primitive",
         [] {
             try {
                 static_cast<void>(quoted_atomic{K});
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::quoted_atomic requires a quoted symbol or symbolic "
         "string or recursive function");
    test("quoted atomic rejects a named basis",
         [] {
             try {
                 static_cast<void>(quoted_atomic{M});
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::quoted_atomic requires a quoted symbol or symbolic "
         "string or recursive function");
    test("quoted atomic rejects another opaque value",
         [] {
             try {
                 static_cast<void>(quoted_atomic{42});
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::quoted_atomic requires a quoted symbol or symbolic "
         "string or recursive function");
    test("quoted atomic rejects an application",
         [] {
             try {
                 static_cast<void>(quoted_atomic{quote(x)(y)});
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::quoted_atomic requires a quoted symbol or symbolic "
         "string or recursive function");
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
    const auto s_with_sk_function =
        single_step(quote(S)(quote(S)(K))(y)(z));
    test("single step S with x equal to SK",
         s_with_sk_function, "SKz(yz)");
    test("next step contracts the SKz function",
         single_step(s_with_sk_function), "I(yz)");
    const auto s_with_sk_argument =
        single_step(quote(S)(x)(quote(S)(K))(z));
    test("single step S with y equal to SK",
         s_with_sk_argument, "xz(SKz)");
    test("next step contracts the SKz argument without parentheses",
         single_step(s_with_sk_argument), "xzI");
    const auto s_with_two_sk_arguments =
        single_step(
            quote(S)(quote(S)(K))(quote(S)(K))(z));
    test("single step S with x and y equal to SK",
         s_with_two_sk_arguments, "SKz(SKz)");
    test("next step contracts both SKz applications",
         single_step(s_with_two_sk_arguments), "II");
    test("paired SK contraction preserves later operands",
         single_step(
             quote(S)(K)(z)(quote(S)(K)(w))(u)),
         "IIu");
    const auto wm_expanded =
        single_step(quote(W)(M)(x), true);
    test("basis step expands WMx",
         wm_expanded, "SS(SK)Mx");
    const auto wm_after_s =
        single_step(wm_expanded, true);
    test("next WMx step applies the outer S",
         wm_after_s, "SM(SKM)x");
    test("next WMx step prioritizes the nested SKM",
         single_step(wm_after_s, true), "SMIx");
    test("single step SKIx", single_step(quoted_ski_x), "Ix");
    test("two steps SKIx", single_step(single_step(quoted_ski_x)), "x");
    test("single step native SKI partial", single_step(quote(S(K)(I))(x)),
         "Ix");
    test("single step contracts an exact SK application",
         single_step(quote(S)(K)(x)), "I");
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
    test("single step does not search arbitrary nested SK applications",
         single_step(
             quote(S)(M)(quote(S)(K)(M))(x)),
         "Mx(SKMx)");
    test("single step keeps ordinary head priority over nested SK",
         single_step(quote(I)(quote(S)(K)(M))), "SKM");
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
    test("quoted SK application stays structural as an operand",
         quote(x)(quote(S)(K)(y)), "x(SKy)");
    test("quoted native SK application stays structural as an operand",
         quote(x(S(K)(y))), "x(SKy)");
    test("quoted SK application stays structural with a trailing operand",
         quote(S)(K)(x)(y), "SKxy");
    test("nested quoted SK application stays structural",
         quote(z)(quote(S)(K)(x)(y)), "z(SKxy)");
    test("quoted SK application preserves a compound trailing operand",
         quote(S)(K)(x)(quote(y)(z)), "SKx(yz)");
    test("quoted SK application preserves all later operands",
         quote(S)(K)(x)(y)(z), "SKxyz");
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
    const auto bkm_self_first_step =
        single_step(parse("BKM(BKM)"));
    test("single step contracts self-referential BKM application",
         bkm_self_first_step, "K(M(BKM))");
    test("next BKM self-application step reduces inside K",
         single_step(bkm_self_first_step), "K(BKM(BKM))");
    test("eval scans a deeply nested discarded argument iteratively",
         [] {
             constexpr std::size_t depth = 100'000;
             // Keep every level alive so this test isolates traversal from
             // shared_ptr's separate recursive destruction behavior.
             std::vector<combdsl::quoted_expression> retained_levels;
             retained_levels.reserve(depth + 1);

             auto deep_argument = quote(z);
             retained_levels.push_back(deep_argument);
             for (std::size_t level = 0; level < depth; ++level) {
                 deep_argument = quote(y)(deep_argument);
                 retained_levels.push_back(deep_argument);
             }

             std::istringstream input;
             std::ostringstream output;
             eval(quote(K)(x)(deep_argument), output, input, false);
             std::cout << (output.str() == "x\n" ? "ok" : "failed");

             deep_argument = quote(z);
             while (!retained_levels.empty()) {
                 retained_levels.pop_back();
             }
         },
         "ok");
    test("single step finds a redex beneath 100000 applications",
         [] {
             constexpr std::size_t depth = 100'000;
             // Retaining each level also lets the two deep persistent trees
             // be released safely from their outermost nodes inward.
             std::vector<combdsl::quoted_expression> original_levels;
             original_levels.reserve(depth + 1);

             auto original = quote(I)(z);
             original_levels.push_back(original);
             for (std::size_t level = 0; level < depth; ++level) {
                 original = quote(x)(original);
                 original_levels.push_back(original);
             }

             auto reduced = single_step(original);
             auto current = reduced;
             std::vector<combdsl::quoted_expression> reduced_levels;
             reduced_levels.reserve(depth);
             bool correct = true;
             for (std::size_t level = 0; level < depth; ++level) {
                 auto const& root =
                     combdsl::detail::quoted_access::root(current);
                 if (root->kind() !=
                     combdsl::detail::quoted_node_kind::application) {
                     correct = false;
                     break;
                 }
                 reduced_levels.push_back(current);
                 auto const& application = static_cast<
                     combdsl::detail::quoted_application_node const&>(
                         *root);
                 if (!combdsl::detail::same_quoted_atom(
                         quote(x), application.function())) {
                     correct = false;
                     break;
                 }
                 current = application.argument();
             }
             correct =
                 correct &&
                 combdsl::detail::same_quoted_atom(quote(z), current);
             std::cout << (correct ? "ok" : "failed");

             auto const shallow = quote(z);
             reduced = shallow;
             current = shallow;
             for (auto& level : reduced_levels) {
                 level = shallow;
             }
             original = shallow;
             while (!original_levels.empty()) {
                 original_levels.pop_back();
             }
         },
         "ok");
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
    test("progress reporter reports every completed reduction",
         [] {
             std::vector<std::size_t> reports;
             evaluation_progress_callback callback =
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 };
             combdsl::detail::evaluation_progress_reporter reporter(
                 callback);
             for (std::size_t step = 0; step < 3; ++step) {
                 reporter.completed_reduction();
             }
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
         },
         "123");
    test("progress reporter accepts an empty callback",
         [] {
             evaluation_progress_callback callback;
             combdsl::detail::evaluation_progress_reporter reporter(
                 callback);
             reporter.completed_reduction();
             std::cout << "ok";
         },
         "ok");
    test("eval reports completed reductions",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             eval(
                 repeated_identity_expression(3),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 });
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "123/x\n");
    test("single step run reports completed reductions",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             single_step_run(
                 repeated_identity_expression(3),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 });
             auto const text = output.str();
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::size_t lines = 0;
             for (char value : text) {
                 if (value == '\n') {
                     ++lines;
                 }
             }
             std::cout << '/' << lines << '/'
                       << (text.ends_with("x\n") ? "x" : "wrong");
         },
         "123/3/x");
    test("eval prints only reduced expression",
         [&] { eval(quoted_ski_x); },
         "x\n");
    test("eval reduces inside head normal form",
         [&] { eval(quote(x)(quote(I)(y))); },
         "xy\n");
    test("eval leaves a partial K argument unreduced",
         [&] { eval(quote(K)(quote(I)(u))); },
         "K(Iu)\n");
    test("eval leaves a nested partial K argument unreduced",
         [&] { eval(quote(x)(quote(K)(quote(I)(u)))); },
         "x(K(Iu))\n");
    test("eval stops when BKM(BKM) produces a partial K",
         [&] { eval(parse("BKM(BKM)")); },
         "K(M(BKM))\n");
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
         "Ix\n"
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
         "Ix\n");
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
         "Ix\n"
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
    test("color step contracts SK application and then I",
         [&] {
             auto expression = color_step_html(quoted_ski_x);
             static_cast<void>(color_step_html(std::move(expression)));
         },
         std::string{"  "} +
             red_argument("SKI") +
             "x" +
             "\n->" +
             red_argument("I") +
             "x\n  I" +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             "\n");
    test("color step prints an unchanged normal form",
         [&] { static_cast<void>(color_step_html(quote(x))); },
         "  x\n"
         "->x\n");
    test("color step colors I argument",
         [&] { static_cast<void>(color_step_html(quote(I)(x))); },
         std::string{"  I"} +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             "\n");
    test("color step colors K arguments and preserves trailing operand",
         [&] {
             static_cast<void>(
                 color_step_html(quote(K)(x)(y)(w)));
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
                 color_step_html(quote(S)(x)(y)(z)));
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
    test("color step contracts SKM marked by the preceding S reduction",
         [&] {
             std::ostringstream first_step;
             auto pending = color_step_html(
                 wm_expanded, first_step, true);
             static_cast<void>(
                 color_step_html(std::move(pending), true));
         },
         std::string{"  SM"} +
             red_argument("(SKM)") +
             "x" +
             "\n->" +
             "SM" +
             red_argument("I") +
             "x\n");
    test("color step contracts SKz in function position",
         [&] {
             static_cast<void>(
                 color_step_html(s_with_sk_function));
         },
         std::string{"  "} +
             red_argument("SKz") +
             "(yz)\n->" +
             red_argument("I") +
             "(yz)\n");
    test("color step contracts SKz in argument position",
         [&] {
             static_cast<void>(
                 color_step_html(s_with_sk_argument));
         },
         std::string{"  xz"} +
             red_argument("(SKz)") +
             "\n->xz" +
             red_argument("I") +
             "\n");
    test("color step contracts both SKz applications",
         [&] {
             static_cast<void>(
                 color_step_html(s_with_two_sk_arguments));
         },
         std::string{"  "} +
             red_argument("SKz") +
             green_argument("(SKz)") +
             "\n->" +
             red_argument("I") +
             green_argument("I") +
             "\n");
    test("color step carries Y argument color into deferred Y",
         [&] { static_cast<void>(color_step_html(quote(Y)(x))); },
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
                 color_step_html(single_step(quote(Y)(x))));
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
                 color_step_html(quote(q)(quote(I)(x))));
         },
         std::string{"  q(I"} +
             red_argument("x") +
             ")\n->q" +
             red_argument("x") +
             "\n");
    test("color step carries unary basis argument through duplication",
         [&] { static_cast<void>(color_step_html(quote(M)(x))); },
         std::string{"  M"} +
             red_argument("x") +
             "\n->" +
             red_argument("x") +
             red_argument("x") +
             "\n");
    test("color step carries binary basis arguments through reordering",
         [&] { static_cast<void>(color_step_html(quote(T)(x)(y))); },
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
                 color_step_html(quote(C)(x)(y)(z)));
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
                 color_step_html(quote(D)(u)(v)(w)(x)));
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
    test("color step carries fifth basis argument color",
         [&] {
             static_cast<void>(
                 color_step_html(
                     quote(fifth_argument_projection)
                         (u)(v)(w)(x)(y)(z)));
         },
         std::string{"  Fifth"} +
             red_argument(" u") +
             green_argument("v") +
             blue_argument("w") +
             dark_orange_argument("x") +
             munsell_purple_argument("y") +
             "z\n->" +
             munsell_purple_argument("y") +
             "z\n");
    test("color step colored spacing precedes multicharacter basis",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(x)(Cstar)));
         },
         std::string{"  I"} +
             red_argument("x") +
             " Cstar\n->" +
             red_argument("x") +
             " Cstar\n");
    test("color step colors multicharacter basis with lexical spacing",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(Cstar)));
         },
         std::string{"  I"} +
             red_argument(" Cstar") +
             "\n->" +
             red_argument("Cstar") +
             "\n");
    test("color step compares structure rather than output",
         [&] {
             static_cast<void>(
                 color_step_html(quote(basis("K", 1, K))(x)));
         },
         std::string{"  K"} +
             red_argument("x") +
             "\n->K" +
             red_argument("x") +
             "\n");
    test("color step HTML-escapes expression text",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(std::string{"<&>\"'"})));
         },
         std::string{"  I"} +
             red_argument(" &lt;&amp;&gt;&quot;&#39;") +
             "\n->" +
             red_argument("&lt;&amp;&gt;&quot;&#39;") +
             "\n");
    test("color step preserves stream formatting",
         [&] {
             std::ostringstream output;
             output << std::hex;
             static_cast<void>(
                 color_step_html(quote(I)(255), output));
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
             auto result = color_step_html(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("color step accepts an output stream without a buffer",
         [&] {
             std::ostream output(nullptr);
             auto result = color_step_html(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("color step does not color zero-arity basis expansion",
         [&] {
             static_cast<void>(
                 color_step_html(quote(zero_arity_basis)));
         },
         "  Qzero\n"
         "->K\n");
    test("color step supports a custom output stream",
         [&] {
             std::ostringstream output;
             auto expression = color_step_html(quoted_ski_x, output);
             std::cout << output.str() << "returned: ";
             expression();
         },
         std::string{"  "} +
             red_argument("SKI") +
             "x" +
             "\n->" +
             red_argument("I") +
             "x\nreturned: Ix");
    test("color step colors only a basis expansion",
         [&] {
             auto result =
                 color_step_html(quote(M)(x)(y), true);
             std::cout << "returned: ";
             result();
         },
         std::string{"  "} +
             red_argument("M") +
             "xy\n->" +
             red_argument("SII") +
             "xy\nreturned: SIIxy");
    test("color step colors a zero-arity basis expansion",
         [&] {
             static_cast<void>(
                 color_step_html(
                     quote(zero_arity_basis), true));
         },
         std::string{"  "} +
             red_argument("Qzero") +
             "\n->" +
             red_argument("K") +
             "\n");
    test("terminal color step carries S argument colors through reduction",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(S)(x)(y)(z)));
         },
         std::string{"  S"} +
             terminal_red_argument("x") +
             terminal_green_argument("y") +
             terminal_blue_argument("z") +
             "\n->" +
             terminal_red_argument("x") +
             terminal_blue_argument("z") +
             "(" +
             terminal_green_argument("y") +
             terminal_blue_argument("z") +
             ")\n");
    test("terminal color step preserves trailing K operand",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(K)(x)(y)(w)));
         },
         std::string{"  K"} +
             terminal_red_argument("x") +
             terminal_green_argument("y") +
             "w\n->" +
             terminal_red_argument("x") +
             "w\n");
    test("terminal color step carries fourth and fifth basis colors",
         [&] {
             static_cast<void>(
                 color_step_ansi(
                     quote(fifth_argument_projection)
                         (u)(v)(w)(x)(y)(z)));
         },
         std::string{"  Fifth"} +
             terminal_red_argument(" u") +
             terminal_green_argument("v") +
             terminal_blue_argument("w") +
             terminal_dark_orange_argument("x") +
             terminal_munsell_purple_argument("y") +
             "z\n->" +
             terminal_munsell_purple_argument("y") +
             "z\n");
    test("terminal color step leaves normal form uncolored",
         [&] {
             static_cast<void>(color_step_ansi(quote(x)));
         },
         "  x\n"
         "->x\n");
    test("terminal color step leaves zero-arity basis uncolored",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(zero_arity_basis)));
         },
         "  Qzero\n"
         "->K\n");
    test("terminal color step preserves multicharacter basis spacing",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(I)(Cstar)));
         },
         std::string{"  I"} +
             terminal_red_argument(" Cstar") +
             "\n->" +
             terminal_red_argument("Cstar") +
             "\n");
    test("terminal color step does not HTML-escape expression text",
         [&] {
             static_cast<void>(
                 color_step_ansi(
                     quote(I)(std::string{"<&>\"'"})));
         },
         std::string{"  I"} +
             terminal_red_argument(" <&>\"'") +
             "\n->" +
             terminal_red_argument("<&>\"'") +
             "\n");
    test("terminal color step preserves stream formatting",
         [&] {
             std::ostringstream output;
             output << std::hex;
             static_cast<void>(
                 color_step_ansi(quote(I)(255), output));
             std::cout << output.str();
         },
         std::string{"  I"} +
             terminal_red_argument("<ff>") +
             "\n->" +
             terminal_red_argument("<ff>") +
             "\n");
    test("terminal color step leaves a failed output stream untouched",
         [&] {
             std::ostringstream output;
             output.setstate(std::ios_base::failbit);
             auto result =
                 color_step_ansi(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("terminal color step accepts an output stream without a buffer",
         [&] {
             std::ostream output(nullptr);
             auto result =
                 color_step_ansi(quote(I)(x), output);
             std::cout << "returned: ";
             result();
         },
         "returned: x");
    test("terminal color step supports a custom output stream",
         [&] {
             std::ostringstream output;
             auto expression =
                 color_step_ansi(quote(I)(x), output);
             std::cout << output.str() << "returned: ";
             expression();
         },
         std::string{"  I"} +
             terminal_red_argument("x") +
             "\n->" +
             terminal_red_argument("x") +
             "\nreturned: x");
    test("terminal color step colors only a basis expansion",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(M)(x)(y), true));
         },
         std::string{"  "} +
             terminal_red_argument("M") +
             "xy\n->" +
             terminal_red_argument("SII") +
             "xy\n");
    test("single step run resumes after SIGINT",
         [&] {
             std::istringstream input("\n");
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             single_step_run(quoted_ski_x, output, input);
             std::cout << buffer.str();
         },
         "Interrupted. Press Enter to resume; type q or Q then Enter to quit.\n"
         "Ix\n"
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
         "Ix\n"
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
    test("fifteen-character basis",
         fifteen_character_basis, "123456789012345");
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
    test("self-delimiting basis before a symbol stays compact",
         quote(Q1)(x), "Q1x");
    test("self-delimiting basis before a UTF-8 symbol stays compact",
         quote(Q1)(circle), "Q1\xE2\x97\x8F");
    test("self-delimiting basis before a primitive keeps a space",
         quote(Q1)(K), "Q1 K");
    test("self-delimiting basis before another basis keeps a space",
         quote(Q1)(Q3), "Q1 Q3");
    test("nested self-delimiting basis before a symbol stays compact",
         x(parse("Q1y")), "x(Q1y)");
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
    test("S(SKy)(SK)(zu)w", (S)(S(K)(y))(S(K))(z(u))(w),
         "zu(SK(zu))w");
    test("S(SK)yzw", (S)(S(K))(y)(z)(w), "yzw");
    test("S(SK)(Ky)zw", (S)(S(K))(K(y))(z)(w), "yw");
    test("S(SK)(zu)yw", (S)(S(K))(z(u))(y)(w), "zuyw");
    test("S(SK)yI", (S)(S(K))(y)(I), "yI");
    test("S(SK)y(SKz)", (S)(S(K))(y)(S(K)(z)), "y(SKz)");
    test("S(KI)yzw", (S)(K(I))(y)(z)(w), "yzw");
    test("S(KI)(Ky)zw", (S)(K(I))(K(y))(z)(w), "yw");
    test("S(KI)(zu)yw", (S)(K(I))(z(u))(y)(w), "zuyw");
    test("S(K(SKx))yzw", (S)(K(S(K)(x)))(y)(z)(w), "yzw");
    test("S(K(SKx))(Ky)zw", (S)(K(S(K)(x)))(K(y))(z)(w), "yw");
    test("S(K(SKx))(zu)yw", (S)(K(S(K)(x)))(z(u))(y)(w), "zuyw");
    test("SxIzw", (S)(x)(I)(z)(w), "xzzw");
    test("Sx(SKy)zw", (S)(x)(S(K)(y))(z)(w), "xzzw");
    test("Sxy(SKz)w", (S)(x)(y)(S(K)(z))(w),
         "x(SKz)(y(SKz))w");
    test("SI(SKx)zw", (S)(I)(S(K)(x))(z)(w), "zzw");
    test("SKyzw", (S)(K)(y)(z)(w), "zw");
    test("SKKzw", (S)(K)(K)(z)(w), "zw");
    test("Sx(KS)zw", (S)(x)(K(S))(z)(w), "xzSw");
    test("Sx(SK)zw", (S)(x)(S(K))(z)(w), "xz(SKz)w");
    test("SI(SK)zw", (S)(I)(S(K))(z)(w), "z(SKz)w");
    test("SI(SK)(zu)w", (S)(I)(S(K))(z(u))(w), "zu(SK(zu))w");
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
    test("x(SKy)w", (x)(S(K)(y))(w), "x(SKy)w");
    test("I(Syz)", (I)(S(y)(z)), "Syz");
    test("I(Syz)w", (I)(S(y)(z))(w), "yw(zw)");
    test("SxI(zu)w", (S)(x)(I)(z(u))(w), "x(zu)(zu)w");
    test("Sx(SKx)(zu)w", (S)(x)(S(K)(x))(z(u))(w), "x(zu)(zu)w");
    test("Sx(Sy)zw", (S)(x)(S(y))(z)(w), "xz(Syz)w");
    test("Sx(Syz)uw", (S)(x)(S(y)(z))(u)(w), "xu(yu(zu))w");
    test("S(Sx)y(zu)", (S)(S(x))(y)(z(u)), "x(y(zu))(zu(y(zu)))");
    test("S(Sx)I(zu)", (S)(S(x))(I)(z(u)), "x(zu)(zu(zu))");
    test("S(Sx)yI", (S)(S(x))(y)(I), "x(yI)(yI)");
    test("S(Sx)(SK)(Kz)u", (S)(S(x))(S(K))(K(z))(u),
         "x(SK(Kz))zu");
    test("SSy(SK)uw", (S)(S)(y)(S(K))(u)(w), "y(SK)uw");
    test("S(S(Sx)(SK))yz", S(S(S(x))(S(K)))(y)(z),
         "x(SKz)(z(SKz))(yz)");
    test("S(Sx)y(SK)u", S(S(x))(y)(S(K))(u),
         "x(y(SK))(SK(y(SK)))u");
    test("S(Sx)y(SKz)u", S(S(x))(y)(S(K)(z))(u),
         "x(y(SKz))(y(SKz))u");
    test("S(Sx)y(Kv)", (S)(S(x))(y)(K(v)), "x(y(Kv))v");
    test("S(Sx)(SK)z", (S)(S(x))(S(K))(z),
         "x(SKz)(z(SKz))");
    test("S(S(Sx))(SK)z", (S)(S(S(x)))(S(K))(z),
         "x(z(SKz))(z(SKz))");
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
    test("U", (U), "U");
    test("Uxyw [applies y to xxy]", (U)(x)(y)(w), "y(xxy)w");
    test("N", (N), "N");
    test("Nxyw [duplicates x after y]", (N)(x)(y)(w), "xyxw");
    test("R", (R), "R");
    test("Rxyzw [moves x after yz (opposite of V)]", (R)(x)(y)(z)(w), "yzxw");
    test("C", (C), "C");
    test("Cxyzw [swaps y and z after x]", (C)(x)(y)(z)(w), "xzyw");
    test("C*", (C_star), "C*");
    test("C*wxyzv [swaps y and z after wx]",
         (C_star)(w)(x)(y)(z)(v), "wxzyv");
    test("C**", (C_star_star), "C**");
    test("C**vwxyzu [swaps y and z after vwx]",
         (C_star_star)(v)(w)(x)(y)(z)(u), "vwxzyu");
    test("W*", (W_star), "W*");
    test("W*wxyv [duplicates y after wxy]",
         (W_star)(w)(x)(y)(v), "wxyyv");
    test("W**", (W_star_star), "W**");
    test("W**vwxyu [duplicates y after vwxy]",
         (W_star_star)(v)(w)(x)(y)(u), "vwxyyu");
    test("Q", (Q), "Q");
    test("Qxyzw [combines (xz) after y]", (Q)(x)(y)(z)(w), "y(xz)w");
    test("Q1", (Q1), "Q1");
    test("Q1xyzw [combines (zy) after x]",
         (Q1)(x)(y)(z)(w), "x(zy)w");
    test("Q3", (Q3), "Q3");
    test("Q3xyzw [combines (xy) after z]",
         (Q3)(x)(y)(z)(w), "z(xy)w");
    test("V", (V), "V");
    test("Vxyzw [moves z in front of xy (opposite of R)]", (V)(x)(y)(z)(w), "zxyw");
    test("D", (D), "D");
    test("Dwxyzv [like B, but combines (yz) after wx]", (D)(w)(x)(y)(z)(v), "wx(yz)v");
    test("L", (L), "L");
    test("Lxyw [combines (yy) after x]", (L)(x)(y)(w), "x(yy)w");
    test("W1", (W1), "W1");
    test("W1xyw [duplicates x after y]",
         (W1)(x)(y)(w), "yxxw");
    test("Z", (Z), "Z");
    test("Zxyw [combines (xy) after x]", (Z)(x)(y)(w), "x(xy)w");
    test("A", (A), "A");
    test("Axyw [combines (yx) after x]", (A)(x)(y)(w), "x(yx)w");
    test("E", (E), "E");
    test("Exyzwvu [combines (zwv) after xy]",
         (E)(x)(y)(z)(w)(v)(u), "xy(zwv)u");
    test("F", (F), "F");
    test("Fxyzw [reverses x, y, and z]",
         (F)(x)(y)(z)(w), "zyxw");
    test("G", (G), "G");
    test("Gxyzwv [combines (yz) after xw]",
         (G)(x)(y)(z)(w)(v), "xw(yz)v");
    test("H", (H), "H");
    test("Hxyzw [repeats y after xyz]",
         (H)(x)(y)(z)(w), "xyzyw");
    test("J", (J), "J");
    test("Jxyzwv [combines (xwz) after xy]",
         (J)(x)(y)(z)(w)(v), "xy(xwz)v");
    test("Cstar", (Cstar), "Cstar");
    test("Cstar wxyzv [swaps y and z after wx]", (Cstar)(w)(x)(y)(z)(v), "wxzyv");
    test("Vstar", (Vstar), "Vstar");
    test("Vstar wxyzv [like V, but moves z after w and before xy]", (Vstar)(w)(x)(y)(z)(v), "wzxyv");
    test("V4", (V4), "V4");
    test("V4 wxyzv [like V, but moves z before wxy]", (V4)(w)(x)(y)(z)(v), "zwxyv");
    test("G1", (G1), "G1");
    test("G1xw", (G1)(x)(w), "xSTK(KK)(SK)w");
    test("G1K", (G1)(K), "SK");
    test("G1B", (G1)(B), "K");
    test("G1D", (G1)(D), "K");
    test("G2", (G2), "G2");
    test("G2 xw", (G2)(x)(w), "xSTK(KK)(SK)w");
    test("bazTest zxy", (bazTest)(z)(x)(y), "uy(z(yx))x");
    test("Hprime zxy", (Hprime)(z)(x)(y), "ySTK(KK)(SK)(z(yx))x");
    test("H1xK", (H1)(x)(K), "x");
    test("H1xBKyz", (H1)(x)(B)(K)(y)(z), "x(yz)");
    test("H1wBBKxyz", (H1)(w)(B)(B)(K)(x)(y)(z), "w(xyz)");
    test("H1vBBBKwxyz", (H1)(v)(B)(B)(B)(K)(w)(x)(y)(z), "v(wxyz)");
    test("H1wDKxyz", (H1)(w)(D)(K)(x)(y)(z), "wx(yz)");
    test("H1vDDKwxyz", (H1)(v)(D)(D)(K)(w)(x)(y)(z), "vw(xyz)");
    test("H1uDDDKvwxyz", (H1)(u)(D)(D)(D)(K)(v)(w)(x)(y)(z), "uv(wxyz)");

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
            "1234567890123456", 1, std::move(preserved_expression)));
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

    auto move_only_basis = basis("Q", 1, K(std::make_unique<int>(31)));
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
