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

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#if !defined(__EMSCRIPTEN__)
#include <latch>
#include <semaphore>
#endif
#include <limits>
#include <memory>
#include <optional>
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
    basis("BazTest", 3, B(W)(B(B(C))(
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
    combdsl::check_for_match_excluded_pair_count == 69);
static_assert(
    combdsl::check_for_match_kestrel_argument_head_count == 2);
static_assert(
    combdsl::check_for_match_arbitrary_tail_left_pattern_count == 6);
static_assert(
    combdsl::check_for_match_fixed_tail_left_pattern_count == 6);
static_assert(
    combdsl::check_for_match_structural_left_trip_exclusion_count == 358);
static_assert(
    combdsl::check_for_pairs_match_candidate_count == 831);
static_assert(
    combdsl::check_for_trips_match_candidate_count == 47'622);
static_assert(
    combdsl::check_for_trips_match_shape_count == 2);
static_assert(
    combdsl::check_for_trips_match_column_count == 1'800);
static_assert(
    combdsl::check_for_match_left_trip_candidate_count == 23'642);
static_assert(
    combdsl::check_for_match_right_trip_candidate_count == 23'980);
static_assert(
    combdsl::check_for_quads_match_tuple_count == 810'000);
static_assert(
    combdsl::check_for_quads_match_shape_count == 5);
static_assert(
    combdsl::check_for_quads_match_column_count == 135'000);
static_assert(
    combdsl::check_for_quads_match_candidate_count == 3'487'022);
static_assert(
    combdsl::check_for_quads_match_candidate_count ==
    combdsl::check_for_match_combinator_count *
        combdsl::check_for_match_left_trip_candidate_count +
    combdsl::check_for_pairs_match_candidate_count *
        combdsl::check_for_pairs_match_candidate_count -
    combdsl::check_for_match_arbitrary_tail_left_pattern_count *
        combdsl::check_for_pairs_match_candidate_count +
    combdsl::check_for_match_combinator_count *
        combdsl::check_for_match_right_trip_candidate_count -
    combdsl::check_for_match_fixed_tail_left_pattern_count *
        combdsl::check_for_pairs_match_candidate_count +
    (combdsl::check_for_match_combinator_count - 1) *
        combdsl::check_for_match_left_trip_candidate_count +
    (combdsl::check_for_match_combinator_count - 1) *
        combdsl::check_for_match_right_trip_candidate_count -
    combdsl::check_for_match_right_composite_root_pattern_count *
        combdsl::check_for_pairs_match_candidate_count +
    combdsl::check_for_match_structural_right_trip_exclusion_count);
static_assert(
    combdsl::detail::find_search_window ==
    std::chrono::seconds{10});
static_assert(std::is_same_v<
              decltype(combdsl::find_combinator_matches_among(
                  std::declval<
                      std::span<combdsl::quoted_atomic const>>(),
                  std::declval<combdsl::quoted_expression const&>(),
                  std::declval<
                      std::span<combdsl::quoted_expression const>>(),
                  true)),
              combdsl::catalog_combinator_find_result>);
#if !defined(__EMSCRIPTEN__)
static_assert(
    combdsl::detail::native_find_worker_count(0, 12) == 0);
static_assert(
    combdsl::detail::native_find_worker_count(1, 12) == 1);
static_assert(
    combdsl::detail::native_find_worker_count(3, 12) == 3);
static_assert(
    combdsl::detail::native_find_worker_count(100, 12) == 11);
static_assert(
    combdsl::detail::native_find_worker_count(100, 1) == 1);
static_assert(
    combdsl::detail::native_find_worker_count(100, 0) == 1);
static_assert(
    combdsl::detail::native_find_worker_count(100, 100) ==
    combdsl::detail::native_find_worker_limit);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(1, 1) ==
    1);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(2, 1) ==
    1);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(3, 1) ==
    2);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(3, 2) ==
    combdsl::detail::native_catalog_find_worker_limit);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(4, 2) ==
    combdsl::detail::native_catalog_find_worker_limit);
static_assert(
    combdsl::detail::native_catalog_find_maximum_work_count(
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max()) ==
    combdsl::detail::native_catalog_find_worker_limit);
#endif
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
using eval_with_progress_and_limit_signature = void (*)(
    combdsl::quoted_expression,
    std::ostream&,
    std::istream&,
    bool,
    evaluation_progress_callback const&,
    std::optional<std::size_t>);
static_assert(std::is_same_v<
              decltype(static_cast<eval_without_progress_signature>(&eval)),
              eval_without_progress_signature>);
static_assert(std::is_same_v<
              decltype(static_cast<eval_with_progress_signature>(&eval)),
              eval_with_progress_signature>);
static_assert(std::is_same_v<
              decltype(static_cast<eval_with_progress_and_limit_signature>(
                  &eval)),
              eval_with_progress_and_limit_signature>);
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
using parse_eval_with_progress_and_limit_signature = void (*)(
    std::string_view,
    std::ostream&,
    std::istream&,
    bool,
    evaluation_progress_callback const&,
    std::optional<std::size_t>);
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
static_assert(std::is_same_v<
              decltype(static_cast<
                       parse_eval_with_progress_and_limit_signature>(
                  &parse_eval)),
              parse_eval_with_progress_and_limit_signature>);
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

struct compare_clock_override_reset {
    ~compare_clock_override_reset() {
        combdsl::detail::compare_clock_now_override = {};
    }
};

struct find_clock_override_reset {
    ~find_clock_override_reset() {
        combdsl::detail::find_clock_now_override = {};
    }
};

struct catalog_find_runtime_overrides_reset {
    ~catalog_find_runtime_overrides_reset() {
        combdsl::detail::catalog_find_runtime_overrides_override = {};
    }
};

std::atomic<std::size_t> catalog_find_thread_token_source{1};

[[nodiscard]] std::size_t catalog_find_thread_token() {
    thread_local auto const token =
        catalog_find_thread_token_source.fetch_add(1);
    return token;
}

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
    auto seven_character_basis = basis("123456x", 1, I);
    auto fifteen_character_basis =
        basis("A12345678901234", 1, I);
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
    test("parse nonnegative integer value", parse("42"), "42");
    test("parse zero integer value", parse("0"), "0");
    test("integer output removes leading zeroes", parse("00042"), "42");
    test("parse maximum nonnegative integer value",
         parse("9223372036854775807"), "9223372036854775807");
    test_parse_failure("parse rejects an explicit plus sign", "+4", 0);
    test_parse_failure("parse rejects a lone plus sign", "+", 0);
    test_parse_failure(
        "parse rejects a plus-signed integer after a symbol", "x+1", 1);
    test_parse_failure("parse rejects a negative integer", "-17", 0);
    test_parse_failure(
        "parse rejects the minimum signed integer spelling",
        "-9223372036854775808", 0);
    test_parse_failure("parse rejects a decimal fraction", "1.5", 0);
    test_parse_failure(
        "parse rejects a fraction without leading digits", ".5", 0);
    test_parse_failure(
        "parse rejects a decimal point without trailing digits", "1.", 0);
    test_parse_failure("parse rejects a lowercase exponent", "1e3", 0);
    test_parse_failure("parse rejects an uppercase exponent", "2E2", 0);
    test_parse_failure("parse rejects a signed exponent", "1e+3", 0);
    test_parse_failure(
        "parse rejects a signed floating value", "-2.5e-2", 0);
    test_parse_failure(
        "parse rejects a high-precision decimal",
        "1.2345678901234567", 0);
    test("numeric value may immediately follow a symbol",
         parse("x2"), "x 2");
    test("symbol may immediately follow a numeric value",
         parse("2x"), "2 x");
    test("numeric value is separated after a primitive",
         parse("K2"), "K 2");
    test("primitive is separated after a numeric value",
         parse("2K"), "2 K");
    test("a bare lowercase exponent marker remains a symbol",
         parse("1e"), "1 e");
    test("a bare uppercase exponent marker remains a basis",
         parse("1E"), "1 E");
    test("unsupported hexadecimal notation remains an application",
         parse("0x10"), "0 x 10");
    test("adjacent numeric values print with a separator",
         parse("1")(parse("2")), "1 2");
    test("integer values round trip with their exact values",
         [] {
             auto const expression = parse(
                 "42 0 0007 9223372036854775807");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 42 0 7 9223372036854775807");
    test("numeric output cannot fuse with exponent-marker operands",
         [] {
             for (auto const source : {"1 e 2", "1 E 2"}) {
                 auto const expression = parse(source);
                 std::ostringstream rendered;
                 expression.print_to(rendered);
                 auto const reparsed = parse(rendered.str());
                 std::cout
                     << (combdsl::detail::
                             same_parser_definition_expression(
                                 expression, reparsed)
                             ? "roundtrip "
                             : "changed ");
             }
         },
         "roundtrip roundtrip ");
    test("parentheses group an integer value",
         parse("x(25)"), "x 25");
    test("a parenthesized operand stays compact after a numeric value",
         parse("2(xy)"), "2(xy)");
    test("numeric values stay separated inside a parenthesized operand",
         parse("x(2 3)"), "x(2 3)");
    test("identity evaluates to its integer value",
         single_step(parse("I 42")), "42");
    test("ordinary C++ integer values retain opaque notation",
         quote(42), "<42>");
    test("ordinary nonnumeric C++ values retain opaque notation",
         quote(named_value{}), "<named>");
    test("a spaced lowercase run is always symbols",
         parse("x foo y"), "xfooy");
    test("a parenthesized lowercase run is always symbols",
         parse("x(foo y)"), "x(fooy)");
    test("a lowercase run after a spaced symbol is always symbols",
         parse("x(y foo)"), "x(yfoo)");
    test("parentheses preserve an unspaced compact symbol run",
         parse("x(foo)y"), "x(foo)y");
    test("outer whitespace remains padding",
         parse(" \tfoo \n"), "foo");
    test("parenthetical whitespace remains padding",
         parse("x( foo )y"), "x(foo)y");
    test("a root mixed-case fallback may precede a spaced operand",
         parse("Cxyz w"), "Cxyzw");
    test("a spaced lowercase run after compact birds remains symbols",
         parse("SBT xy"), "SBTxy");
    test("a spaced lowercase symbol followed by a bird remains split",
         parse("SBT xC"), "SBTxC");
    test("spaced lowercase symbols preserve compact-bird behavior",
         [] { parse_eval("SBT xy"); }, "x(yx)\n");
    test("a spaced lowercase symbol before a bird preserves behavior",
         [] { parse_eval("SBT xC"); }, "x(Cx)\n");
    test_parse_failure(
        "an unregistered uppercase-leading mixed-case token is a name",
        "SBT Unknownmixed", 4, "unknown operand");
    test_parse_failure(
        "a preceding exact multicharacter atom makes mixed case a name",
        "Alias Unknownmixed", 6, "unknown operand");
    test("a root mixed-case fallback may precede an exact name",
         parse("Unknownmixed Alias"), "Unknownmixed Alias");
    test("an isolated uppercase-leading mixed-case token keeps fallback",
         parse("Unknownmixed"), "Unknownmixed");
    test("padding alone does not turn mixed case into a name",
         parse(" \tUnknownmixed\n"), "Unknownmixed");
    test("a lowercase-leading token keeps an internal mixed-case fallback",
         parse("xUnknownmixed y"), "xUnknownmixedy");
    test("a root compact fallback round trips before an exact name",
         [] {
             auto const expression = parse("SBTxy Alias");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 SBTxy Alias");
    test("a shorter root fallback round trips before an exact name",
         [] {
             auto const expression = parse("CKx Alias");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CKx Alias");
    auto const lowercase_cpp_registry_before =
        combdsl::detail::registered_parser_lookup_snapshot();
    test("the C++ basis API rejects a lowercase-leading name",
         [] {
             try {
                 static_cast<void>(basis("foo", 1, I));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::basis names cannot begin with a lowercase ASCII letter");
    test("a rejected lowercase C++ basis does not enter the registry",
         [&] {
             auto const after =
                 combdsl::detail::registered_parser_lookup_snapshot();
             std::cout
                 << (after.bases.size() ==
                     lowercase_cpp_registry_before.bases.size())
                 << (after.versions.size() ==
                     lowercase_cpp_registry_before.versions.size())
                 << !after.bases.contains("foo");
         },
         "111");
    test("show exposes a named basis definition",
         parse("show M"), "arity:1 SII");
    test_parse_failure(
        "show keeps lowercase text as a non-mutating name lookup",
        "show foo", 5, "foo is not a defined name");
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
        "show rejects a word",
        "show \"word\"", 5,
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
    test("usedby accepts its compact alias",
         parse("usedby U"), "U directly uses: B M O");
    test("usedby accepts its hyphenated alias",
         parse("used-by U"), "U directly uses: B M O");
    test("usedby accepts its two-word alias and extra whitespace",
         parse(" \tused\n  by\t U\f"), "U directly uses: B M O");
    test("dependson accepts its compact alias",
         parse("dependson O"), "O is directly depended on by: U");
    test("dependson accepts its hyphenated alias",
         parse("depends-on O"), "O is directly depended on by: U");
    test("dependson accepts its two-word alias and extra whitespace",
         parse(" \tdepends\n  on\t O\f"),
         "O is directly depended on by: U");
    test("usedby excludes fundamental combinators",
         parse("usedby M"), "M directly uses nothing");
    test("usedby deduplicates and sorts direct named bases",
         parse("usedby Q1"), "Q1 directly uses: B C");
    test("dependson reports no direct users",
         parse("dependson J"),
         "J is not directly depended on by anything");
    test("usedby all retains the direct no-dependency message",
         parse("usedby all M"), "M directly uses nothing");
    test("dependson all retains the direct no-user message",
         parse("dependson all J"),
         "J is not directly depended on by anything");
    test("dependency commands are display only",
         [] {
             auto const depends =
                 combdsl::detail::parse_input("depends on O");
             auto const used =
                 combdsl::detail::parse_input("used by U");
             auto const depends_all =
                 combdsl::detail::parse_input("depends on all O");
             auto const used_all =
                 combdsl::detail::parse_input("used by all U");
             auto const path = combdsl::detail::parse_input(
                 "used by path U O");
             std::cout << depends.is_display_only
                       << depends.is_definition
                       << used.is_display_only
                       << used.is_definition
                       << depends_all.is_display_only
                       << depends_all.is_definition
                       << used_all.is_display_only
                       << used_all.is_definition
                       << path.is_display_only
                       << path.is_definition;
         },
         "1010101010");
    test_parse_failure(
        "plain depends requires on", "depends O", 8,
        "expected 'on'");
    test_parse_failure(
        "plain used requires by", "used U", 5,
        "expected 'by'");
    test_parse_failure(
        "dependson requires a name", "dependson", 9,
        "missing combinator name");
    test_parse_failure(
        "depends-on requires a name", "depends-on ", 11,
        "missing combinator name");
    test_parse_failure(
        "depends on requires a name", "depends on", 10,
        "missing combinator name");
    test_parse_failure(
        "usedby requires a name", "usedby", 6,
        "missing combinator name");
    test_parse_failure(
        "used-by requires a name", "used-by ", 8,
        "missing combinator name");
    test_parse_failure(
        "used by requires a name", "used by", 7,
        "missing combinator name");
    test_parse_failure(
        "dependson all requires a name", "dependson all", 13,
        "missing combinator name");
    test_parse_failure(
        "used by all requires a name", "used by all", 11,
        "missing combinator name");
    constexpr std::string_view path_missing_first = "usedby path";
    test_parse_failure(
        "usedby path requires a first name",
        path_missing_first,
        path_missing_first.size(),
        "missing first combinator name");
    constexpr std::string_view path_between_missing_first =
        "used-by path between";
    test_parse_failure(
        "used-by path between requires a first name",
        path_between_missing_first,
        path_between_missing_first.size(),
        "missing first combinator name");
    constexpr std::string_view path_missing_second =
        "used by path U";
    test_parse_failure(
        "used by path requires a second name",
        path_missing_second,
        path_missing_second.size(),
        "missing second combinator name");
    constexpr std::string_view path_and_missing_second =
        "usedby path U and";
    test_parse_failure(
        "usedby path and requires a second name",
        path_and_missing_second,
        path_and_missing_second.size(),
        "missing second combinator name");
    test_parse_failure(
        "dependson rejects an undefined name",
        "dependson MissingDep", 10,
        "MissingDep is not a defined name");
    test_parse_failure(
        "usedby rejects an undefined name",
        "usedby MissingDep", 7,
        "MissingDep is not a defined name");
    test_parse_failure(
        "usedby all rejects an undefined name",
        "usedby all MissingDep", 11,
        "MissingDep is not a defined name");
    constexpr std::string_view path_undefined_first =
        "usedby path MissingDep U";
    test_parse_failure(
        "usedby path rejects an undefined first name",
        path_undefined_first,
        path_undefined_first.find("MissingDep"),
        "MissingDep is not a defined name");
    constexpr std::string_view path_undefined_second =
        "used-by path U MissingDep";
    test_parse_failure(
        "used-by path rejects an undefined second name",
        path_undefined_second,
        path_undefined_second.find("MissingDep"),
        "MissingDep is not a defined name");
    test_parse_failure(
        "dependson rejects a fundamental name", "dependson S", 10,
        "S is a fundamental name and cannot be queried");
    test_parse_failure(
        "dependson all rejects a fundamental name",
        "dependson all S", 14,
        "S is a fundamental name and cannot be queried");
    test_parse_failure(
        "usedby rejects a fundamental name", "usedby Y", 7,
        "Y is a fundamental name and cannot be queried");
    constexpr std::string_view path_fundamental =
        "used by path S U";
    test_parse_failure(
        "used by path rejects a fundamental endpoint",
        path_fundamental,
        path_fundamental.find('S'),
        "S is a fundamental name and cannot be queried");
    constexpr std::string_view path_second_fundamental =
        "usedby path U S";
    test_parse_failure(
        "usedby path rejects a fundamental second endpoint",
        path_second_fundamental,
        path_second_fundamental.rfind('S'),
        "S is a fundamental name and cannot be queried");
    test_parse_failure(
        "dependson rejects trailing input", "dependson O extra", 12,
        "unexpected input after name");
    test_parse_failure(
        "usedby rejects trailing input", "usedby U extra", 9,
        "unexpected input after name");
    test_parse_failure(
        "depends-on all rejects trailing input",
        "depends-on all O extra", 17,
        "unexpected input after name");
    constexpr std::string_view path_same_endpoints =
        "usedby path between U and U";
    test_parse_failure(
        "usedby path rejects equal endpoints",
        path_same_endpoints,
        path_same_endpoints.rfind('U'),
        "dependency path endpoints must be different");
    constexpr std::string_view path_trailing =
        "used by path between U and O extra";
    test_parse_failure(
        "used by path rejects trailing input",
        path_trailing,
        path_trailing.find("extra"),
        "unexpected input after second name");
    test("dependency command prefixes remain ordinary input",
         parse("dependsonx"), "dependsonx");
    test("reverse dependency command prefixes remain ordinary input",
         parse("usedbyx"), "usedbyx");
    test("set list registers a default zero arity",
         parse("set LZero = I"), "LZero");
    test("set list shows a default zero arity",
         [] { std::cout << set_list(); },
         "references captured\nset LZero = 0 I");
    test("set list registers a dependent basis",
         parse("set LUse = 1 LZero"), "LUse");
    test("set list registers a binary basis",
         parse("set LPair = 2 K"), "LPair");
    test("set list registers a backslash name",
         parse("set Q\\R = 1 I"), "Q\\R");
    test("set list registers a backslash body reference",
         parse("set LSlash = 1 Q\\R"), "LSlash");
    test("set list registers a multicharacter basis body",
         parse("set LMulti = 1 Cstar x"), "LMulti");
    test("set list registers a raw word body",
         parse("set LRaw = 1 K \"a b()\\\\c\""),
         "LRaw");

    const std::string expected_set_list =
        "references captured\n"
        "set LZero = 0 I\n"
        "set LUse = 1 LZero\n"
        "set LPair = 2 K\n"
        "set Q\\R = 1 I\n"
        "set LSlash = 1 Q\\R\n"
        "set LMulti = 1 Cstar x\n"
        "set LRaw = 1 K \"a b()\\\\c\"";
    test("set list preserves definitions in replay order",
         [] { std::cout << set_list(); }, expected_set_list);
    test("show all displays the entire set list",
         parse("show all"), expected_set_list);
    test("set list backslash basis replays",
         single_step(parse("Q\\R x")), "x");
    test("set list raw word basis replays",
         single_step(parse("LRaw x")), "a b()\\c");
    test("usedby reports a direct user dependency",
         parse("usedby LUse"), "LUse directly uses: LZero");
    test("dependson reports a direct user",
         parse("dependson LZero"),
         "LZero is directly depended on by: LUse");
    test("usedby stops at the directly contained named basis",
         parse("usedby LMulti"), "LMulti directly uses: Cstar");
    test("dependson includes predefined and user bases",
         parse("dependson Cstar"),
         "Cstar is directly depended on by: LMulti Vstar");

    test("set list accepts a later duplicate definition",
         parse("set LPair = 1 I"), "LPair");
    const std::string expected_redefined_set_list =
        "references captured\n"
        "set LZero = 0 I\n"
        "set LUse = 1 LZero\n"
        "set LPair = 2 K\n"
        "set Q\\R = 1 I\n"
        "set LSlash = 1 Q\\R\n"
        "set LMulti = 1 Cstar x\n"
        "set LRaw = 1 K \"a b()\\\\c\"\n"
        "set LPair = 1 I";
    test("set list preserves an earlier immutable revision",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test_parse_failure(
        "set rejects a primitive definition",
        "set K = I",
        4,
        "K is a pre-defined basis and cannot be redefined");
    test("set list excludes rejected primitive definitions",
         [] { std::cout << set_list(); },
         expected_redefined_set_list);
    test_parse_failure(
        "set rejects a predefined basis definition",
        "set M = I",
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
                    definition,
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
                 parse("define DefSave xyz = x(y z)"));
             std::cout << set_list();
         },
         expected_definition_list);

    test("transitive dependency graph registers first leaf",
         parse("set AllIndirectC = 1 I"), "AllIndirectC");
    test("transitive dependency graph registers second leaf",
         parse("set AllIndirectD = 1 I"), "AllIndirectD");
    test("transitive dependency graph registers third leaf",
         parse("set AllIndirectE = 1 I"), "AllIndirectE");
    test("transitive dependency graph registers first branch",
         parse("set AllDirectA = 1 AllIndirectE AllIndirectD "
               "AllIndirectC"),
         "AllDirectA");
    test("transitive dependency graph registers second branch",
         parse("set AllDirectB = 1 AllIndirectD AllIndirectC "
               "AllIndirectE"),
         "AllDirectB");
    test("transitive dependency graph registers a deduplicated root",
         parse("set AllRoot = 1 AllDirectB AllIndirectC "
               "AllDirectA AllDirectA"),
         "AllRoot");

    constexpr std::string_view all_uses =
        "AllRoot directly uses: AllDirectA AllDirectB AllIndirectC\n"
        "AllRoot indirectly uses: AllIndirectD AllIndirectE";
    test("usedby all accepts its compact alias",
         parse("usedby all AllRoot"), all_uses);
    test("usedby all accepts its hyphenated alias",
         parse("used-by all AllRoot"), all_uses);
    test("usedby all accepts its two-word alias",
         parse("used by all AllRoot"), all_uses);

    constexpr std::string_view all_depended_on_by =
        "AllIndirectD is directly depended on by: AllDirectA AllDirectB\n"
        "AllIndirectD is indirectly depended on by: AllRoot";
    test("dependson all accepts its compact alias",
         parse("dependson all AllIndirectD"), all_depended_on_by);
    test("dependson all accepts its hyphenated alias",
         parse("depends-on all AllIndirectD"), all_depended_on_by);
    test("dependson all accepts its two-word alias",
         parse("depends on all AllIndirectD"), all_depended_on_by);

    constexpr std::string_view shortest_dependency_path =
        "AllRoot uses AllIndirectD via:\n"
        "  AllRoot -> AllDirectA  [captured]\n"
        "  AllDirectA -> AllIndirectD  [captured]";
    test("usedby path accepts compact syntax without optional words",
         parse("usedby path AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("usedby path accepts both optional words",
         parse("usedby path between AllRoot and AllIndirectD"),
         shortest_dependency_path);
    test("usedby path accepts between without and",
         parse("usedby path between AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("usedby path accepts and without between",
         parse("usedby path AllRoot and AllIndirectD"),
         shortest_dependency_path);
    test("used-by path accepts syntax without optional words",
         parse("used-by path AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("used-by path accepts between without and",
         parse("used-by path between AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("used-by path accepts and without between",
         parse("used-by path AllRoot and AllIndirectD"),
         shortest_dependency_path);
    test("used-by path accepts both optional words",
         parse("used-by path between AllRoot and AllIndirectD"),
         shortest_dependency_path);
    test("used by path accepts syntax without optional words",
         parse("used by path AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("used by path accepts between without and",
         parse("used by path between AllRoot AllIndirectD"),
         shortest_dependency_path);
    test("used by path accepts and without between and whitespace",
         parse(" \tused\n by path\tAllRoot\n and\fAllIndirectD "),
         shortest_dependency_path);
    test("used by path accepts both optional words",
         parse("used by path between AllRoot and AllIndirectD"),
         shortest_dependency_path);
    test("usedby path is independent of endpoint order",
         parse("usedby path between AllIndirectD and AllRoot"),
         shortest_dependency_path);
    test("usedby path chooses a shorter direct edge",
         parse("usedby path AllRoot AllIndirectC"),
         "AllRoot uses AllIndirectC via:\n"
         "  AllRoot -> AllIndirectC  [captured]");
    test("usedby path reports a normalized disconnected pair",
         parse("usedby path AllIndirectE AllIndirectD"),
         "AllIndirectD and AllIndirectE have no dependency path");
    test("usedby path normalizes a reversed disconnected pair",
         parse("usedby path AllIndirectD AllIndirectE"),
         "AllIndirectD and AllIndirectE have no dependency path");

    test("dependency path registers a punctuation-ending leaf",
         parse("set PathLeaf: = 1 I"), "PathLeaf:");
    test("dependency path registers a punctuation-ending intermediate",
         parse("set PathMiddle: = 1 PathLeaf:"), "PathMiddle:");
    test("dependency path registers a punctuation-ending root",
         parse("set PathRoot: = 1 PathMiddle:"), "PathRoot:");
    test("dependency path keeps separators after punctuation-ending names",
         parse("usedby path PathRoot: PathLeaf:"),
         "PathRoot: uses PathLeaf: via:\n"
         "  PathRoot: -> PathMiddle:  [captured]\n"
         "  PathMiddle: -> PathLeaf:  [captured]");

    test("captured traversal registers its first leaf",
         parse("set AllSnapC = 1 I"), "AllSnapC");
    test("captured traversal registers its replacement leaf",
         parse("set AllSnapD = 1 I"), "AllSnapD");
    test("captured traversal registers its original branch",
         parse("set AllSnapB = 1 AllSnapC"), "AllSnapB");
    test("captured traversal snapshots the original branch",
         parse("set AllSnapA = 1 AllSnapB"), "AllSnapA");
    test("captured traversal replaces the branch",
         parse("set AllSnapB = 1 AllSnapD"), "AllSnapB");
    constexpr std::string_view captured_uses =
        "AllSnapA directly uses: AllSnapB\n"
        "AllSnapA indirectly uses: AllSnapC";
    constexpr std::string_view captured_depended_on_by =
        "AllSnapC is not directly depended on by anything\n"
        "AllSnapC is indirectly depended on by: AllSnapA";
    test("usedby all follows captured revisions after redefinition",
         parse("usedby all AllSnapA"), captured_uses);
    test("dependson all follows captured revisions after redefinition",
         parse("dependson all AllSnapC"), captured_depended_on_by);
    constexpr std::string_view captured_path =
        "AllSnapA uses AllSnapC via:\n"
        "  AllSnapA -> AllSnapB@1  [captured]\n"
        "  AllSnapB@1 -> AllSnapC  [captured]";
    test("usedby path follows the captured branch revision",
         parse("usedby path AllSnapA AllSnapC"), captured_path);
    test("usedby path excludes the replacement from a captured path",
         parse("usedby path AllSnapA AllSnapD"),
         "AllSnapA and AllSnapD have no dependency path");
    test("captured traversal removes the replacement branch",
         parse("remove AllSnapB"), "AllSnapB");
    test("usedby all retains a captured removed revision",
         parse("usedby all AllSnapA"), captured_uses);
    test("dependson all retains a captured removed revision",
         parse("dependson all AllSnapC"), captured_depended_on_by);
    test("usedby path retains a removed captured intermediate",
         parse("usedby path AllSnapA AllSnapC"),
         "AllSnapA uses AllSnapC via:\n"
         "  AllSnapA -> AllSnapB@1  [captured] [name removed]\n"
         "  AllSnapB@1 -> AllSnapC  [captured]");

    test("live traversal enables live references",
         parse("references live"), "references live");
    test("live traversal registers its first leaf",
         parse("set AllLiveC = 1 I"), "AllLiveC");
    test("live traversal registers its replacement leaf",
         parse("set AllLiveD = 1 I"), "AllLiveD");
    test("live traversal registers its original branch",
         parse("set AllLiveB = 1 AllLiveC"), "AllLiveB");
    test("live traversal refers to the branch by name",
         parse("set AllLiveA = 1 AllLiveB"), "AllLiveA");
    test("live traversal replaces the branch",
         parse("set AllLiveB = 1 AllLiveD"), "AllLiveB");
    test("usedby all follows a live branch replacement",
         parse("usedby all AllLiveA"),
         "AllLiveA directly uses: AllLiveB\n"
         "AllLiveA indirectly uses: AllLiveD");
    test("dependson all follows a live branch replacement",
         parse("dependson all AllLiveD"),
         "AllLiveD is directly depended on by: AllLiveB\n"
         "AllLiveD is indirectly depended on by: AllLiveA");
    test("usedby path follows a live branch replacement",
         parse("usedby path AllLiveA AllLiveD"),
         "AllLiveA uses AllLiveD via:\n"
         "  AllLiveA -> AllLiveB@2  [live]\n"
         "  AllLiveB@2 -> AllLiveD  [live]");
    test("usedby path excludes the old live target",
         parse("usedby path AllLiveA AllLiveC"),
         "AllLiveA and AllLiveC have no dependency path");
    test("live traversal removes the current branch name",
         parse("remove AllLiveB"), "AllLiveB");
    test("usedby path retains the last removed live target",
         parse("usedby path AllLiveA AllLiveD"),
         "AllLiveA uses AllLiveD via:\n"
         "  AllLiveA -> AllLiveB@2  [live] [name removed]\n"
         "  AllLiveB@2 -> AllLiveD  [live]");
    test("live traversal restores captured references",
         parse("references captured"), "references captured");

    test("dependency path registers a pre-defined target user",
         parse("set PathPreRoot = 1 M"),
         "PathPreRoot");
    test("dependency path leaves pre-defined nodes unversioned",
         parse("usedby path PathPreRoot M"),
         "PathPreRoot uses M via:\n"
         "  PathPreRoot -> M  [pre-defined]");

    test("explicit path target registers its first revision",
         parse("set PathExpTarget = 1 I"),
         "PathExpTarget");
    test("explicit path target advances while retaining history",
         parse("set PathExpTarget = 2 K"),
         "PathExpTarget");
    test("dependency path registers an explicit old-revision edge",
         parse("set live PathExpRoot = 1 PathExpTarget@1"),
         "PathExpRoot");
    test("explicit old-revision edges override live mode as captured",
         parse("usedby path PathExpRoot PathExpTarget"),
         "PathExpRoot uses PathExpTarget via:\n"
         "  PathExpRoot -> PathExpTarget@1  [captured]");

    test("parallel dependency path registers a shared target",
         parse("set PathParTarget = 1 I"),
         "PathParTarget");
    test("parallel dependency path mixes live and exact edges",
         parse("set live PathParRoot = 1 PathParTarget "
               "PathParTarget@1"),
         "PathParRoot");
    test("captured wins a parallel-edge label deterministically",
         parse("usedby path PathParRoot PathParTarget"),
         "PathParRoot uses PathParTarget via:\n"
         "  PathParRoot -> PathParTarget  [captured]");

    test("set registers a basis for circular-redefinition checks",
         parse("set SelfReplay = 0 I"), "SelfReplay");
    test("set inspection accepts a frozen self-name revision",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set SelfReplay = 0 SelfReplay",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "SelfReplay=0 I");
    test("set accepts a frozen self-name revision",
         parse("set SelfReplay = 0 SelfReplay"), "SelfReplay");
    test("the frozen self-name points to the prior revision",
         parse("show SelfReplay"), "arity:0 SelfReplay@1");
    test("both immutable self-name revisions are saved",
         [] {
             std::cout << (set_list().ends_with(
                 "set SelfReplay = 0 I\n"
                 "set SelfReplay = 0 SelfReplay")
                 ? "saved"
                 : "missing");
         },
         "saved");
    test("a basis remains removable after a frozen revision",
         parse("remove SelfReplay"), "SelfReplay");
    test("removing that basis preserves revision history",
         [] {
             std::cout << (set_list().ends_with(
                 "set SelfReplay = 0 I\n"
                 "set SelfReplay = 0 SelfReplay\n"
                 "remove SelfReplay")
                 ? "retained"
                 : "missing");
         },
         "retained");

    test("define registers an initial replaceable definition",
         parse("define DefReplace x = x"), "DefReplace");
    test("define replaces the unreferenced definition",
         parse("define DefReplace x = Kx"), "DefReplace");
    test("set list keeps every immutable define revision",
         [] {
             auto const definitions = set_list();
             auto const replacement = definitions.find(
                 "define DefReplace x = Kx");
             std::cout <<
                 (replacement != std::string::npos &&
                  definitions.find("define DefReplace x = x") !=
                      std::string::npos
                     ? "retained"
                     : "missing");
         },
         "retained");

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
    test("an unreferenced removed basis preserves saved commands",
         [] {
             std::cout << (set_list().ends_with(
                 "set RemoveInspect = 0 I\nremove RemoveInspect")
                 ? "retained"
                 : "missing");
         },
         "retained");

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
    test("usedby recognizes a removed historical dependency",
         parse("usedby RemoveUse"),
         "RemoveUse directly uses: RemoveBase");
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
    test("a later definition and removal preserve version history",
         [] {
             static_cast<void>(parse("remove RemoveBase"));
             auto const definitions = set_list();
             auto const first = definitions.find("remove RemoveBase");
             auto const second = first == std::string::npos
                 ? std::string::npos
                 : definitions.find("remove RemoveBase", first + 1);
             std::cout <<
                 (first != std::string::npos &&
                  second != std::string::npos &&
                  definitions.find("set RemoveBase = 0 K") !=
                      std::string::npos
                     ? "complete history"
                     : "unexpected history");
         },
         "complete history");

    test("set registers the first basis in a two-name chain",
         parse("set CycleA = 0 I"), "CycleA");
    test("set registers the second basis in a two-name chain",
         parse("set CycleB = 0 CycleA"), "CycleB");
    test("set accepts a frozen two-name chain revision",
         parse("set CycleA = 0 CycleB"), "CycleA");
    test("the frozen two-name chain identifies its revision",
         parse("show CycleA"), "arity:0 CycleB");
    test("a non-circular redefinition of the same basis succeeds",
         parse("set CycleA = 0 K"), "CycleA");
    test("the non-circular replacement becomes current",
         parse("show CycleA"), "arity:0 K");

    test("set registers the first basis in a three-name chain",
         parse("set CircleFoo = 3 C"), "CircleFoo");
    test("set registers the second basis in a three-name chain",
         parse("set CircleBar = 3 CircleFoo"), "CircleBar");
    test("set registers the third basis in a three-name chain",
         parse("set CircleBaz = 3 CircleBar"), "CircleBaz");
    test("set accepts a frozen three-name chain revision",
         parse("set CircleFoo = 3 CircleBaz"), "CircleFoo");
    test("the frozen three-name chain identifies its revision",
         parse("show CircleFoo"), "arity:3 CircleBaz");

    test("set registers a basis for an equivalent cyclic replacement",
         parse("set EqCircleA = 2 I"), "EqCircleA");
    test("set registers its dependent basis",
         parse("set EqCircleB = 2 EqCircleA"), "EqCircleB");
    test("define may create a recursive named dependency",
         parse("define EqCircleA x = EqCircleB x"), "EqCircleA");
    test("inspection accepts an equivalent set after define",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set EqCircleA = 1 EqCircleB",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << (inspected.replaced_definition.empty()
                 ? "unchanged"
                 : "changed");
         },
         "unchanged");
    test("set accepts that equivalent definition unchanged",
         parse("set EqCircleA = 1 EqCircleB"), "EqCircleA");
    test("the equivalent definition remains current",
         parse("show EqCircleA"), "arity:1 EqCircleB");

    test("set registers a basis for a removed-snapshot circle",
         parse("set RemovedCircleA = 0 I"), "RemovedCircleA");
    test("set registers the soon-to-be-removed dependency",
         parse("set RemovedCircleR = 0 RemovedCircleA"),
         "RemovedCircleR");
    test("set captures the soon-to-be-removed dependency",
         parse("set RemovedCircleB = 0 RemovedCircleR"),
         "RemovedCircleB");
    test("remove leaves the captured dependency snapshot",
         parse("remove RemovedCircleR"), "RemovedCircleR");
    test("inspection accepts a chain through a removed frozen revision",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "set RemovedCircleA = 0 RemovedCircleB",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "RemovedCircleA=0 I");
    test("set accepts a chain through a removed frozen revision",
         parse("set RemovedCircleA = 0 RemovedCircleB"),
         "RemovedCircleA");
    test("that frozen chain becomes the current revision",
         parse("show RemovedCircleA"), "arity:0 RemovedCircleB");

    test("set registers a basis for a late predefined snapshot circle",
         parse("set LateCircleA = 0 I"), "LateCircleA");
    test("a late C++ basis may capture that user definition",
         [] {
             static_cast<void>(basis(
                 "LateCircleCpp", 0, parse("LateCircleA")));
             parse("LateCircleCpp").print_to(std::cout);
         },
         "LateCircleCpp");
    test("set accepts a frozen late-predefined dependency",
         parse("set LateCircleA = 0 LateCircleCpp"), "LateCircleA");
    test("the late-predefined dependency becomes current",
         parse("show LateCircleA"), "arity:0 LateCircleCpp");

    test("all referred definitions and removals remain replayable",
         [] {
             static_cast<void>(parse("set StickyA = 0 I"));
             static_cast<void>(parse("set StickyB = 0 StickyA"));
             static_cast<void>(parse("remove StickyB"));
             static_cast<void>(parse("remove StickyA"));
             auto const definitions = set_list();
             std::cout <<
                 (definitions.ends_with(
                      "set StickyA = 0 I\n"
                      "set StickyB = 0 StickyA\n"
                      "remove StickyB\n"
                      "remove StickyA")
                     ? "complete"
                     : "incomplete");
         },
         "complete");

    test("references is classified as a state-mutating definition",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "references live",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.is_definition
                       << inspected.is_display_only;
         },
         "10");
    test_parse_failure(
        "bare references requires a mode", "references", 10,
        "expected 'captured' or 'live'");
    test("references live enables live references",
         parse("references live"), "references live");
    test("references command prefixes remain ordinary input",
         parse("referencesx"), "referencesx");
    test("references is a reserved definition name",
         [] {
             try {
                 static_cast<void>(parse("set references = I"));
             } catch (parse_error const& error) {
                 std::cout << error.detail();
             }
         },
         "references is a reserved word");
    test("references rejects an unknown option",
         [] {
             try {
                 static_cast<void>(parse("references maybe"));
             } catch (parse_error const& error) {
                 std::cout << error.detail();
             }
         },
         "expected 'captured' or 'live'");
    test("legacy snapshot commands use the new canonical form",
         parse("snapshot off"), "references live");
    test_parse_failure(
        "bare legacy snapshot requires a mode", "snapshot", 8,
        "expected 'on' or 'off'");
    test("legacy snapshot remains a reserved definition name",
         [] {
             try {
                 static_cast<void>(parse("set snapshot = I"));
             } catch (parse_error const& error) {
                 std::cout << error.detail();
             }
         },
         "snapshot is a reserved word");

    test("captured set overrides references live",
         [] {
             static_cast<void>(parse(
                 "set CmdCapT = 2 I"));
             static_cast<void>(parse(
                 "set captured CmdCapS = 1 CmdCapT"));
             parse("show CmdCapS").print_to(std::cout);
         },
         "arity:1 CmdCapT");
    test("captured define overrides references live",
         [] {
             static_cast<void>(parse(
                 "define captured CmdCapD x = CmdCapT x"));
             parse("show CmdCapD").print_to(std::cout);
         },
         "arity:1 CmdCapT");
    test("captured overrides do not change references live",
         [] {
             static_cast<void>(parse(
                 "set CmdAmbLS = 1 CmdCapT"));
             static_cast<void>(parse(
                 "define CmdAmbLD x = CmdCapT x"));
             parse("show CmdAmbLS").print_to(std::cout);
             std::cout << '\n';
             parse("show CmdAmbLD").print_to(std::cout);
         },
         "arity:1 CmdCapT\n"
         "arity:1 CmdCapT");
    test("captured overrides remain frozen after redefinition",
         [] {
             static_cast<void>(parse(
                 "set CmdCapT = 2 K"));
             eval(parse("CmdCapS x y"));
             eval(parse("CmdCapD x y"));
             eval(parse("CmdAmbLS x y"));
             eval(parse("CmdAmbLD x y"));
         },
         "xy\nxy\nx\nx\n");
    test("captured modifiers are retained in the set list",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find(
                        "set captured CmdCapS = 1 CmdCapT") !=
                    std::string::npos)
                 << (definitions.find(
                        "define captured CmdCapD x = CmdCapT x") !=
                    std::string::npos);
         },
         "11");

    test("live target gets its first immutable revision",
         parse("set LiveTarget = 1 I"), "LiveTarget");
    test("references live stores an unqualified live reference",
         parse("set LiveUse = 1 LiveTarget"), "LiveUse");
    test("show prints a live reference without a version",
         parse("show LiveUse"), "arity:1 LiveTarget");
    test("a changed live target gets another revision",
         parse("set LiveTarget = 1 K"), "LiveTarget");
    test("a live reference observes the changed target",
         single_step(parse("LiveUse x")), "Kx");
    test("repeating a live definition is a no-op",
         parse("set LiveUse = 1 LiveTarget"), "LiveUse");
    test_parse_failure(
        "a no-op does not allocate another revision",
        "show LiveUse@2", 5,
        "LiveUse@2 is not a defined name");
    test_parse_failure(
        "references live rejects a direct live cycle",
        "set LiveTarget = 1 LiveTarget", 4,
        "LiveTarget would have a circular definition\n"
        "LiveTarget -> LiveTarget");
    test("a live-cycle rejection preserves the current target",
         parse("show LiveTarget"), "arity:1 K");
    test("a second live binding can refer to the target",
         parse("set LiveOther = 1 LiveTarget"), "LiveOther");
    test_parse_failure(
        "references live rejects an indirect live cycle",
        "set LiveTarget = 1 LiveOther", 4,
        "LiveTarget would have a circular definition\n"
        "LiveTarget -> LiveOther -> LiveTarget");

    test("a removable versioned name is registered",
         parse("set Versioned = 1 I"), "Versioned");
    test("a versioned name can be removed",
         parse("remove Versioned"), "Versioned");
    test("a removed version remains showable",
         parse("show Versioned@1"), "arity:1 I");
    test("re-adding a removed name continues its versions",
         parse("set Versioned = 1 K"), "Versioned");
    test("the re-added name has version two",
         parse("show Versioned@2"), "arity:1 K");

    test("shared live binding target starts at version one",
         parse("set SharedLive = 1 I"), "SharedLive");
    test("the first dependent stores the shared live binding",
         parse("set SharedLiveA = 1 SharedLive"), "SharedLiveA");
    test("the shared live binding advances to version two",
         parse("set SharedLive = 1 K"), "SharedLive");
    test("the second dependent stores the same live binding",
         parse("set SharedLiveB = 1 SharedLive"), "SharedLiveB");
    test("removing a live target retains the latest binding target",
         parse("remove SharedLive"), "SharedLive");
    test("an older live reference uses the shared removed target",
         single_step(parse("SharedLiveA x")), "Kx");
    test("a newer live reference uses the same removed target",
         single_step(parse("SharedLiveB x")), "Kx");
    test("re-adding the live target updates the shared binding",
         parse("set SharedLive = 1 S"), "SharedLive");
    test("the older live reference follows the re-added target",
         single_step(parse("SharedLiveA x")), "Sx");
    test("the newer live reference follows the re-added target",
         single_step(parse("SharedLiveB x")), "Sx");

    test("references captured restores captured references",
         parse("references captured"), "references captured");
    test("live set overrides references captured",
         [] {
             static_cast<void>(parse(
                 "set CmdLiveT = 2 I"));
             static_cast<void>(parse(
                 "set live CmdLiveS = 1 CmdLiveT"));
             parse("show CmdLiveS").print_to(std::cout);
         },
         "arity:1 CmdLiveT");
    test("live define overrides references captured",
         [] {
             static_cast<void>(parse(
                 "define live CmdLiveD x = CmdLiveT x"));
             parse("show CmdLiveD").print_to(std::cout);
         },
         "arity:1 CmdLiveT");
    test("live overrides do not change references captured",
         [] {
             static_cast<void>(parse(
                 "set CmdAmbCS = 1 CmdLiveT"));
             static_cast<void>(parse(
                 "define CmdAmbCD x = CmdLiveT x"));
             parse("show CmdAmbCS")
                 .print_to(std::cout);
             std::cout << '\n';
             parse("show CmdAmbCD")
                 .print_to(std::cout);
         },
         "arity:1 CmdLiveT\n"
         "arity:1 CmdLiveT");
    test("live overrides follow redefinition",
         [] {
             static_cast<void>(parse(
                 "set live CmdExplicit = 1 CmdLiveT@1"));
             static_cast<void>(parse(
                 "set CmdLiveT = 2 K"));
             eval(parse("CmdLiveS x y"));
             eval(parse("CmdLiveD x y"));
             eval(parse("CmdAmbCS x y"));
             eval(parse("CmdAmbCD x y"));
             eval(parse("CmdExplicit x y"));
         },
         "x\nx\nxy\nxy\nxy\n");
    test("live modifiers are retained in the set list",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find(
                        "set live CmdLiveS = 1 CmdLiveT") !=
                    std::string::npos)
                 << (definitions.find(
                        "define live CmdLiveD x = CmdLiveT x") !=
                    std::string::npos);
         },
         "11");
    test_parse_failure(
        "live override applies circularity checks under references captured",
        "set live CmdLiveT = 2 CmdLiveT", 9,
        "CmdLiveT would have a circular definition\n"
        "CmdLiveT -> CmdLiveT");
    test("a frozen target gets its first revision",
         parse("set FrozenTarget = 1 I"), "FrozenTarget");
    test("references captured captures the current revision",
         parse("set FrozenUse = 1 FrozenTarget"), "FrozenUse");
    test("show omits a captured singleton revision suffix",
         parse("show FrozenUse"), "arity:1 FrozenTarget");
    test("the frozen target can be redefined",
         parse("set FrozenTarget = 1 K"), "FrozenTarget");
    test("the frozen reference retains its old behavior",
         single_step(parse("FrozenUse x")), "x");
    test("a frozen name chain is not a runtime cycle",
         parse("set FrozenTarget = 1 FrozenUse"), "FrozenTarget");
    test("the frozen chain points to a specific revision",
         parse("show FrozenTarget"), "arity:1 FrozenUse");
    test("usedby all terminates on a revision-name cycle",
         parse("usedby all FrozenTarget"),
         "FrozenTarget directly uses: FrozenUse");
    test("dependson all terminates and excludes its cyclic query",
         parse("dependson all FrozenTarget"),
         "FrozenTarget is directly depended on by: FrozenUse");
    test("usedby path terminates on a revision-name cycle",
         parse("usedby path FrozenTarget FrozenUse"),
         "FrozenTarget uses FrozenUse via:\n"
         "  FrozenTarget@3 -> FrozenUse  [captured]");
    test("usedby path reverses endpoints around a revision-name cycle",
         parse("usedby path FrozenUse FrozenTarget"),
         "FrozenTarget uses FrozenUse via:\n"
         "  FrozenTarget@3 -> FrozenUse  [captured]");
    test("usedby path safely exhausts a disconnected name cycle",
         parse("usedby path FrozenTarget AllIndirectC"),
         "AllIndirectC and FrozenTarget have no dependency path");

    test("mixed-cycle setup uses live references",
         parse("references live"), "references live");
    test("mixed-cycle target starts without a dependency",
         parse("set MixedCycleA = 1 R"), "MixedCycleA");
    test("mixed-cycle dependent keeps a live edge",
         parse("set MixedCycleB = 1 MixedCycleA"), "MixedCycleB");
    test("mixed-cycle replacement uses a frozen edge",
         parse("references captured"), "references captured");
    test_parse_failure(
        "a frozen revision containing a live edge closes a cycle",
        "set MixedCycleA = 1 MixedCycleB", 4,
        "MixedCycleA would have a circular definition\n"
        "MixedCycleA -> MixedCycleB -> MixedCycleA");

    test("qualified cycle setup creates a live first revision",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse("set QualCycleA = 1 I"));
             parse("set QualCycleB = 1 QualCycleA")
                 .print_to(std::cout);
         },
         "QualCycleB");
    test("qualified cycle setup redefines the live intermediary",
         parse("set QualCycleB = 1 K QualCycleA"),
         "QualCycleB");
    test("qualified cycle setup restores captured references",
         parse("references captured"), "references captured");
    test_parse_failure(
        "a circular diagnostic qualifies an old revision after redefinition",
        "set QualCycleA = 1 QualCycleB@1", 4,
        "QualCycleA would have a circular definition\n"
        "QualCycleA -> QualCycleB@1 -> QualCycleA");

    test("recursive define remains represented through Y",
         parse("define RecVersion x = RecVersion x"),
         "RecVersion");
    test("recursive define remains evaluable",
         parse("show RecVersion"), "arity:1 YI");
    test("define-cycle setup uses live references",
         parse("references live"), "references live");
    test("define-cycle target starts independently",
         parse("set DefineCycleA = 1 I"), "DefineCycleA");
    test("define-cycle dependent keeps a live edge",
         parse("set DefineCycleB = 1 DefineCycleA"), "DefineCycleB");
    test("define-cycle replacement uses a frozen edge",
         parse("references captured"), "references captured");
    test_parse_failure(
        "define rejects a cycle through another live binding",
        "define DefineCycleA x = DefineCycleB", 7,
        "DefineCycleA would have a circular definition\n"
        "DefineCycleA -> DefineCycleB -> DefineCycleA");

    test("pre-defined birds expose immutable version one",
         parse("show C@1"), "arity:3 S(S(KB)S)(KK)");
    test("an explicit pre-defined sole revision prints unqualified",
         parse("C@1"), "C");
    test("ordinary pre-defined bird printing remains unqualified",
         parse("C"), "C");
    test("set registers a Cardinal-star alias for direct printing",
         parse("set PrintCstar = 4 C*"), "PrintCstar");
    test("a current captured user name evaluates unversioned with its boundary",
         [] { parse_eval("PrintCstar xy"); }, "PrintCstar xy\n");
    test("a current captured user name output round trips",
         [] {
             auto const expression = parse("PrintCstar xy");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintCstar xy");
    test("set registers a punctuation-ending captured name",
         parse("set PrintTail+ = 1 I"), "PrintTail+");
    test("a current captured punctuation name stays compact and unversioned",
         [] {
             auto const expression = parse("Cstar PrintTail+ Vstar");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarPrintTail+Vstar");
    test("set registers a digit-ending captured name",
         parse("set PrintDigit1 = 1 I"), "PrintDigit1");
    test("a current captured digit name keeps its asymmetric boundaries",
         [] {
             auto const expression = parse(
                 "Cstar PrintDigit1 x Vstar");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarPrintDigit1x Vstar");
    test("inspect omits a sole captured revision suffix",
         parse("inspect PrintCstar xy"),
         "free symbols: x y\n"
         "references:\n"
         "  PrintCstar [captured]\n"
         "next reduction: none [normal form]");
    test("an explicit sole user revision prints unqualified",
         parse("PrintCstar@1xy"), "PrintCstar xy");
    test("an explicit sole user revision output round trips",
         [] {
             auto const expression = parse("PrintCstar@1xy");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintCstar xy");
    test("a stored captured sole revision remains semantically captured",
         parse("set captured PrintCap = 2 PrintCstar"),
         "PrintCap");
    test("show omits the stored captured sole revision suffix",
         parse("show PrintCap"),
         "arity:2 PrintCstar");
    test("an explicitly captured sole revision is stored",
         parse("set captured PrintExact = 2 PrintCstar@1"),
         "PrintExact");
    test("show omits an explicitly stored sole revision suffix",
         parse("show PrintExact"),
         "arity:2 PrintCstar");
    test("the replay journal preserves an explicit revision spelling",
         [] {
             std::cout << (set_list().find(
                 "set captured PrintExact = 2 PrintCstar@1") !=
                 std::string::npos);
         },
         "1");
    test("a stored live reference remains unversioned",
         parse("set live PrintLive = 2 PrintCstar"),
         "PrintLive");
    test("show keeps the stored live name",
         parse("show PrintLive"),
         "arity:2 PrintCstar");
    test("a sole user revision can be removed",
         [] {
             static_cast<void>(parse("set PrintOnce = 1 I"));
             parse("remove PrintOnce").print_to(std::cout);
         },
         "PrintOnce");
    test("a removed sole revision prints bare and round trips",
         [] {
             auto const expression = parse("PrintOnce@1x");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintOnce x");
    test_parse_failure(
        "management commands still treat a sole revision as removed",
        "show PrintOnce", 5,
        "PrintOnce is not a defined name");
    test("re-adding a removed sole revision creates revision two",
         parse("set PrintOnce = 1 K"), "PrintOnce");
    test("both revisions print qualified after redefinition",
         [] {
             parse("PrintOnce@1x").print_to(std::cout);
             std::cout << ' ';
             parse("PrintOnce@2x").print_to(std::cout);
         },
         "PrintOnce@1x PrintOnce@2x");
    test("live mode is enabled for direct-print coverage",
         parse("references live"), "references live");
    test("a current live user name evaluates unversioned with its boundary",
         [] { parse_eval("PrintCstar xy"); }, "PrintCstar xy\n");
    test("a current live user name output round trips",
         [] {
             auto const expression = parse("PrintCstar xy");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintCstar xy");
    test("captured mode is restored after direct-print coverage",
         parse("references captured"), "references captured");
    test("redefining the Cardinal-star alias creates a current revision",
         parse("set PrintCstar = 4 C**"), "PrintCstar");
    test("the redefined current name evaluates unversioned and spaced",
         [] { parse_eval("PrintCstar xy"); }, "PrintCstar xy\n");
    test("the old explicit revision remains qualified after redefinition",
         parse("PrintCstar@1xy"), "PrintCstar@1xy");
    test("the explicit current revision remains qualified after redefinition",
         parse("PrintCstar@2xy"), "PrintCstar@2xy");
    test("expanding a captured holder exposes its old exact revision",
         single_step(parse("PrintCap x y"), true),
         "PrintCstar@1xy");
    test("an explicitly captured holder also exposes its old revision",
         single_step(parse("PrintExact x y"), true),
         "PrintCstar@1xy");
    test("expanding a live holder exposes the current unversioned name",
         single_step(parse("PrintLive x y"), true),
         "PrintCstar xy");
    test("the Cardinal-star alias can be removed after redefinition",
         parse("remove PrintCstar"), "PrintCstar");
    test("the first removed revision still prints and round trips",
         [] {
             auto const expression = parse("PrintCstar@1xy");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintCstar@1xy");
    test("the latest removed revision still prints and round trips",
         [] {
             auto const expression = parse("PrintCstar@2xy");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 PrintCstar@2xy");
    test("a retained plain expression qualifies a stale or removed target",
         [] {
             static_cast<void>(parse("set PrintStale = 2 K"));
             auto const first = parse("PrintStale x");
             std::ostringstream current;
             first.print_to(current);

             static_cast<void>(parse("set PrintStale = 2 S"));
             std::ostringstream stale;
             first.print_to(stale);
             auto const reparsed_stale = parse(stale.str());

             auto const second = parse("PrintStale x");
             static_cast<void>(parse("remove PrintStale"));
             std::ostringstream removed;
             second.print_to(removed);
             auto const reparsed_removed = parse(removed.str());

             std::cout << current.str() << ' '
                       << combdsl::detail::same_parser_definition_expression(
                              first, reparsed_stale)
                       << ' ' << stale.str() << ' '
                       << combdsl::detail::same_parser_definition_expression(
                              second, reparsed_removed)
                       << ' ' << removed.str();
         },
         "PrintStale x 1 PrintStale@1x 1 PrintStale@2x");
    test_parse_failure(
        "fundamental names do not have versions",
        "show S@1", 5,
        "S@1 is not a defined name");
    test_parse_failure(
        "set rejects a version suffix on its left side",
        "set Versioned@3 = I", 13,
        "version suffix is not allowed in a definition name");
    test_parse_failure(
        "remove rejects a version suffix",
        "remove Versioned@1", 16,
        "version suffix is not allowed in a removal name");
    test("the C++ basis API accepts a leading question mark",
         basis("?CppQuestion", 1, I), "?CppQuestion");
    test("the C++ basis API accepts an interior question mark",
         basis("Cpp?Question", 1, I), "Cpp?Question");
    test("the C++ basis API accepts an ending question mark",
         basis("CppQuestion?", 1, I), "CppQuestion?");
    auto const numeric_name_registry_before =
        combdsl::detail::registered_parser_lookup_snapshot();
    constexpr std::string_view invalid_numeric_basis_names[] = {
        "0", "000", "123456789012345"};
    for (auto const name : invalid_numeric_basis_names) {
        auto title = std::string(
            "the C++ basis API rejects numeric-only name ");
        title += name;
        test(title,
             [name] {
                 try {
                     static_cast<void>(basis(name, 1, I));
                 } catch (std::invalid_argument const& error) {
                     std::cout << error.what();
                 }
             },
             "combdsl::basis names cannot be non-negative integer literals");
    }
    test("the C++ basis API validates a visible numeric prefix before null",
         [] {
             try {
                 static_cast<void>(basis(
                     std::string_view("7\0suffix", 8), 1, I));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::basis names cannot be non-negative integer literals");
    test("rejected C++ numeric names never enter the parser registry",
         [&] {
             auto const after =
                 combdsl::detail::registered_parser_lookup_snapshot();
             std::cout
                 << (after.bases.size() ==
                     numeric_name_registry_before.bases.size())
                 << (after.versions.size() ==
                     numeric_name_registry_before.versions.size())
                 << (after.live_bindings.size() ==
                     numeric_name_registry_before.live_bindings.size())
                 << !after.bases.contains("0")
                 << !after.bases.contains("000")
                 << !after.bases.contains("123456789012345");
         },
         "111111");
    test("set accepts a question mark inside a basis name",
         parse("set Mid?Set = 1 I"), "Mid?Set");
    test("define accepts a question mark at the end of a basis name",
         parse("define EndDefine? x = x"), "EndDefine?");
    test("remove accepts a question mark inside a basis name",
         parse("remove Mid?Set"), "Mid?Set");
    test_parse_failure(
        "set rejects a name ending in an at sign",
        "set AtSet@ = I", 4,
        "combdsl::basis names cannot end with @");
    test_parse_failure(
        "a rejected at-sign set does not register its name",
        "show AtSet", 5,
        "AtSet is not a defined name");
    test_parse_failure(
        "define rejects a name ending in an at sign",
        "define AtDefine@ x = x", 7,
        "combdsl::basis names cannot end with @");
    test_parse_failure(
        "a rejected at-sign define does not register its name",
        "show AtDefine", 5,
        "AtDefine is not a defined name");
    test("at-sign remove setup remains registered",
         parse("set AtRemove = 1 I"), "AtRemove");
    test_parse_failure(
        "remove rejects a name ending in an at sign",
        "remove AtRemove@", 7,
        "combdsl::basis names cannot end with @");
    test("a rejected at-sign remove leaves its name registered",
         parse("show AtRemove"), "arity:1 I");
    test("terminal-at parser failures are absent from the set list",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find("AtSet@") == std::string::npos)
                 << (definitions.find("AtDefine@") == std::string::npos)
                 << (definitions.find("remove AtRemove@") ==
                     std::string::npos);
         },
         "111");
    test("the C++ basis API rejects a reserved version suffix",
         [] {
             try {
                 static_cast<void>(basis("CppVersion@1", 1, I));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::basis name cannot end in a version suffix: CppVersion@1");
    test("the C++ basis API rejects a terminal at sign",
         [] {
             try {
                 static_cast<void>(basis("CppTerminal@", 1, I));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::basis names cannot end with @");

    test("references history records a replay sequence",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse("set ReplayTarget = 1 I"));
             static_cast<void>(parse(
                 "set ReplayUse = 1 ReplayTarget"));
             static_cast<void>(parse("set ReplayTarget = 1 K"));
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse("remove ReplayTarget"));
             static_cast<void>(parse("set ReplayTarget = 1 S"));
             constexpr std::string_view suffix =
                 "references live\n"
                 "set ReplayTarget = 1 I\n"
                 "set ReplayUse = 1 ReplayTarget\n"
                 "set ReplayTarget = 1 K\n"
                 "references captured\n"
                 "remove ReplayTarget\n"
                 "set ReplayTarget = 1 S";
             std::cout << (set_list().ends_with(suffix)
                 ? "chronological"
                 : "out of order");
         },
         "chronological");
    test("replay history keeps explicit old versions",
         parse("show ReplayTarget@1"), "arity:1 I");
    test("replay history continues versions across removal",
         parse("show ReplayTarget@3"), "arity:1 S");

    test("revisions omits a sole captured current revision suffix",
         [] {
             static_cast<void>(parse(
                 "set RevModeTarget = 1 I"));
             parse("revisions RevModeTarget").print_to(std::cout);
         },
         "RevModeTarget arity:1 I [captured] [current]");
    test("revisions reports changed revisions in chronological order",
         [] {
             static_cast<void>(parse(
                 "set live RevModeTarget = 2 K"));
             parse("revisions RevModeTarget").print_to(std::cout);
         },
         "RevModeTarget@1 arity:1 I [captured]\n"
         "RevModeTarget@2 arity:2 K [live] [current]");
    test("revisions retains implicit live and explicit captured modes",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse(
                 "set RevModeUse = 1 RevModeTarget"));
             static_cast<void>(parse(
                 "set captured RevModeUse = 1 RevModeTarget"));
             static_cast<void>(parse("references captured"));
             parse("revisions RevModeUse").print_to(std::cout);
         },
         "RevModeUse@1 arity:1 RevModeTarget [live]\n"
         "RevModeUse@2 arity:1 RevModeTarget@2 "
         "[captured] [current]");
    test("an opposite-mode no-op remains an unqualified sole revision",
         [] {
             static_cast<void>(parse("set RevNoop = 1 I"));
             static_cast<void>(parse(
                 "set live RevNoop = 1 I"));
             parse("revisions RevNoop").print_to(std::cout);
         },
         "RevNoop arity:1 I [captured] [current]");
    test("revisions marks the latest revision of a removed name",
         [] {
             static_cast<void>(parse("remove RevModeTarget"));
             parse("revisions RevModeTarget").print_to(std::cout);
         },
         "RevModeTarget@1 arity:1 I [captured]\n"
         "RevModeTarget@2 arity:2 K [live] [removed]");
    test("revisions marks only the current re-added revision",
         [] {
             static_cast<void>(parse(
                 "set RevModeTarget = 3 S"));
             parse("revisions RevModeTarget").print_to(std::cout);
         },
         "RevModeTarget@1 arity:1 I [captured]\n"
         "RevModeTarget@2 arity:2 K [live]\n"
         "RevModeTarget@3 arity:3 S [captured] [current]");
    test("revisions omits a pre-defined sole revision suffix",
         parse("revisions M"),
         "M arity:1 SII [pre-defined] [current]");
    test("revisions is display only",
         [] {
             auto const parsed =
                 combdsl::detail::parse_input("revisions M");
             std::cout << parsed.is_display_only
                       << parsed.is_definition
                       << parsed.is_show_all;
         },
         "100");
    test_parse_failure(
        "revisions rejects a fundamental name",
        "revisions S", 10,
        "S is a fundamental name and has no revisions");
    test_parse_failure(
        "revisions rejects an unknown name",
        "revisions MissingRev", 10,
        "MissingRev is not a defined name");
    test_parse_failure(
        "revisions rejects a versioned name",
        "revisions RevModeTarget@1", 23);
    test_parse_failure(
        "revisions requires a name", "revisions", 9,
        "missing combinator name");
    test_parse_failure(
        "revisions rejects trailing input",
        "revisions RevModeTarget extra", 24,
        "unexpected input after name");
    test("revisions command prefixes remain ordinary input",
         parse("revisionsx"), "revisionsx");

    test("inspect omits an already identical canonical spelling",
         parse("inspect S(Kx)(Iy)z"),
         "free symbols: x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "  I [fundamental]\n"
         "next reduction: S(Kx)(Iy)z [S at root]");
    test("inspect limits a root S reduction to its saturated prefix",
         parse("inspect S(Kx)(Iy)zwv"),
         "free symbols: v w x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "  I [fundamental]\n"
         "next reduction: S(Kx)(Iy)z [S at root]");
    test("inspect limits a root K reduction to its saturated prefix",
         parse("inspect Kxyzw"),
         "free symbols: w x y z\n"
         "references:\n"
         "  K [fundamental]\n"
         "next reduction: Kxy [K at root]");
    test("inspect limits a root I reduction to its saturated prefix",
         parse("inspect Ixy"),
         "free symbols: x y\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Ix [I at root]");
    test("inspect limits a root Y reduction to its saturated prefix",
         parse("inspect Yxy"),
         "free symbols: x y\n"
         "references:\n"
         "  Y [fundamental]\n"
         "next reduction: Yx [Y at root]");
    test("inspect includes an SK argument consumed by the shortcut",
         parse("inspect SKx(SKy)z"),
         "free symbols: x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "next reduction: SKx(SKy) [S at root]");
    test("inspect leaves an unconsumed argument after an SK shortcut",
         parse("inspect SKxyz"),
         "free symbols: x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "next reduction: SKx [S at root]");
    test("ordinary single step contracts the saturated S prefix",
         single_step(parse("S(Kx)(Iy)zwv")),
         "Kxz(Iyz)wv");
    test("inspect ignores leading command and delimiter whitespace",
         parse(" \tinspect   Ix"),
         "free symbols: x\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Ix [I at root]");
    test("inspect retains canonical for trailing expression whitespace",
         parse("inspect Ix \n"),
         "canonical: Ix\n"
         "free symbols: x\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Ix [I at root]");
    test("inspect retains canonical for internal whitespace",
         parse("inspect S(Kx) (Iy)z"),
         "canonical: S(Kx)(Iy)z\n"
         "free symbols: x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "  I [fundamental]\n"
         "next reduction: S(Kx)(Iy)z [S at root]");
    test("inspect retains canonical for redundant parentheses",
         parse("inspect (S(Kx))(Iy)z"),
         "canonical: S(Kx)(Iy)z\n"
         "free symbols: x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "  I [fundamental]\n"
         "next reduction: S(Kx)(Iy)z [S at root]");
    test("inspect canonicalizes numbers and digit-ended basis spacing",
         parse("inspect ((Q1 x) (00042)) Q3"),
         "canonical: Q1x 42 Q3\n"
         "free symbols: x\n"
         "references:\n"
         "  Q1 [pre-defined]\n"
         "  Q3 [pre-defined]\n"
         "next reduction: Q1x 42 Q3 [Q1 at root]");
    test("inspect quotes a canonical direct backslash",
         parse("inspect \\"),
         R"(canonical: "\\")" "\n"
         "free symbols: none\n"
         "references: none\n"
         "next reduction: none [normal form]");
    test("inspect retains canonical for a directly quoted word",
         parse("inspect \"a\\\\b\""),
         "canonical: a\\b\n"
         "free symbols: none\n"
         "references: none\n"
         "next reduction: none [normal form]");
    test("inspect sorts and deduplicates free symbols",
         parse("inspect zyxzx"),
         "free symbols: x y z\n"
         "references: none\n"
         "next reduction: none [normal form]");
    test("inspect reports no free symbols or references",
         parse("inspect 42"),
         "free symbols: none\n"
         "references: none\n"
         "next reduction: none [normal form]");
    test("inspect deduplicates direct references in first-use order",
         parse("inspect x(M)(S)(M)(I)(K)"),
         "canonical: xMSMIK\n"
         "free symbols: x\n"
         "references:\n"
         "  M [pre-defined]\n"
         "  S [fundamental]\n"
         "  I [fundamental]\n"
         "  K [fundamental]\n"
         "next reduction: none [normal form]");

    test("inspect setup registers captured user references",
         [] {
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse(
                 "set InspectCaptured = 1 I"));
             static_cast<void>(parse(
                 "set InspectLive = 1 I"));
             static_cast<void>(parse(
                 "set InspectRemoved = 1 I"));
             static_cast<void>(parse(
                 "set InspectRedex = 2 K"));
             static_cast<void>(parse(
                 "set InspectZero = 0 I"));
             static_cast<void>(parse("remove InspectRemoved"));
             std::cout << "ready";
         },
         "ready");
    test("inspect omits a sole captured current revision suffix",
         parse("inspect InspectCaptured"),
         "free symbols: none\n"
         "references:\n"
         "  InspectCaptured [captured]\n"
         "next reduction: none [normal form]");
    test("inspect omits an explicit removed sole revision suffix",
         parse("inspect InspectRemoved@1"),
         "canonical: InspectRemoved\n"
         "free symbols: none\n"
         "references:\n"
         "  InspectRemoved [captured]\n"
         "next reduction: none [normal form]");
    test("inspect omits an explicit predefined sole revision suffix",
         parse("inspect M@1"),
         "canonical: M\n"
         "free symbols: none\n"
         "references:\n"
         "  M [pre-defined]\n"
         "next reduction: none [normal form]");
    test("inspect identifies a live current-name reference",
         [] {
             static_cast<void>(parse("references live"));
             parse("inspect InspectLive").print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "free symbols: none\n"
         "references:\n"
         "  InspectLive [live]\n"
         "next reduction: none [normal form]");
    test("inspect keeps singleton live and captured classifications distinct",
         [] {
             static_cast<void>(parse("references live"));
             parse(
                 "inspect x(InspectLive)(InspectLive)"
                 "(InspectLive@1)(InspectLive@1)")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "canonical: x InspectLive InspectLive InspectLive InspectLive\n"
         "free symbols: x\n"
         "references:\n"
         "  InspectLive [live]\n"
         "  InspectLive [captured]\n"
         "next reduction: none [normal form]");
    test("inspect limits an explicit sole revision without a suffix",
         parse("inspect InspectRedex@1xyzw"),
         "canonical: InspectRedex xyzw\n"
         "free symbols: w x y z\n"
         "references:\n"
         "  InspectRedex [captured]\n"
         "next reduction: InspectRedex xy "
         "[InspectRedex at root]");
    test("inspect reports only a saturated sole arity-zero basis",
         parse("inspect InspectZero@1xy"),
         "canonical: InspectZero xy\n"
         "free symbols: x y\n"
         "references:\n"
         "  InspectZero [captured]\n"
         "next reduction: InspectZero "
         "[InspectZero at root]");
    test("inspect uses current arity for a live basis",
         [] {
             static_cast<void>(parse("references live"));
             parse("inspect InspectRedex xyzw").print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "free symbols: w x y z\n"
         "references:\n"
         "  InspectRedex [live]\n"
         "next reduction: InspectRedex xy [InspectRedex at root]");
    test("inspect selects a nested ordinary next reduction",
         parse("inspect x(Iy)"),
         "free symbols: x y\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Iy [I at argument]");
    test("inspect reports a stable deeper application path",
         parse("inspect (x(Iy))z"),
         "canonical: x(Iy)z\n"
         "free symbols: x y z\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Iy [I at function.argument]");
    test("inspect limits a nested reduction without changing its path",
         parse("inspect p(S(Kx)(Iy)zwv)q"),
         "free symbols: p q v w x y z\n"
         "references:\n"
         "  S [fundamental]\n"
         "  K [fundamental]\n"
         "  I [fundamental]\n"
         "next reduction: S(Kx)(Iy)z "
         "[S at function.argument]");
    test("ordinary single step chooses inspect's nested redex",
         single_step(parse("x(Iy)")), "xy");
    test("inspect parser metadata is display only",
         [] {
             auto const parsed =
                 combdsl::detail::parse_input("inspect Ix");
             std::cout << parsed.is_display_only
                       << parsed.is_definition
                       << parsed.is_show_all
                       << parsed.is_find;
         },
         "1000");
    test("parse eval prints inspect without reducing its expression",
         [] { parse_eval("inspect Ix"); },
         "free symbols: x\n"
         "references:\n"
         "  I [fundamental]\n"
         "next reduction: Ix [I at root]\n");
    test_parse_failure(
        "inspect requires an expression", "inspect", 7,
        "expected an expression");
    test_parse_failure(
        "inspect rejects a missing close parenthesis",
        "inspect (Ix", 11);
    test_parse_failure(
        "inspect rejects a trailing close parenthesis",
        "inspect Ix)", 10, "unexpected ')'");
    test("inspect command prefixes remain ordinary input",
         parse("inspectx"), "inspectx");
    test("inspect requires a command boundary",
         parse("inspect(Ix)"), "inspect(Ix)");

    test("compare reports a shared normal form",
         parse("compare ?x I = SKK"),
         "both reduce to: x");
    test("compare reports different normal forms without a heading",
         parse("compare ?xy K = KI"),
         "left reduces to: x\nright reduces to: y");
    test("compare applies all symbols in their written order",
         parse("compare ?xyz BCB = Q1"),
         "both reduce to: x(zy)");
    test("compare accepts grouped expressions and numeric values",
         parse(" \tcompare ?x (K 00042) = K(42)\n"),
         "both reduce to: 42");
    test("compare follows captured and live named references",
         [] {
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse(
                 "set CmpTarget = 1 I"));
             static_cast<void>(parse(
                 "set CmpCaptured = 1 CmpTarget"));
             static_cast<void>(parse("references live"));
             static_cast<void>(parse(
                 "set CmpLive = 1 CmpTarget"));
             static_cast<void>(parse(
                 "set CmpTarget = 1 K"));
             parse("compare ?xy CmpCaptured = I")
                 .print_to(std::cout);
             std::cout << '\n';
             parse("compare ?xy CmpLive = K")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "both reduce to: xy\nboth reduce to: x");
    test("compare is display only and not a find or definition",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 "compare ?x I = SKK");
             std::cout << parsed.is_display_only
                       << parsed.is_definition
                       << parsed.is_show_all
                       << parsed.is_find << ' ';
             parsed.expression.print_to(std::cout);
         },
         "1000 both reduce to: x");
    test("compare inspection parses but never normalizes",
         [] {
             std::size_t clock_calls = 0;
             compare_clock_override_reset reset;
             combdsl::detail::compare_clock_now_override = [&] {
                 ++clock_calls;
                 return combdsl::detail::compare_clock::time_point{};
             };
             auto const parsed = combdsl::detail::parse_input(
                 "compare ?x I = SKK",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << clock_calls << ' '
                       << parsed.is_display_only << ' ';
             parsed.expression.print_to(std::cout);
         },
         "0 1 SKK");
    test("compare gives each completed side an independent window",
         [] {
             std::size_t clock_calls = 0;
             compare_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::compare_clock::time_point{};
             combdsl::detail::compare_clock_now_override = [&] {
                 auto const value = epoch + std::chrono::milliseconds{
                     static_cast<long long>(clock_calls * 100)};
                 ++clock_calls;
                 return value;
             };
             parse("compare ?x I = I").print_to(std::cout);
             std::cout << ' ' << clock_calls;
         },
         "both reduce to: x 10");
    test("compare accepts a normal form reached before its deadline",
         [] {
             std::size_t clock_calls = 0;
             compare_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::compare_clock::time_point{};
             combdsl::detail::compare_clock_now_override = [&] {
                 ++clock_calls;
                 return epoch + std::chrono::milliseconds{499};
             };
             auto result = combdsl::detail::normalize_for_compare_until(
                 parse("Ix"),
                 epoch + std::chrono::milliseconds{500});
             if (result) {
                 result->print_to(std::cout);
             }
             std::cout << ' ' << clock_calls;
         },
         "x 4");
    test("compare times out with the exact inconclusive result",
         [] {
             std::size_t clock_calls = 0;
             compare_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::compare_clock::time_point{};
             combdsl::detail::compare_clock_now_override = [&] {
                 auto const value = clock_calls == 0
                     ? epoch
                     : epoch + std::chrono::seconds{1};
                 ++clock_calls;
                 return value;
             };
             parse("compare ?x I = I").print_to(std::cout);
             std::cout << ' ' << clock_calls;
         },
         "inconclusive 2");
    test("compare counts named-basis expansion within its window",
         [] {
             static_cast<void>(parse(
                 "set CmpClock = 1 I"));
             std::size_t clock_calls = 0;
             compare_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::compare_clock::time_point{};
             combdsl::detail::compare_clock_now_override = [&] {
                 ++clock_calls;
                 return clock_calls < 5
                     ? epoch
                     : epoch + std::chrono::milliseconds{500};
             };
             auto result = combdsl::detail::normalize_for_compare_until(
                 parse("CmpClock x"),
                 epoch + std::chrono::milliseconds{500});
             std::cout << (result ? "completed" : "timed out")
                       << ' ' << clock_calls;
         },
         "timed out 5");
    test("parse eval prints compare output exactly once",
         [] { parse_eval("compare ?xy K = KI"); },
         "left reduces to: x\nright reduces to: y\n");
    test_parse_failure(
        "compare requires a question mark", "compare", 7,
        "expected '?'");
    test_parse_failure(
        "compare rejects a missing question mark",
        "compare x I = I", 8, "expected '?'");
    test_parse_failure(
        "compare requires at least one symbol",
        "compare ? I = I", 9, "expected at least one symbol");
    test_parse_failure(
        "compare rejects an uppercase symbol list",
        "compare ?X I = I", 9, "expected at least one symbol");
    test_parse_failure(
        "compare requires whitespace after its symbol list",
        "compare ?x=I", 10,
        "expected whitespace after symbol list");
    test_parse_failure(
        "compare requires a left expression",
        "compare ?x = I", 11, "expected a left expression");
    test_parse_failure(
        "compare requires an equals sign",
        "compare ?x I", 12, "expected '='");
    test_parse_failure(
        "compare requires a right expression",
        "compare ?x I =", 14, "expected a right expression");
    test_parse_failure(
        "compare rejects trailing unmatched input",
        "compare ?x I = K)", 16, "unexpected ')'");
    test("compare command prefixes remain ordinary input",
         parse("comparex"), "comparex");
    test("compare requires a command boundary",
         parse("compare(I)"), "compareI");

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
        "define no longer accepts a steps option", "define steps", 7,
        "steps is a reserved word");
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

    test("abstract applies the Albatross optimizer after takeout",
         parse("abstract ?xy = x(yx)"), "?=A");
    test("abstract handles the Starling proposal example",
         parse("abstract ?xyz = xz(yz)"), "?=S");
    test("abstract takes symbols out from right to left",
         parse("abstract ?xyz = exp"), "?=B(ZK)(Cep)");
    test("abstract preprocesses saturated bases",
         parse("abstract ?xyz = C(CB)xyz"), "?=B");
    test("abstract does not change stored definitions",
         [] {
             auto const before = combdsl::set_list();
             static_cast<void>(parse("abstract ?xy = yx"));
             std::cout << (before == combdsl::set_list());
         },
         "1");
    test("abstract inspection stops before abstraction",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 "abstract steps ?xy = x(yx)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             parsed.expression.print_to(std::cout);
             std::cout << ' ' << parsed.is_display_only
                       << parsed.is_definition
                       << parsed.is_find;
         },
         "x(yx) 100");
    test("abstract ministeps inspection stops before abstraction",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 "abstract ministeps ?xy = x(yx)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             parsed.expression.print_to(std::cout);
             std::cout << ' ' << parsed.is_display_only
                       << parsed.is_definition
                       << parsed.is_find;
         },
         "x(yx) 100");
    test("abstract steps traces reverse takeout and optimization",
         parse("abstract steps ?xy = x(yx)"),
         "optimize: x(yx) -> Oyx\n"
         "takeout y from Oyx: COx\n"
         "takeout x from COx: CO\n"
         "optimize: CO -> A\n"
         "?=A");
    test("abstract directly optimizes CO after takeout",
         parse("abstract steps ?x = COx"),
         "takeout x from COx: CO\n"
         "optimize: CO -> A\n"
         "?=A");
    test("abstract recursively optimizes an exact nested CO",
         parse("abstract steps ?x = z(C O)x"),
         "takeout x from z(CO)x: z(CO)\n"
         "optimize: CO -> A\n"
         "?=zA");
    test("define preserves the reversed OC near miss",
         parse("define NearCOReverse = O C"),
         "NearCOReverse");
    test("show exposes the unchanged reversed OC near miss",
         parse("show NearCOReverse"), "arity:0 OC");
    test("define preserves the C-of-applied-O near miss",
         parse("define NearCOApplied = C(Ox)"),
         "NearCOApplied");
    test("show exposes the unchanged C-of-applied-O near miss",
         parse("show NearCOApplied"), "arity:0 C(Ox)");
    test("define closes named Albatrosses through duplicate optimization",
         parse("define FinalCOClosure = (C O)(S B T)"),
         "FinalCOClosure");
    test("show exposes the named-to-duplicate MA closure",
         parse("show FinalCOClosure"), "arity:0 MA");
    test("abstract duplicate pass feeds the final optimizer",
         parse("abstract ?x = BxB"),
         "?=NB");
    test("abstract steps traces duplicate normalization before first takeout",
         parse("abstract steps ?x = BxB"),
         "optimize: BxB -> NBx\n"
         "takeout x from NBx: NB\n"
         "?=NB");
    test("abstract repeatedly rescans duplicate-normalized takeout",
         parse("abstract ?xy = xxxy"),
         "?=WN");
    test("abstract steps traces each duplicate optimizer rescan",
         parse("abstract steps ?xy = xxxy"),
         "optimize: xxxy -> Nxxy\n"
         "optimize: Nxxy -> WNxy\n"
         "takeout y from WNxy: WNx\n"
         "takeout x from WNx: WN\n"
         "?=WN");
    test("abstract ministeps scans duplicates before first takeout",
         parse("abstract ministeps ?xy = xxxy"),
         "optimize: xxxy -> Nxxy\n"
         "optimize: Nxxy -> WNxy\n"
         "takeout y from WNxy: WNx\n"
         "takeout x from WNx: WN\n"
         "?=WN");
    test("abstract optimizes duplicate expressions before first takeout",
         parse("abstract ?x = abbx"),
         "?=Wab");
    test("abstract steps traces duplicate optimization before first takeout",
         parse("abstract steps ?x = abbx"),
         "optimize: abbx -> Wabx\n"
         "takeout x from Wabx: Wab\n"
         "?=Wab");
    test("abstract ministeps traces duplicate optimization before first takeout",
         parse("abstract ministeps ?x = abbx"),
         "optimize: abbx -> Wabx\n"
         "takeout x from Wabx: Wab\n"
         "?=Wab");
    test("abstract leaves named-bird optimization until after takeout",
         parse("abstract steps ?x = BCx"),
         "takeout x from BCx: BC\n"
         "optimize: BC -> C*\n"
         "?=C*");
    test("abstract preprocessing precedes initial duplicate optimization",
         parse("abstract steps ?x = Wabx"),
         "preprocess: Wabx -> abbx\n"
         "optimize: abbx -> Wabx\n"
         "takeout x from Wabx: Wab\n"
         "?=Wab");
    test("abstract initial duplicate optimization stops at its first repeat",
         parse("abstract steps ?x = NNMx"),
         "optimize: NNMx -> MNMx\n"
         "optimize: MNMx -> NMNx\n"
         "optimize: NMNx -> NNMx\n"
         "takeout x from NNMx: NNM\n"
         "optimize: NNM -> MNM\n"
         "optimize: MNM -> NMN\n"
         "optimize: NMN -> NNM\n"
         "?=NNM");
    test("abstract repeatedly rescans nested duplicate expressions",
         parse("abstract steps ?y = p(xxxy)"),
         "optimize: p(xxxy) -> p(Nxxy)\n"
         "optimize: p(Nxxy) -> p(WNxy)\n"
         "takeout y from p(WNxy): Bp(WNx)\n"
         "?=Bp(WNx)");
    test("abstract applies the H optimizer before first takeout",
         parse("abstract ?x = C*Bxx"),
         "?=HB");
    test("abstract steps trace H optimization and its next rescan",
         parse("abstract steps ?x = C*Hxx"),
         "optimize: C*Hxx -> W(C*H)x\n"
         "optimize: W(C*H)x -> HHx\n"
         "optimize: HHx -> MHx\n"
         "takeout x from MHx: MH\n"
         "?=MH");
    test("abstract H optimizer captures arbitrary compound expressions",
         parse("abstract ?x = C*(a(bc))xx"),
         "?=H(a(bc))");
    test("abstract steps stops after tracing a repeated cycle state",
         parse("abstract steps ?y = NNMy"),
         "optimize: NNMy -> MNMy\n"
         "optimize: MNMy -> NMNy\n"
         "optimize: NMNy -> NNMy\n"
         "takeout y from NNMy: NNM\n"
         "optimize: NNM -> MNM\n"
         "optimize: MNM -> NMN\n"
         "optimize: NMN -> NNM\n"
         "?=NNM");
    test("abstract duplicate pass feeds the next reverse takeout",
         parse("abstract ?xy = ya(ya)"),
         "?=K(C(WS)a)");
    test("abstract steps uses normalized output for the next takeout",
         parse("abstract steps ?xy = ya(ya)"),
         "optimize: ya(ya) -> Syya\n"
         "optimize: Syya -> WSya\n"
         "takeout y from WSya: C(WS)a\n"
         "takeout x from C(WS)a: K(C(WS)a)\n"
         "?=K(C(WS)a)");
    test("abstract ministeps keeps stages raw until aggregate normalization",
         parse("abstract ministeps ?xy = ya(ya)"),
         "optimize: ya(ya) -> Syya\n"
         "optimize: Syya -> WSya\n"
         "takeout y from WSya: C[takeout y from WSy]a\n"
         "= C(WS)a\n"
         "takeout x from C(WS)a: K(C(WS)a)\n"
         "?=K(C(WS)a)");
    test("abstract steps omits unchanged duplicate normalization",
         parse("abstract steps ?x = xa"),
         "takeout x from xa: Ta\n"
         "?=Ta");
    test("abstract ministeps omits unchanged duplicate normalization",
         parse("abstract ministeps ?x = xa"),
         "takeout x from xa: Ta\n"
         "?=Ta");
    test("abstract steps shows the expression before every takeout",
         parse("abstract steps ?xy = y"),
         "takeout y from y: I\n"
         "takeout x from I: KI\n"
         "?=KI");
    test("abstract ministeps traces recursive takeout in place",
         parse("abstract ministeps ?xy = y(xy)"),
         "optimize: y(xy) -> Oxy\n"
         "takeout y from Oxy: Ox\n"
         "takeout x from Ox: O\n"
         "?=O");
    test("abstract ministeps traces nested recursive takeout in place",
         parse("abstract ministeps ?x = a(b(xc))"),
         "takeout x from a(b(xc)): Ba[takeout x from b(xc)]\n"
         "= Ba(Bb[takeout x from xc])\n"
         "= Ba(Bb(Tc))\n"
         "?=Ba(Bb(Tc))");
    test("abstract ministeps traces both recursive branches in place",
         parse("abstract ministeps ?x = (xa)(xb)"),
         "takeout x from xa(xb): "
         "S[takeout x from xa][takeout x from xb]\n"
         "= S(Ta)[takeout x from xb]\n"
         "= S(Ta)(Tb)\n"
         "?=S(Ta)(Tb)");
    test("abstract steps traces changed preprocessing",
         parse("abstract steps ?xyz = C(CB)xyz"),
         "preprocess: C(CB)xyz -> x(yz)\n"
         "takeout z from x(yz): Bxy\n"
         "takeout y from Bxy: Bx\n"
         "takeout x from Bx: B\n"
         "?=B");
    test("abstract steps traces each chained optimizer substitution",
         parse("abstract steps ?x = C(Tx)"),
         "takeout x from C(Tx): BCT\n"
         "optimize: BC -> C*\n"
         "optimize: C*T -> V\n"
         "?=V");
    test("abstract rescans duplicate expressions after final optimization",
         parse("abstract ?xy = yxxx"),
         "?=H(ZBWT)");
    test("abstract steps traces the post-final duplicate rescan",
         parse("abstract steps ?xy = yxxx"),
         "optimize: yxxx -> W(yx)x\n"
         "takeout y from W(yx)x: C(BW(Tx))x\n"
         "takeout x from C(BW(Tx))x: W(BC(B(BW)T))\n"
         "optimize: W(BC(B(BW)T)) -> W(BC(ZBWT))\n"
         "optimize: BC -> C*\n"
         "optimize: W(C*(ZBWT)) -> H(ZBWT)\n"
         "?=H(ZBWT)");
    test("abstract ministeps traces the post-final duplicate rescan",
         parse("abstract ministeps ?xy = yxxx"),
         "optimize: yxxx -> W(yx)x\n"
         "takeout y from W(yx)x: C[takeout y from W(yx)]x\n"
         "= C(BW[takeout y from yx])x\n"
         "= C(BW(Tx))x\n"
         "takeout x from C(BW(Tx))x: "
         "W[takeout x from C(BW(Tx))]\n"
         "= W(BC[takeout x from BW(Tx)])\n"
         "= W(BC(B(BW)[takeout x from Tx]))\n"
         "= W(BC(B(BW)T))\n"
         "optimize: W(BC(B(BW)T)) -> W(BC(ZBWT))\n"
         "optimize: BC -> C*\n"
         "optimize: W(C*(ZBWT)) -> H(ZBWT)\n"
         "?=H(ZBWT)");
    test("abstract alternates final named and structural optimization",
         parse("abstract steps ?x = C(C*T)Vx"),
         "preprocess: C(C*T)Vx -> C*TxV\n"
         "takeout x from C*TxV: C(C*T)V\n"
         "optimize: C*T -> V\n"
         "optimize: CVV -> WCV\n"
         "optimize: WC -> N\n"
         "?=NV");
    test("abstract final-phase cycle trace stops at its first repeat",
         parse("abstract steps ?x = WCNMx"),
         "takeout x from WCNMx: WCNM\n"
         "optimize: WC -> N\n"
         "optimize: NNM -> MNM\n"
         "optimize: MNM -> NMN\n"
         "optimize: NMN -> NNM\n"
         "?=NNM");

    test("define applies duplicate normalization at its symbol boundary",
         parse("define DupRuleW x = xa(xa)"), "DupRuleW");
    test("show exposes duplicate-normalized define body",
         parse("show DupRuleW"), "arity:1 C(WS)a");
    test("duplicate-normalized define preserves its applied behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("DupRuleW b"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "ba(ba)");
    test("define optimizes a zero-symbol body before any takeout",
         parse("define InitialDupZero = abb"), "InitialDupZero");
    test("show exposes the initial duplicate-normalized zero-symbol body",
         parse("show InitialDupZero"), "arity:0 Wab");
    test("initial duplicate-normalized zero-symbol define preserves behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("InitialDupZero c"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "abbc");
    test("define uses its initial duplicate result for first takeout",
         parse("define InitDupSym x = abbx"),
         "InitDupSym");
    test("show exposes the initial duplicate-normalized symbol body",
         parse("show InitDupSym"), "arity:1 Wab");
    test("initial duplicate-normalized symbol define preserves behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("InitDupSym c"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "abbc");
    test("define applies duplicate normalization at recursive-name takeout",
         parse("define DupRec = DupRec a(DupRec a)"), "DupRec");
    test("show exposes duplicate-normalized recursive-name body",
         parse("show DupRec"), "arity:0 Y(C(WS)a)");
    test("define repeatedly rescans duplicate-normalized takeout",
         parse("define DupRescan xy = xxxy"), "DupRescan");
    test("show exposes repeatedly normalized define body",
         parse("show DupRescan"), "arity:2 WN");
    test("repeatedly normalized define preserves its applied behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("DupRescan a b"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "aaab");
    test("define rescans duplicate expressions after final optimization",
         parse("define FinalDupRescan xy = yxxx"),
         "FinalDupRescan");
    test("show exposes the post-final duplicate-rescanned define body",
         parse("show FinalDupRescan"),
         "arity:2 H(ZBWT)");
    test("post-final duplicate-rescanned define preserves behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("FinalDupRescan a b"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "baaa");
    test("define applies the H optimizer at its symbol boundary",
         parse("define DupH x = C*Bxx"), "DupH");
    test("show exposes the H-optimized define body",
         parse("show DupH"), "arity:1 HB");

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
    test("set creates a preprocessing dependency snapshot",
         parse("set CycleOld = 1 K"), "CycleOld");
    test("set captures the preprocessing dependency snapshot",
         parse("set CycleSnap = 0 CycleOld"), "CycleSnap");
    test("set replaces the dependency for future parsing",
         parse("set CycleOld = 1 I"), "CycleOld");
    test("define preprocessing preserves captured snapshots",
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
    test("show applies duplicate takeout within the constant body",
         parse("show PrepOmega"), "arity:1 LKM");
    test("define abstracts symbols from right to left",
         parse("define DefE xyz = exp"), "DefE");
    test("basis step exposes right-to-left abstraction",
         single_step(parse("DefE a b c"), true),
         "B(ZK)(Cep)abc");
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
    test("define gives duplicate takeout precedence over BB to D",
         parse("define DefD x = BBx"), "DefD");
    test("show exposes the Mockingbird Bluebird result",
         parse("show DefD"), "arity:1 MB");
    test("duplicate-pass Dove equivalent preserves behavior",
         [] {
             auto normalized = combdsl::detail::normalize_for_compare(
                 parse("DefD a b c d"));
             if (normalized) {
                 normalized->print_to(std::cout);
             }
         },
         "ab(cd)");
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
         parse("show DefKCstar"), "arity:1 KC*");
    test("define gives duplicate takeout precedence over B C*",
         parse("define OptCstarstar xyzwv = xyzvw"), "OptCstarstar");
    test("show exposes the Zazu Bluebird Cardinal result",
         parse("show OptCstarstar"), "arity:5 ZBC");
    test("define recursively optimizes nested B C*",
         parse("define DefKCstarstar x = B C*"), "DefKCstarstar");
    test("show exposes nested Cardinal star star optimization",
         parse("show DefKCstarstar"), "arity:1 KC**");
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
    test("define gives duplicate takeout precedence over B(QT)B",
         parse("define OptQ1 xyz = x(zy)"), "OptQ1");
    test("show exposes the Nightingale B QT result",
         parse("show OptQ1"), "arity:3 NB(QT)");
    test("duplicate-pass Quixotic equivalent preserves behavior",
         single_step(single_step(parse("OptQ1 a b c"))), "a(cb)");
    test("define applies duplicate takeout within constant B(QT)B",
         parse("define DefKQ1 x = B(QT)B"), "DefKQ1");
    test("show exposes the constant Nightingale B QT result",
         parse("show DefKQ1"), "arity:1 K(NB(QT))");
    test("define gives duplicate takeout precedence over BDD",
         parse("define OptE xyzwv = xy(zwv)"), "OptE");
    test("show exposes the Owl Zazu Bluebird result",
         parse("show OptE"), "arity:5 OZB");
    test("define applies duplicate takeout within constant BDD",
         parse("define DefKE x = BDD"), "DefKE");
    test("show exposes the constant Zazu Dove result",
         parse("show DefKE"), "arity:1 K(ZD)");
    test("define pre-optimizes y(xxy) before takeout",
         parse("define OptU xy = y(xxy)"), "OptU");
    test("show exposes the pre-takeout optimized U result",
         parse("show OptU"), "arity:2 U");
    test("define recursively optimizes nested BOM",
         parse("define DefKU x = BOM"), "DefKU");
    test("show exposes nested Turing bird optimization",
         parse("show DefKU"), "arity:1 KU");
    test("define recursively optimizes nested BT",
         parse("define DefKQ3 x = BT"), "DefKQ3");
    test("show exposes nested Quirky optimization",
         parse("show DefKQ3"), "arity:1 KQ3");
    test("define recursively optimizes nested BW",
         parse("define DefKWstar x = BW"), "DefKWstar");
    test("show exposes nested Warbler star optimization",
         parse("show DefKWstar"), "arity:1 KW*");
    test("define preserves nested QTC",
         parse("define DefKQV x = QTC"), "DefKQV");
    test("show exposes nested unoptimized QTC",
         parse("show DefKQV"), "arity:1 K(QTC)");
    test("define pre-optimizes BB before constant takeout",
         parse("define DefKD x = BB"), "DefKD");
    test("show exposes pre-optimized BB under K",
         parse("show DefKD"), "arity:1 K(MB)");
    test("define produces R without the CC optimizer",
         parse("define DefR xyz = yzx"), "DefR");
    test("show exposes the takeout-produced Robin",
         parse("show DefR"), "arity:3 R");
    test("takeout-produced Robin preserves behavior",
         single_step(single_step(parse("DefR a b c"))), "bca");
    test("define pre-optimizes CC before constant takeout",
         parse("define DefKR x = CC"), "DefKR");
    test("show exposes pre-optimized CC under K",
         parse("show DefKR"), "arity:1 K(MC)");
    test("define pre-optimizes x(yx) before takeout",
         parse("define DefA xy = x(yx)"), "DefA");
    test("show exposes the final Albatross optimization",
         parse("show DefA"), "arity:2 A");
    test("Albatross-optimized DefA preserves behavior",
         single_step(single_step(parse("DefA a b"))), "a(ba)");
    test("define applies the Albatross optimizer without takeout",
         parse("define DefAZero = CO"), "DefAZero");
    test("show exposes the zero-symbol Albatross optimization",
         parse("show DefAZero"), "arity:0 A");
    test("define recursively optimizes nested SBT",
         parse("define DefKA x = SBT"), "DefKA");
    test("show exposes nested Albatross optimization",
         parse("show DefKA"), "arity:1 KA");
    test("define recursively optimizes nested SR",
         parse("define DefKH x = SR"), "DefKH");
    test("show exposes nested H optimization",
         parse("show DefKH"), "arity:1 KH");
    test("define gives duplicate takeout precedence over DC",
         parse("define OptG xyzw = xw(yz)"), "OptG");
    test("show exposes the Mockingbird Bluebird Cardinal result",
         parse("show OptG"), "arity:4 MBC");
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
    test("define pre-optimizes yxx before takeout",
         parse("define OptW1 xy = yxx"), "OptW1");
    test("show exposes the pre-takeout optimized OptW1 body",
         parse("show OptW1"), "arity:2 CW");
    test("define recursively optimizes nested WV",
         parse("define DefKW1 x = WV"), "DefKW1");
    test("show exposes nested Converse warbler optimization",
         parse("show DefKW1"), "arity:1 KW1");
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
    test("define gives duplicate takeout precedence over B W*",
         parse("define OptWstarstar xyzw = xyzww"),
         "OptWstarstar");
    test("show exposes the Zazu Bluebird Warbler result",
         parse("show OptWstarstar"), "arity:4 ZBW");
    test("define recursively optimizes nested BW*",
         parse("define DefKWss x = B W*"), "DefKWss");
    test("show exposes nested Warbler star star optimization",
         parse("show DefKWss"), "arity:1 KW**");
    test("define gives duplicate takeout precedence over the Jay pattern",
         parse("define OptJ xyzw = xy(xwz)"), "OptJ");
    test("show exposes the duplicate-normalized Jay equivalent",
         parse("show OptJ"), "arity:4 S(MB(BQC))(MB)");
    test("duplicate-pass Jay equivalent preserves behavior",
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
    test("define applies duplicate takeout within C(CB)",
         parse("define DefChainB x = C(CB)x"), "DefChainB");
    test("show exposes the resulting Zazu Cardinal Bluebird",
         parse("show DefChainB"), "arity:1 ZCB");
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
    test("define accepts an integer value body",
         parse("define NumericDefine = 42"), "NumericDefine");
    test("show preserves the integer value in a define body",
         parse("show NumericDefine"), "arity:0 42");
    auto const definitions_before_rejected_numeric_defines = set_list();
    constexpr std::string_view negative_numeric_define =
        "define NegativeNumeric x = -25";
    test_parse_failure(
        "define rejects a negative numeric body",
        negative_numeric_define,
        negative_numeric_define.find('-'));
    constexpr std::string_view floating_numeric_define =
        "define FloatingNumeric x = 2.5";
    test_parse_failure(
        "define rejects a floating numeric body",
        floating_numeric_define,
        floating_numeric_define.find('2'));
    constexpr std::string_view exponent_numeric_define =
        "define ExponentNumeric = 1e3";
    test_parse_failure(
        "define rejects an exponent numeric body",
        exponent_numeric_define,
        exponent_numeric_define.find('1'));
    test("rejected numeric defines leave definitions unchanged",
         [&] {
             std::cout <<
                 (set_list() ==
                          definitions_before_rejected_numeric_defines
                      ? "unchanged"
                      : "changed");
         },
         "unchanged");
    test("an internal uppercase letter makes a full name zero-arity",
         parse("define ZeroDef = x"), "ZeroDef");
    test("show exposes a zero-symbol define by its full name",
         parse("show ZeroDef"), "arity:0 x");
    test("a zero-symbol define expands with trailing arguments",
         single_step(parse("ZeroDef y")), "xy");
    test("a zero-symbol define has a replayable canonical signature",
         [] {
             auto const definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define ZeroDef = x");
    test("a spaced zero-symbol canonical define can be reparsed",
         parse("define ZeroDef = x"), "ZeroDef");
    test("zero-symbol define inspection identifies a replacement",
         [] {
             auto inspected = combdsl::detail::parse_input(
                 "define ZeroDef = K",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << inspected.replaced_definition;
         },
         "ZeroDef=0 x");
    test("a zero-symbol define can be redefined",
         parse("define ZeroDef = K"), "ZeroDef");
    test("show exposes a zero-symbol define replacement",
         parse("show ZeroDef"), "arity:0 K");
    test("a replaced zero-symbol define expands with arguments",
         single_step(parse("ZeroDef x y")), "Kxy");
    test("zero-symbol define history keeps canonical revisions",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find("define ZeroDef = x") !=
                         std::string::npos)
                 << (definitions.find("define ZeroDef = K") !=
                         std::string::npos);
         },
         "11");
    constexpr std::string_view lowercase_name_error =
        "combdsl::basis names cannot begin with a lowercase ASCII letter";
    test_parse_failure(
        "set rejects a lowercase-leading name",
        "set lower = 1 I", 4, lowercase_name_error);
    test_parse_failure(
        "define rejects a lowercase-leading name",
        "define lower x=x", 7, lowercase_name_error);
    test_parse_failure(
        "define rejects a lowercase one-letter name",
        "define f x=x", 7, lowercase_name_error);
    test_parse_failure(
        "remove rejects a lowercase-leading name",
        "remove lower", 7, lowercase_name_error);
    test_parse_failure(
        "revisions rejects a lowercase-leading name",
        "revisions lower", 10, lowercase_name_error);
    test("lowercase symbols remain usable after rejected registrations",
         parse("f lower"), "flower");
    test("a lowercase suffix stays symbols despite whitespace before equals",
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
         parse("define DRaw x = \"x\"x"), "DRaw");
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
    test("D-derived Eagle basis step keeps its registered body",
         single_step(parse("Eabcde"), true), "BDDabcde");
    test("show exposes the pre-defined Jay",
         parse("show J"), "arity:4 C**(HE)");
    test("show exposes the pre-defined Dove compact body",
         parse("show D"), "arity:4 S(KB)");
    test("Dove basis step exposes its compact body",
         single_step(parse("Dabcd"), true), "S(KB)abcd");
    test("show exposes the D-derived Goldfinch",
         parse("show G"), "arity:4 DC");
    test("D-derived Goldfinch basis step keeps its registered body",
         single_step(parse("Gabcd"), true), "DCabcd");
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
         parse("show C**"), "arity:5 BC*");
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
         parse("show W**"), "arity:4 BW*");
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
         parse("define WordRec x = \"WordRec\""), "WordRec");
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
    test("set accepts a numeric body after an explicit zero arity",
         parse("set NumericSet = 0 42"), "NumericSet");
    test("show preserves an explicit-arity numeric set body",
         parse("show NumericSet"), "arity:0 42");
    test("set accepts a parenthesized integer body without an arity",
         parse("set NumericParenSet = (15)"), "NumericParenSet");
    test("show preserves a parenthesized integer set body",
         parse("show NumericParenSet"), "arity:0 15");
    test("equivalent integer spellings leave a set unchanged",
         [] {
             auto const before = set_list();
             static_cast<void>(parse("set NumericSet = 0 00042"));
             std::cout << (set_list() == before
                 ? "unchanged"
                 : "changed");
         },
         "unchanged");
    test("set accepts a lone integer body",
         parse("set NumericTyped = 1"), "NumericTyped");
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
    test("usedby recognizes a captured earlier snapshot by name",
         parse("usedby HistB"), "HistB directly uses: HistA");
    test("dependson recognizes a captured earlier snapshot by name",
         parse("dependson HistA"),
         "HistA is directly depended on by: HistB");
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
         "HistB=0 HistA@1");
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
    test("set registers another direct-circle test basis",
         parse("set Self = 1 I"), "Self");
    test("another frozen self-name revision is accepted",
         parse("set Self = 1 Self"), "Self");
    test("a frozen self-name creates no live self-dependency",
         parse("dependson Self"),
         "Self is not directly depended on by anything");
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
    test("a C++ basis cannot take a removed user name",
         [] {
             static_cast<void>(parse("set RemovedLateCpp=I"));
             static_cast<void>(parse("remove RemovedLateCpp"));
             try {
                 static_cast<void>(basis("RemovedLateCpp", 0, K));
                 std::cout << "accepted\n";
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what() << '\n';
             }
             parse("revisions RemovedLateCpp").print_to(std::cout);
         },
         "combdsl::basis name is already user-defined: RemovedLateCpp\n"
         "RemovedLateCpp arity:0 I [captured] [removed]");
    test("parse left association reduction", single_step(parse("KIxy")),
         "Iy");
    test("parse parentheses override association",
         single_step(parse("K(Ix)y")), "Ix");
    test("parse directly quoted word", parse(R"("word")"), "word");
    test("parse directly quoted word example",
         parse(R"(x "word" y "mid\\dle" z)"),
         "x word y mid\\dle z");
    test("parse direct C-style quote and backslash",
         parse(R"("fo\"\\o")"), R"(fo"\o)");
    test("parse direct quote escape",
         parse(R"("fo\"o")"), R"(fo"o)");
    test("parse direct backslash escape",
         parse(R"("fo\\o")"), R"(fo\o)");
    test("parse direct doubled trailing backslash",
         parse(R"("tail\\")"), R"(tail\)");
    auto test_backslash_quote_run = [](
        std::string_view title,
        std::size_t backslashes,
        std::string_view expected,
        bool remains_inside_word) {
        std::string source = "\"a";
        source.append(backslashes, '\\');
        source.push_back('"');
        if (remains_inside_word) {
            source += "b\"";
        }
        test(
            title,
            [source = std::move(source),
             expected = std::string(expected)] {
                auto const parsed = parse(source);
                std::cout <<
                    combdsl::detail::same_parser_definition_expression(
                        parsed, quote(expected));
            },
            "1");
    };
    test_backslash_quote_run(
        "one backslash and quote preserves a quote", 1, "a\"b", true);
    test_backslash_quote_run(
        "two backslashes and quote preserve a slash then close",
        2, "a\\", false);
    test_backslash_quote_run(
        "three backslashes and quote preserve a slash and quote",
        3, "a\\\"b", true);
    test_backslash_quote_run(
        "four backslashes and quote preserve two slashes then close",
        4, "a\\\\", false);
    test_backslash_quote_run(
        "five backslashes and quote preserve two slashes and quote",
        5, "a\\\\\"b", true);
    test("a slash-leading mixed word prints and reparses",
         [] {
             auto const expression =
                 quote(std::string_view(R"(\fo"\bar\)"));
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     expression, reparsed)
                 << ' ' << rendered.str();
         },
         R"(1 "\\fo\"\\bar\\")");
    test("parse adjacent directly quoted words",
         parse(R"("left""right")"), "left right");
    test("parse directly quoted word inside parentheses",
         parse(R"(x("word"y))"), "x(word y)");
    test("parse quoted word preserves parser characters",
         parse("x \"a b()\" y"), "x a b() y");
    const auto unregistered_literal_backslash =
        parse(R"(\)");
    const auto unregistered_literal_backslash_application =
        parse(R"(\foo)");
    test("parse direct backslash as an operand",
         unregistered_literal_backslash, R"("\\")");
    test("parse direct backslash between symbols",
         parse(R"(x\y)"), R"(x"\\"y)");
    test("unregistered direct backslash remains an operand",
         single_step(parse(R"(\x)")), R"("\\"x)");
    test("an unregistered direct backslash remains a raw operand",
         [] {
             auto const parsed = parse(R"(\)");
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     parsed, quote(std::string_view(R"(\)")));
         },
         "1");
    test("the quoted direct backslash spells the same raw operand",
         [] {
             auto const parsed = parse(R"("\\")");
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     parsed, quote(std::string_view(R"(\)")));
         },
         "1");
    test("a raw value beginning with backslash prints quoted",
         quote(std::string_view(R"(\foo)")), R"("\\foo")");
    test("a printed raw backslash value round trips directly",
         [&unregistered_literal_backslash] {
             std::ostringstream rendered;
             unregistered_literal_backslash.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     unregistered_literal_backslash, reparsed)
                 << ' ' << rendered.str();
         },
         R"(1 "\\")");
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
    const auto tail_plus_basis = basis("Tail+", 1, I);
    const auto plus_basis = basis("+", 1, I);
    test("registered plus basis takes precedence over numeric syntax",
         parse("+4"), "+ 4");
    test("registered plus basis applies to an integer",
         single_step(parse("+4")), "4");
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
    const auto plus_integer_name_basis = basis("+4", 1, I);
    const auto minus_integer_name_basis = basis("-4", 1, I);
    const auto decimal_name_basis = basis("4.0", 1, I);
    const auto exponent_name_basis = basis("4e2", 1, I);
    const auto mixed_numeric_name_basis = basis("4x", 1, I);
    const auto unicode_digit_name_basis =
        basis("\xD9\xA7", 1, I);
    test("the C++ basis API retains a plus-prefixed numeric name",
         plus_integer_name_basis, "+4");
    test("the C++ basis API retains a minus-prefixed numeric name",
         minus_integer_name_basis, "-4");
    test("the C++ basis API retains a decimal-looking name",
         decimal_name_basis, "4.0");
    test("the C++ basis API retains an exponent-looking name",
         exponent_name_basis, "4e2");
    test("the C++ basis API retains a mixed alphanumeric name",
         mixed_numeric_name_basis, "4x");
    test("the C++ basis API retains a non-ASCII digit name",
         unicode_digit_name_basis, "\xD9\xA7");
    test("an exact plus-prefixed basis name remains usable",
         single_step(parse("+4 x")), "x");
    test("an exact minus-prefixed basis name remains usable",
         single_step(parse("-4 x")), "x");
    test("an exact decimal-looking basis name remains usable",
         single_step(parse("4.0 x")), "x");
    test("an exact exponent-looking basis name remains usable",
         single_step(parse("4e2 x")), "x");
    test("an exact mixed alphanumeric basis name remains usable",
         single_step(parse("4x x")), "x");
    test("an exact non-ASCII digit basis name remains usable",
         single_step(parse("\xD9\xA7 x")), "x");
    test("parse escaped backslash inside quoted word",
         parse(R"("a\\b")"), "a\\b");
    test("parse UTF-8 quoted word",
         parse("\"\xE2\x97\x8F\"x"),
         "\xE2\x97\x8F x");
    {
        char const source[] = {'"', 'a', '\\', '"', 'b', '"'};
        test("parse quoted word preserves escaped quote",
             parse(std::string_view(source, sizeof source)), "a\"b");
    }
    {
        char const source[] = {'"', 'a', '\0', 'b', '"'};
        test("parse quoted word preserves embedded null",
             parse(std::string_view(source, sizeof source)),
             std::string_view("a\0b", 3));
    }
    test("parsed quoted word owns its contents",
         [] {
             std::string source = "x\"word\"";
             auto expression = parse(source);
             source.assign("changed");
             expression();
         },
         "x word");
    test("single step parsed I quoted word",
         single_step(parse(R"(I"word")")), "word");
    test("single step parsed K quoted word",
         single_step(parse(R"(K"left"x)")), "left");
    test("single step parsed S quoted words",
         single_step(parse(R"(S"left""right""value")")),
         "left value(right value)");
    test("single step parsed Y quoted word",
         single_step(parse(R"(Y"word")")),
         "word <deferred Y(word)>");
    test("parse quoted word beside undersaturated basis",
         parse(R"(T"word")"), "T word");
    test("quoted basis spelling remains a word",
         single_step(parse(R"("M"x)")), "Mx");

    constexpr std::string_view expected_registered_basis_names[] = {
        "M", "W", "B", "O", "T", "U", "N", "R", "C", "C*", "C**",
        "W*", "W**", "Q", "Q1", "Q3", "V", "D", "L", "W1", "Z", "A", "E",
        "F", "G", "H", "J", "Cstar", "Vstar", "V4", "G2", "G1",
        "BazTest", "Hprime", "H1",
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
    test("quoted word opener delimits Cstar",
         parse(R"(Cstar"word")"), "Cstar word");
    test_parse_failure("unseparated Cstar is an unknown operand",
                       "Cstarx", 0);
    test("parse separated Hprime", single_step(parse("Hprime x y")),
         "Hprime xy");
    test("parse G2 exact match", single_step(parse("G2")), "G2");
    test("parse G2 without a trailing delimiter", parse("G2x"), "G2x");
    test("parse separated V4", single_step(parse("V4 x y z")), "V4xyz");
    test("parse V4 without a trailing delimiter",
         parse("V4x"), "V4x");
    test("seven digits remain a numeric literal",
         parse("1234567"), "1234567");
    test("a leading-zero spelling remains a numeric literal",
         parse("0000000"), "0");
    test("fifteen digits remain a numeric literal",
         parse("123456789012345"), "123456789012345");
    test("maximum-length digit-ending basis prints compactly",
         parse("A12345678901234x"), "A12345678901234x");
    test("maximum-length digit-ending basis needs no delimiter",
         single_step(parse("A12345678901234x")), "x");
    static_cast<void>(basis("Long1", 1, K));
    static_cast<void>(basis("Long12", 1, I));
    test("longest eligible basis prefix wins",
         single_step(parse("Long12x")), "x");
    static_cast<void>(basis("Exact1", 1, K));
    static_cast<void>(basis("Exact1x", 0, I));
    test("exact longer basis wins over an eligible prefix",
         single_step(parse("Exact1x")), "I");
    test("basis automatically registers seven-character name",
         single_step(parse("123456x x")), "x");
    test("basis automatically registers fifteen-character name",
         single_step(parse("A12345678901234 x")), "x");
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
    test("spaced lowercase symbols remain separate operands",
         parse("foo x"), "foox");
    test("unseparated lowercase run remains symbols",
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
    test_parse_failure(
        "parse integer above supported range",
        "9223372036854775808", 0);
    test_parse_failure(
        "parse rejects an out-of-range negative spelling",
        "-9223372036854775809", 0);
    test_parse_failure(
        "parse rejects an exponent even above floating range", "1e309", 0);
    test_parse_failure(
        "parse rejects a negative exponent spelling", "-1e309", 0);
    test_parse_failure("parse lone minus sign", "-", 0);
    test_parse_failure("parse lone decimal point", ".", 0);
    test_parse_failure("parse repeated sign", "--1", 0);
    test_parse_failure("parse exponent without digits", "1e+", 0);
    test_parse_failure("parse negative exponent without digits", "1e-", 0);
    test_parse_failure(
        "parse does not split an exponent before a symbol", "1e2x", 0);
    test_parse_failure(
        "parse rejects a negative integer after a symbol", "x-1", 1);
    test_parse_failure(
        "parse rejects a decimal after a symbol", "x1.5", 1);
    test_parse_failure("parse repeated decimal point", "1..2", 0);
    test_parse_failure("parse multiple decimal points", "1.2.3", 0);
    test_parse_failure(
        "parse punctuation symbol", "@", 0, "unknown operand");
    test_parse_failure("parse UTF-8 symbol", "\xCE\xBB", 0);
    test_parse_failure("parse quoted word opener only", "\"", 1);
    test_parse_failure("parse empty quoted word", "\"\"", 1);
    test_parse_failure(
        "parse unterminated quoted word", "\"word", 5);
    test_parse_failure(
        "parse unterminated quoted word after atom", "x\"word", 6);
    test_parse_failure(
        "parse invalid atom after quoted word", "\"word\"@", 6);
    test_parse_failure(
        "parse invalid escape inside quoted word", R"("a\q")", 2);
    test_parse_failure(
        "parse escaped quote still requires a closing delimiter",
        R"("a\")", 4);
    {
        char const source[] = {'"', 'a', '\\'};
        test_parse_failure(
            "parse dangling backslash inside quoted word",
            std::string_view(source, sizeof source), 3);
    }
    for (unsigned int byte = 0; byte <= 255; ++byte) {
        if (byte == static_cast<unsigned int>('\\') ||
            byte == static_cast<unsigned int>('"')) {
            continue;
        }
        std::string source = "\"a\\";
        source.push_back(static_cast<char>(byte));
        source += "b\"";
        auto title = std::string(
            "parse rejects non-C backslash escape byte ");
        title += std::to_string(byte);
        test_parse_failure(title, source, 2);
    }
    test_parse_failure(
        "input_escape output is no longer direct parser input",
        input_escape(R"("word")"), 8);
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
        "abstract requires a question-mark marker", "abstract", 8,
        "expected '?'");
    test_parse_failure(
        "abstract steps requires a question-mark marker",
        "abstract steps", 14, "expected '?'");
    test_parse_failure(
        "abstract ministeps requires a question-mark marker",
        "abstract ministeps", 18, "expected '?'");
    test_parse_failure(
        "abstract steps rejects a second trace option",
        "abstract steps ministeps ?x = x", 15, "expected '?'");
    test_parse_failure(
        "abstract ministeps rejects a second trace option",
        "abstract ministeps steps ?x = x", 19, "expected '?'");
    test_parse_failure(
        "abstract requires symbols after its marker", "abstract ?", 10,
        "expected at least one symbol");
    test_parse_failure(
        "abstract does not allow whitespace after its marker",
        "abstract ? x = x", 10, "expected at least one symbol");
    test_parse_failure(
        "abstract rejects a missing marker", "abstract x = x", 9,
        "expected '?'");
    constexpr std::string_view uppercase_abstract_symbol =
        "abstract ?xY = x";
    test_parse_failure(
        "abstract rejects an uppercase symbol",
        uppercase_abstract_symbol,
        uppercase_abstract_symbol.find('Y'),
        "expected a lowercase symbol or '='");
    constexpr std::string_view abstract_without_equals =
        "abstract ?xy";
    test_parse_failure(
        "abstract requires an equals sign",
        abstract_without_equals,
        abstract_without_equals.size(), "expected '='");
    constexpr std::string_view abstract_without_expression =
        "abstract ?xy = ";
    test_parse_failure(
        "abstract requires an expression",
        abstract_without_expression,
        abstract_without_expression.size(),
        "expected an expression");
    test_parse_failure(
        "find requires a question-mark marker", "find", 4,
        "expected '?'");
    test_parse_failure(
        "find all requires a question-mark marker", "find all", 8,
        "expected '?'");
    test_parse_failure(
        "find among requires a bird", "find among", 10,
        "expected at least one bird name");
    test_parse_failure(
        "find among rejects an empty catalog",
        "find among ?x = x", 11,
        "expected at least one bird name");
    test_parse_failure(
        "find all among requires a bird", "find all among", 14,
        "expected at least one bird name");
    test_parse_failure(
        "find among rejects an unknown bird",
        "find among MissingBird ?x = x", 12,
        "issingBird is not a defined name");
    test_parse_failure(
        "find among requires whitespace after its keyword",
        "find amongA ?x = x", 10,
        "expected whitespace after 'among'");
    test_parse_failure(
        "find among requires whitespace before its marker",
        "find among A?x = x", 12,
        "?x is not a defined name");
    test_parse_failure(
        "find among preserves an exact overflowing revision diagnostic",
        "find among A@999999999999999999999999999999 ?x = x",
        13, "basis version is out of range");
    test_parse_failure(
        "find among treats an internal overflowing revision as a suffix",
        "find among A@999999999999999999999999999999K ?x = x",
        12,
        "@999999999999999999999999999999K is not a defined name");
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
        "find maximum and among are mutually exclusive",
        "find 2 among A ?x = x", 7,
        "expected '?'");
    test_parse_failure(
        "find among does not accept a maximum after its birds",
        "find among A 2 ?x = x", 13,
        "2 is not a defined name");
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
    test("step limit parses a positive number",
         [] {
             auto const command =
                 combdsl::parse_step_limit_command(
                     "  step\tlimit 123  ");
             std::cout << command.has_value()
                       << command->enabled << ':' << command->limit;
         },
         "11:123");
    test("step limit parses off",
         [] {
             auto const command =
                 combdsl::parse_step_limit_command("step limit off");
             std::cout << command.has_value()
                       << command->enabled << ':' << command->limit;
         },
         "10:0");
    test("step limit rejects zero",
         [] {
             try {
                 static_cast<void>(
                     combdsl::parse_step_limit_command("step limit 0"));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "11:step limit must be greater than zero");
    test("step limit parser ignores another command prefix",
         [] {
             std::cout << !combdsl::parse_step_limit_command(
                 "stepwise limit 4");
         },
         "1");
    test("bare step requires the limit subcommand",
         [] {
             try {
                 static_cast<void>(
                     combdsl::parse_step_limit_command("step"));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "4:expected 'limit'");
    test("bare step limit requires an option",
         [] {
             try {
                 static_cast<void>(
                     combdsl::parse_step_limit_command("step limit"));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "10:expected 'off' or a number");
    test("step limit rejects a signed number",
         [] {
             try {
                 static_cast<void>(
                     combdsl::parse_step_limit_command("step limit -1"));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "11:expected 'off' or a number");
    test("step limit rejects trailing input",
         [] {
             try {
                 static_cast<void>(combdsl::parse_step_limit_command(
                     "step limit 4 extra"));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "13:unexpected input after step limit");
    test("step limit rejects size_t overflow",
         [] {
             auto source = std::string("step limit ");
             source += std::to_string(
                 std::numeric_limits<std::size_t>::max());
             source.push_back('0');
             try {
                 static_cast<void>(
                     combdsl::parse_step_limit_command(source));
             } catch (parse_error const& error) {
                 std::cout << error.position() << ':' << error.detail();
             }
         },
         "11:step limit is too large");
    constexpr std::string_view reserved_definition_names[] = {
        "abstract", "all", "among", "captured", "live", "limit",
        "step",
        "steps", "ministeps", "path", "between", "and", "set",
        "define", "show", "single", "key", "basis", "colorize",
        "about", "birds", "find", "help", "load", "remove", "save",
        "compare", "inspect", "revisions",
        "dependson", "depends-on", "depends", "on", "usedby",
        "used-by", "used", "by", "quit", "exit"};
    for (auto const name : reserved_definition_names) {
        auto const detail =
            std::string(name) + " is a reserved word";
        auto const set_source = name == "captured"
            ? std::string("set live captured = I")
            : name == "live"
                ? std::string("set captured live = I")
                : std::string("set ") + std::string(name) + " = I";
        auto const set_title =
            std::string("set rejects reserved word ") +
            std::string(name);
        test_parse_failure(
            set_title, set_source, set_source.rfind(name), detail);

        auto const define_source = name == "captured"
            ? std::string("define live captured x = x")
            : name == "live"
                ? std::string("define captured live x = x")
                : std::string("define ") + std::string(name) +
                    " x = x";
        auto const define_title =
            std::string("define rejects reserved word ") +
            std::string(name);
        test_parse_failure(
            define_title,
            define_source,
            define_source.rfind(name),
            detail);
    }
    constexpr std::string_view numeric_basis_name_error =
        "combdsl::basis names cannot be non-negative integer literals";
    auto const numeric_command_registry_before =
        combdsl::detail::registered_parser_lookup_snapshot();
    auto const numeric_command_history_before = set_list();
    test_parse_failure(
        "set rejects zero as a basis name",
        "set 0 = I", 4, numeric_basis_name_error);
    test_parse_failure(
        "set rejects a leading-zero basis name",
        "set 000 = I", 4, numeric_basis_name_error);
    test_parse_failure(
        "set rejects a fifteen-digit basis name",
        "set 123456789012345 = I", 4, numeric_basis_name_error);
    test_parse_failure(
        "define rejects zero as a basis name",
        "define 0 = I", 7, numeric_basis_name_error);
    test_parse_failure(
        "define rejects a leading-zero basis name",
        "define 000 x = x", 7, numeric_basis_name_error);
    test_parse_failure(
        "define rejects a fifteen-digit basis name",
        "define 123456789012345 x = x", 7,
        numeric_basis_name_error);
    test_parse_failure(
        "compact define rejects its numeric one-character name",
        "define 7x = x", 7, numeric_basis_name_error);
    test_parse_failure(
        "remove rejects a numeric-only basis name",
        "remove 0", 7, numeric_basis_name_error);
    test_parse_failure(
        "revisions rejects a numeric-only basis name",
        "revisions 000", 10, numeric_basis_name_error);
    test_parse_failure(
        "dependson rejects a numeric-only basis name",
        "dependson 123456789012345", 10,
        numeric_basis_name_error);
    test_parse_failure(
        "usedby rejects a numeric-only basis name",
        "usedby 0", 7, numeric_basis_name_error);
    test_parse_failure(
        "dependency paths reject a numeric-only first name",
        "usedby path 0 AllRoot", 12, numeric_basis_name_error);
    test_parse_failure(
        "show keeps numeric text as a non-mutating name lookup",
        "show 0", 5, "0 is not a defined name");
    test("numeric-name command failures roll back every registry change",
         [&] {
             auto const after =
                 combdsl::detail::registered_parser_lookup_snapshot();
             std::cout
                 << (after.bases.size() ==
                     numeric_command_registry_before.bases.size())
                 << (after.versions.size() ==
                     numeric_command_registry_before.versions.size())
                 << (after.live_bindings.size() ==
                     numeric_command_registry_before.live_bindings.size())
                 << !after.bases.contains("0")
                 << !after.bases.contains("000")
                 << !after.bases.contains("123456789012345")
                 << (set_list() == numeric_command_history_before);
         },
         "1111111");
    test("set retains a plus-prefixed numeric-looking name",
         parse("set +5 = 1 I"), "+5");
    test("set retains a minus-prefixed numeric-looking name",
         parse("set -5 = 1 I"), "-5");
    test("set retains a decimal-looking name",
         parse("set 5.0 = 1 I"), "5.0");
    test("set retains an exponent-looking name",
         parse("set 5e2 = 1 I"), "5e2");
    test("set retains a mixed alphanumeric name",
         parse("set 5x = 1 I"), "5x");
    test("a plus-prefixed numeric-looking name evaluates",
         single_step(parse("+5 x")), "x");
    test("a minus-prefixed numeric-looking name evaluates",
         single_step(parse("-5 x")), "x");
    test("a decimal-looking name evaluates",
         single_step(parse("5.0 x")), "x");
    test("an exponent-looking name evaluates",
         single_step(parse("5e2 x")), "x");
    test("a mixed alphanumeric name evaluates",
         single_step(parse("5x x")), "x");
    test("define retains a mixed name when symbols are separated",
         parse("define 7x y = y"), "7x");
    test("the separated mixed-name define evaluates",
         single_step(parse("7x z")), "z");
    test_parse_failure(
        "set requires a second equals after an equals name",
        "set = I", 6, "expected '='");
    test_parse_failure("set requires an equals sign", "set NoEq I", 9);
    test_parse_failure("set requires an expression", "set Empty = \t", 13);
    test("a lone integer after set equals is a numeric body",
         parse("set ArOnly = 2"), "ArOnly");
    test("show exposes the lone integer as an arity-zero body",
         parse("show ArOnly"), "arity:0 2");
    test("an integer glued to an expression begins a numeric body",
         parse("set Glued = 2I"), "Glued");
    test("show preserves a glued numeric application body",
         parse("show Glued"), "arity:0 2 I");
    auto const definitions_before_rejected_numeric_sets = set_list();
    constexpr std::string_view negative_set_body =
        "set NegAr = -1 I";
    test_parse_failure(
        "set rejects a negative value where an arity or body begins",
        negative_set_body,
        negative_set_body.find('-'));
    constexpr std::string_view floating_set_body =
        "set FracAr = 1.5 I";
    test_parse_failure(
        "set rejects a floating value where an arity or body begins",
        floating_set_body,
        floating_set_body.find('1'));
    test("rejected numeric sets leave definitions unchanged",
         [&] {
             std::cout <<
                 (set_list() == definitions_before_rejected_numeric_sets
                      ? "unchanged"
                      : "changed");
         },
         "unchanged");
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
        "define requires an assignment equals after an equals name",
        "define = x", 10, "expected '='");
    test("define accepts an empty symbol list before equals",
         parse("define NoArgs = x"), "NoArgs");
    test("show exposes the empty-symbol definition",
         parse("show NoArgs"), "arity:0 x");
    test("the empty-symbol definition has canonical save syntax",
         [] {
             auto const definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             std::cout << definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
         },
         "define NoArgs = x");
    constexpr std::string_view zero_symbol_define_without_expression =
        "define EmptyZero =";
    test_parse_failure(
        "zero-symbol define requires an expression",
        zero_symbol_define_without_expression,
        zero_symbol_define_without_expression.size(),
        "expected an expression");
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
        "define BadRaw \"x\" = x";
    test_parse_failure(
        "define rejects a symbolic-string parameter",
        quoted_define_symbol,
        quoted_define_symbol.find('"'));
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
    const auto leading_single_backslash_cpp_basis =
        basis(R"(\CppApi)", 1, I);
    const auto leading_double_backslash_cpp_basis =
        basis(R"(\\CppEscapedApi)", 1, I);
    test("the C++ basis API accepts a leading single backslash",
         leading_single_backslash_cpp_basis, R"(\CppApi)");
    test("a raw parser input resolves that exact C++ backslash name",
         single_step(parse(R"(\CppApi x)")), "x");
    test("the C++ basis API accepts a leading double backslash",
         leading_double_backslash_cpp_basis, R"(\\CppEscapedApi)");
    test("direct input resolves a doubled C++ backslash name",
         single_step(parse(R"(\\CppEscapedApi x)")),
         "x");
    test("basis name beginning with double quote rejected",
         [] {
             try {
                 static_cast<void>(basis("\"bad", 1, I));
             } catch (std::invalid_argument const&) {
                 std::cout << "invalid";
             }
         },
         "invalid");
    const auto interior_quote_cpp_basis = basis("Bad\"name", 1, I);
    const auto trailing_quote_cpp_basis = basis("Bad\"", 1, I);
    test("the C++ basis API retains an interior double quote",
         interior_quote_cpp_basis, "Bad\"name");
    test("the C++ basis API retains a trailing double quote",
         trailing_quote_cpp_basis, "Bad\"");
    test("an interior-quote C++ basis remains usable natively",
         single_step(quote(interior_quote_cpp_basis)(x)), "x");
    test_parse_failure(
        "set rejects a double quote inside its apparent basis name",
        R"(set Bad"Name = 1 I)", 7);
    test_parse_failure(
        "define rejects a double quote inside its apparent basis name",
        R"(define Bad"Name x = x)", 10);

    test("parse eval", [&] { parse_eval("K(Ix)y"); }, "x\n");
    test("parse eval displays an evaluated integer without brackets",
         [&] { parse_eval("I 42"); }, "42\n");
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
    test("parse eval displays abstract without evaluating it",
         [&] { parse_eval("abstract ?xyz = xz(yz)"); }, "?=S\n");
    test("parse and step displays an abstract trace once",
         [] {
             std::istringstream input;
             std::ostringstream output;
             parse_and_step(
                 "abstract steps ?xy = x(yx)", output, input);
             std::cout << output.str();
         },
         "optimize: x(yx) -> Oyx\n"
         "takeout y from Oyx: COx\n"
         "takeout x from COx: CO\n"
         "optimize: CO -> A\n"
         "?=A\n");
    test("parse and step displays an abstract ministeps trace once",
         [] {
             std::istringstream input;
             std::ostringstream output;
             parse_and_step(
                 "abstract ministeps ?xy = y(xy)", output, input);
             std::cout << output.str();
         },
         "optimize: y(xy) -> Oxy\n"
         "takeout y from Oxy: Ox\n"
         "takeout x from Ox: O\n"
         "?=O\n");
    test("parse and key step displays an abstract result without pausing",
         [] {
             std::istringstream input;
             std::ostringstream output;
             parse_and_key_step(
                 "abstract ?xyz = xz(yz)", output, input);
             std::cout << output.str();
         },
         "?=S\n");
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
    const auto duplicate_compound_a = quote(a)(quote(b)(c));
    const auto duplicate_compound_b = quote(d)(quote(e)(f));
    const auto duplicate_compound_a1 = quote(g)(quote(h)(i));
    const auto duplicate_compound_a2 = quote(j)(quote(k)(l));
    auto optimize_duplicate_takeout_once =
        [](combdsl::quoted_expression value) {
            return combdsl::detail::
                optimize_duplicate_takeout_expressions_once(
                    std::move(value));
        };
    test("duplicate takeout recognizes the H rule over compounds",
         optimize_duplicate_takeout_once(
             quote(W)(quote(C_star)(duplicate_compound_a))),
         "H(a(bc))");
    test("duplicate takeout gives H precedence when its operand is W",
         optimize_duplicate_takeout_once(
             quote(W)(quote(C_star)(W))),
         "HW");
    test("duplicate takeout gives H precedence when its operand is C-star",
         optimize_duplicate_takeout_once(
             quote(W)(quote(C_star)(C_star))),
         "HC*");
    test("duplicate takeout H rule recursively optimizes its capture",
         optimize_duplicate_takeout_once(
             quote(W)(quote(C_star)(quote(a)(b)(a)))),
         "H(Nab)");
    test("duplicate takeout descends to nested H-rule roots",
         optimize_duplicate_takeout_once(
             quote(p)(quote(W)(
                 quote(C_star)(duplicate_compound_a)))),
         "p(H(a(bc)))");
    test("duplicate takeout generalizes Nightingale over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(duplicate_compound_b)(
                 duplicate_compound_a)),
         "N(a(bc))(d(ef))");
    test("duplicate takeout generalizes Warbler over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(duplicate_compound_b)(
                 duplicate_compound_b)),
         "W(a(bc))(d(ef))");
    test("duplicate takeout generalizes Owl over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(
                 duplicate_compound_b(duplicate_compound_a))),
         "O(d(ef))(a(bc))");
    test("duplicate takeout generalizes Zazu over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(
                 duplicate_compound_a(duplicate_compound_b))),
         "Z(a(bc))(d(ef))");
    test("duplicate takeout generalizes Lark over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(
                 duplicate_compound_b(duplicate_compound_b))),
         "L(a(bc))(d(ef))");
    test("duplicate takeout generalizes Starling over compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a1(duplicate_compound_a)(
                 duplicate_compound_a2(duplicate_compound_a))),
         "S(g(hi))(j(kl))(a(bc))");
    test("duplicate takeout falls back to Mockingbird after specific rules",
         optimize_duplicate_takeout_once(quote(a)(a)),
         "Ma");
    test("duplicate takeout gives Starling precedence for equal compounds",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(duplicate_compound_a)),
         "Saa(bc)");
    test("duplicate takeout gives Nightingale top-down precedence",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(duplicate_compound_a)(
                 duplicate_compound_a)),
         "N(a(bc))(a(bc))");
    test("duplicate takeout gives Owl precedence over Zazu and Lark",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(
                 duplicate_compound_a(duplicate_compound_a))),
         "O(a(bc))(a(bc))");
    test("duplicate takeout gives Lark precedence and recurses "
         "into its capture",
         optimize_duplicate_takeout_once(
             duplicate_compound_a(duplicate_compound_a)(
                 duplicate_compound_a(duplicate_compound_a))),
         "L(Saa(bc))(a(bc))");
    test("duplicate takeout performs one top-down pass without a fixed point",
         optimize_duplicate_takeout_once(quote(N)(N)(M)),
         "MNM");
    const auto duplicate_inner_n =
        duplicate_compound_a(duplicate_compound_b)(
            duplicate_compound_a);
    const auto duplicate_inner_w =
        duplicate_compound_a(duplicate_compound_b)(
            duplicate_compound_b);
    test("duplicate takeout recursively optimizes captured operands",
         optimize_duplicate_takeout_once(
             duplicate_inner_n(duplicate_inner_w)(duplicate_inner_n)),
         "N(N(a(bc))(d(ef)))(W(a(bc))(d(ef)))");
    test("duplicate takeout descends when the root has no match",
         optimize_duplicate_takeout_once(
             quote(p)(duplicate_inner_n)(duplicate_inner_w)),
         "p(N(a(bc))(d(ef)))(W(a(bc))(d(ef)))");
    test("duplicate takeout compares separately built compounds structurally",
         optimize_duplicate_takeout_once(
             quote(a)(quote(b)(c))(duplicate_compound_b)(
                 quote(a)(quote(b)(c)))),
         "N(a(bc))(d(ef))");
    auto optimize_duplicate_takeout = [](combdsl::quoted_expression value) {
        return combdsl::detail::
            optimize_duplicate_takeout_expressions(std::move(value));
    };
    test("duplicate takeout rescans newly generated bird skeletons",
         optimize_duplicate_takeout(quote(x)(x)(x)),
         "WNx");
    test("duplicate takeout rescans a newly generated H skeleton",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(W)(quote(C_star)(H)));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
             for (auto const& stage : optimized.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "MH 2\n"
         "HH\n"
         "MH");
    test("duplicate takeout H capture rescans terminate on a cycle",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(W)(quote(C_star)(quote(N)(N)(M))));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
             for (auto const& stage : optimized.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "H(MNM) 4\n"
         "H(MNM)\n"
         "H(NMN)\n"
         "H(NNM)\n"
         "H(MNM)");
    test("duplicate takeout stage scan omits a stable no-op",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(p)(q));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
         },
         "pq 0");
    test("duplicate takeout stage scan omits a matching structural no-op",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(N)(N)(N));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
         },
         "NNN 0");
    test("duplicate takeout rescans nested duplicate expressions",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(p)(quote(x)(x)(x)));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
             for (auto const& stage : optimized.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "p(WNx) 2\n"
         "p(Nxx)\n"
         "p(WNx)");
    test("duplicate takeout aggregates disjoint rewrites into one stage",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(p)(quote(a)(b)(a))(
                         quote(c)(d)(d)));
             std::cout << optimized.stages.size();
             for (auto const& stage : optimized.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "1\n"
         "p(Nab)(Wcd)");
    test("duplicate takeout stops after recording a repeated cycle state",
         [] {
             auto optimized = combdsl::detail::
                 optimize_duplicate_takeout_expression_stages(
                     quote(N)(N)(M));
             optimized.result.print_to(std::cout);
             std::cout << ' ' << optimized.stages.size();
             for (auto const& stage : optimized.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "NNM 3\n"
         "MNM\n"
         "NMN\n"
         "NNM");
    struct duplicate_takeout_parity_case {
        std::string_view rule;
        combdsl::quoted_expression original;
    };
    const std::vector<duplicate_takeout_parity_case>
        duplicate_takeout_parity_cases{
            {"H", quote(W)(quote(C_star)(duplicate_compound_a))},
            {"N", duplicate_compound_a(duplicate_compound_b)(
                      duplicate_compound_a)},
            {"W", duplicate_compound_a(duplicate_compound_b)(
                      duplicate_compound_b)},
            {"O", duplicate_compound_a(
                      duplicate_compound_b(duplicate_compound_a))},
            {"Z", duplicate_compound_a(
                      duplicate_compound_a(duplicate_compound_b))},
            {"L", duplicate_compound_a(
                      duplicate_compound_b(duplicate_compound_b))},
            {"S", duplicate_compound_a1(duplicate_compound_a)(
                      duplicate_compound_a2(duplicate_compound_a))},
            {"M", quote(a)(a)},
        };
    for (auto const& parity_case : duplicate_takeout_parity_cases) {
        auto title = std::string("duplicate takeout ");
        title += parity_case.rule;
        title += " preserves semantics after applying symbols";
        test(
            title,
            [&] {
                auto original = combdsl::detail::normalize_for_compare(
                    parity_case.original(m)(n));
                auto optimized = combdsl::detail::normalize_for_compare(
                    optimize_duplicate_takeout(
                        parity_case.original)(m)(n));
                if (!original || !optimized) {
                    std::cout << "normalization timed out";
                    return;
                }
                std::cout <<
                    combdsl::detail::same_parser_definition_expression(
                        *original, *optimized);
            },
            "1");
    }
    const auto raw_duplicate_takeout_source =
        quote(x)(a)(quote(x)(a));
    test("public takeout keeps duplicate results raw",
         takeout(quoted_atomic{x}, raw_duplicate_takeout_source),
         "S(Ta)(Ta)");
    test("contextual takeout keeps duplicate results raw",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x}, raw_duplicate_takeout_source, {}),
         "S(Ta)(Ta)");
    test("ministep takeout API keeps duplicate result and stages raw",
         [&] {
             auto result = combdsl::detail::
                 takeout_with_pending_atoms_ministeps(
                     quoted_atomic{x}, raw_duplicate_takeout_source, {});
             result.result.print_to(std::cout);
             std::cout << ' ' << result.stages.size();
             for (auto const& stage : result.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "S(Ta)(Ta) 3\n"
         "S[takeout x from xa][takeout x from xa]\n"
         "S(Ta)[takeout x from xa]\n"
         "S(Ta)(Ta)");
    const auto raw_repeated_duplicate_source =
        quote(x)(x)(x)(y);
    test("public takeout does not run repeated duplicate rescans",
         takeout(quoted_atomic{y}, raw_repeated_duplicate_source),
         "xxx");
    test("public takeout does not run initial parser duplicate optimization",
         takeout(quoted_atomic{x}, quote(a)(b)(b)(x)),
         "abb");
    test("contextual takeout does not run repeated duplicate rescans",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{y}, raw_repeated_duplicate_source, {}),
         "xxx");
    test("ministep takeout keeps a repeated-rescan candidate raw",
         [&] {
             auto result = combdsl::detail::
                 takeout_with_pending_atoms_ministeps(
                     quoted_atomic{y}, raw_repeated_duplicate_source, {});
             result.result.print_to(std::cout);
             std::cout << ' ' << result.stages.size();
             for (auto const& stage : result.stages) {
                 std::cout << '\n';
                 stage.print_to(std::cout);
             }
         },
         "xxx 1\n"
         "xxx");
    test("takeout equal symbol",
         takeout(quoted_atomic{x}, quote(x)), "I");
    test("takeout separately parsed equal symbol",
         takeout(quoted_atomic{quote(x)}, parse("x")), "I");
    test("takeout equal symbolic string",
         takeout(quoted_atomic{"x"}, quote("x")), "I");
    test("takeout separately parsed equal symbolic string",
         takeout(quoted_atomic{quote("x")}, parse(R"("x")")), "I");
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
            combdsl::detail::basis_label("Foo"));
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
         "Q Foo y");
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
         "Qz Foo");
    test("x abstraction keeps Bluebird when t contains foo",
         combdsl::detail::takeout_with_pending_atoms(
             quoted_atomic{x},
             quote(u)(contextual_recursive_foo(x)),
             pending_foo),
         "Bu Foo");
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
    struct ministep_takeout_equivalence_case {
        std::string_view branch;
        combdsl::quoted_expression source;
        std::vector<quoted_atomic> pending_atoms;
    };
    const std::vector<ministep_takeout_equivalence_case>
        ministep_takeout_equivalence_cases{
            {"equal atom base case", quote(x), {}},
            {"absent atom base case", quote(y), {}},
            {"Mockingbird direct case", quote(x)(x), {}},
            {"Thrush direct case", quote(x)(y), {}},
            {"matching argument direct case", quote(y)(x), {}},
            {"Owl recursive case", quote(x)(quote(x)(y)), {}},
            {"Warbler recursive case", quote(y)(x)(x), {}},
            {"Cardinal default chooser", quote(y)(x)(z), {}},
            {"Robin next-pending chooser", quote(z)(x)(y),
             {quoted_atomic{z}}},
            {"Cardinal next-pending chooser", quote(y)(x)(z),
             {quoted_atomic{z}}},
            {"Cardinal pending-count chooser",
             quote(z)(x)(quote(w)(z)),
             {quoted_atomic{w}, quoted_atomic{z}}},
            {"Robin pending-count chooser", quote(w)(z)(x)(z),
             {quoted_atomic{w}, quoted_atomic{z}}},
            {"Bluebird default chooser", quote(y)(quote(z)(x)), {}},
            {"Queer next-pending chooser", quote(y)(quote(z)(x)),
             {quoted_atomic{y}}},
            {"Bluebird next-pending chooser", quote(y)(quote(z)(x)),
             {quoted_atomic{y}, quoted_atomic{z}}},
            {"Queer pending-count chooser", quote(w)(quote(z)(x)),
             {quoted_atomic{w}, quoted_atomic{y}}},
            {"Bluebird pending-count chooser", quote(z)(quote(w)(x)),
             {quoted_atomic{w}, quoted_atomic{y}}},
            {"Starling two-branch recursion",
             quote(x)(y)(quote(x)(z)), {}},
        };
    for (auto const& equivalence_case :
         ministep_takeout_equivalence_cases) {
        auto title = std::string("ministeps result matches takeout for ");
        title += equivalence_case.branch;
        test(
            title,
            [&] {
                auto const expected =
                    combdsl::detail::takeout_with_pending_atoms(
                        quoted_atomic{x},
                        equivalence_case.source,
                        equivalence_case.pending_atoms);
                auto const actual =
                    combdsl::detail::
                        takeout_with_pending_atoms_ministeps(
                            quoted_atomic{x},
                            equivalence_case.source,
                            equivalence_case.pending_atoms)
                        .result;
                std::cout <<
                    combdsl::detail::same_parser_definition_expression(
                        expected, actual);
            },
            "1");
    }
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
    const auto duplicate_xy_subexpression_match =
        combdsl::search_for_xy_subexp(
            quote(B)(K)(quote(S)(M)(M)));
    test("xy subexpression search keeps repeated duplicate takeout raw",
         [&] {
             if (duplicate_xy_subexpression_match) {
                 duplicate_xy_subexpression_match->takeout_result();
             } else {
                 std::cout << "not found";
             }
         },
         "BK(SMM)");
    test("raw duplicate search match retains its source expression",
         [&] {
             if (duplicate_xy_subexpression_match) {
                 duplicate_xy_subexpression_match->source_expression();
             } else {
                 std::cout << "not found";
             }
         },
         "xx(xx)");
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
    auto const j_single_matches =
        combdsl::check_for_singles_match(
            j_match_symbols,
            j_match_expression);
    test("single matching finds the Jay bird",
         [&] {
             for (auto const& match : j_single_matches) {
                 match.print_to(std::cout);
             }
         },
         "J");
    test("find includes the Jay bird",
         parse("find ?xyzw = xy(xwz)"), "?=J");
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
    test("find among restricts the catalog",
         [] {
             std::ostringstream output;
             parse("find among A ?xy = x(yx)").print_to(output);
             auto const padded = '\n' + output.str() + '\n';
             std::cout
                 << (padded.find("\n?=A\n") != std::string::npos)
                 << (padded.find("\n?=CO\n") == std::string::npos);
         },
         "11");
    test("find among searches increasing composition sizes",
         parse("find among B C ?x = BCx"), "?=BC");
    test("find among accepts compact fundamental and predefined birds",
         parse("find among AKIS ?xy = x(yx)"), "?=A");
    test("find among accepts mixed compact and spaced bird groups",
         parse("find among A BC\tS ?xy = x(yx)"), "?=A");
    test("find among accepts a compact group before a spaced bird",
         parse("find among BC S ?x = Bx"), "?=B");
    test("find among accepts a spaced bird before a compact group",
         parse("find among B CS ?x = Bx"), "?=B");
    test("find among chooses the longest punctuation bird prefix",
         parse("find among C**K ?x = C**x"), "?=C**");
    test("find among separates adjacent punctuation-ending birds",
         parse("find among C**W*K ?x = C**x"), "?=C**");
    test("find among treats punctuation as a compact bird boundary",
         parse("find among C*K ?x = C*x"), "?=C*");
    test("find among separates adjacent digit-ending birds",
         parse("find among Q1Q3 ?xyz = x(zy)"), "?=Q1");
    test("find among silently deduplicates repeated birds",
         parse("find among A A A ?xy = x(yx)"), "?=A");
    test("find among deduplicates compact repeated birds",
         parse("find among AA@1 ?xy = x(yx)"), "?=A");
    test("find among omits an exact pre-defined sole revision suffix",
         parse("find among A@1A ?xy = x(yx)"), "?=A");
    test("find among deduplicates an exact predefined revision after its name",
         parse("find among A A@1 ?xy = x(yx)"), "?=A");
    test("find among prints an exact predefined sole revision unqualified",
         parse("find among A@1 A ?xy = x(yx)"), "?=A");
    test("find among compact setup registers overlapping user names",
         [] {
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse("set Compact = 2 K"));
             static_cast<void>(parse("set CompactLong = 2 A"));
             static_cast<void>(parse("set CompactLongA = 2 A"));
             static_cast<void>(parse("set Longtail = 2 A"));
             static_cast<void>(parse("set CompactBang! = 2 A"));
             static_cast<void>(parse("set Compact1 = 2 A"));
             static_cast<void>(parse("set CompactQuest? = 2 A"));
             static_cast<void>(parse("set CompactRemoved = 2 A"));
             static_cast<void>(parse("set CompactLive = 2 A"));
             static_cast<void>(parse("set CompactOther = 2 A"));
             static_cast<void>(parse("remove CompactRemoved"));
             std::cout << "ready";
         },
         "ready");
    test("find among gives an exact whole-group user name precedence",
         parse("find among CompactLongA ?xy = x(yx)"),
         "?=CompactLongA");
    test("find among greedily chooses the longest user-name prefix",
         parse("find among CompactLongB ?xy = x(yx)"),
         "?=CompactLong");
    test("find among whitespace can force a shorter user-name boundary",
         parse("find among CompactLong A ?xy = x(yx)"),
         "?=CompactLong\n?=A");
    test("find among parses adjacent punctuation-ending user names",
         parse("find among CompactBang!K ?xy = x(yx)"),
         "?=CompactBang!");
    test("find among parses adjacent digit-ending user names",
         parse("find among KCompact1A ?xy = x(yx)"),
         "?=Compact1\n?=A");
    test_parse_failure(
        "find among rejects a lowercase-leading user name",
        "find among lowerbird ?xy = x(yx)", 11,
        "lowerbird is not a defined name");
    test_parse_failure(
        "find among does not treat an integer as a basis name",
        "find among 7 ?xy = x(yx)", 11,
        "7 is not a defined name");
    test("find among retains question marks inside an exact bird name",
         parse("find among CompactQuest? ?xy = x(yx)"),
         "?=CompactQuest?");
    test("find among retains an ending question mark before an adjacent bird",
         parse("find among CompactQuest?A ?xy = x(yx)"),
         "?=CompactQuest?\n?=A");
    test("find among parses an explicit revision next to another bird",
         parse("find among CompactLong@1A ?xy = x(yx)"),
         "?=CompactLong\n?=A");
    test("find among accepts a leading-zero revision next to another bird",
         parse("find among CompactLong@0001A ?xy = x(yx)"),
         "?=CompactLong\n?=A");
    test("find among retains a valid revision before many adjacent birds",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             combdsl::detail::find_clock_now_override = [&] {
                 ++clock_calls;
                 return combdsl::detail::find_clock::time_point::max();
             };
             auto source = std::string(
                 "find all among CompactLong@1");
             source.append(30, 'A');
             source += "A ?x = x";
             auto const parsed = combdsl::detail::parse_input(
                 source,
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << clock_calls << ' '
                       << parsed.is_display_only
                       << parsed.is_find << ' ';
             parsed.expression.print_to(std::cout);
         },
         "0 11 x");
    test("find among parses adjacent revisions as separate birds",
         parse(
             "find among CompactLong@1CompactOther@1 "
             "?xy = x(yx)"),
         "?=CompactLong\n?=CompactOther");
    test("find among accepts a removed revision inside a compact group",
         parse("find among CompactRemoved@1A ?xy = x(yx)"),
         "?=CompactRemoved\n?=A");
    test("find among accepts a bare removed sole revision",
         parse("find among CompactRemoved ?xy = x(yx)"),
         "?=CompactRemoved");
    test("find among deduplicates identical singleton live and captured output",
         [] {
             static_cast<void>(parse("references live"));
             parse(
                 "find among CompactLiveCompactLive@1 "
                 "?xy = x(yx)")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "?=CompactLive");
    test_parse_failure(
        "find among uses greedy prefixes without backtracking",
        "find among CompactLongtail ?x = x", 22,
        "tail is not a defined name");
    test_parse_failure(
        "find among reports an invalid suffix after a revision",
        "find among CompactLong@1$ ?x = x", 24,
        "$ is not a defined name");
    test_parse_failure(
        "find among reports an unavailable revision at its at-sign",
        "find among CompactLong@9A ?x = x", 22,
        "@9A is not a defined name");
    test_parse_failure(
        "find among compact groups still require a question-mark marker",
        "find among BCS", 14,
        "expected '?'");
    test("find among reports a long compact invalid suffix without searching",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             combdsl::detail::find_clock_now_override = [&] {
                 ++clock_calls;
                 return combdsl::detail::find_clock::time_point::max();
             };
             auto source = std::string("find among ");
             source.append(1'024, 'A');
             source += "$ ?x = x";
             try {
                 static_cast<void>(combdsl::detail::parse_input(
                     source,
                     combdsl::detail::parser_definition_mode::
                         inspect_definitions));
             } catch (parse_error const& error) {
                 std::cout << clock_calls << ' '
                           << error.position() << ' '
                           << error.detail();
             }
         },
         "0 1035 $ is not a defined name");
    test("find among enumerates every size-three shape and labeling in order",
         [] {
             std::array<combdsl::quoted_expression, 2> const catalog{
                 quote(A), quote(B),
             };
             std::vector<combdsl::quoted_expression> candidates;
             auto const status =
                 combdsl::detail::for_each_catalog_candidate_of_size(
                     3,
                     catalog,
                     combdsl::detail::find_clock::time_point::max(),
                     [&](combdsl::quoted_expression candidate) {
                         candidates.push_back(std::move(candidate));
                         return true;
                     });
             std::cout
                 << (status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << ' ' << candidates.size() << ' ';
             for (std::size_t index = 0;
                  index < candidates.size();
                  ++index) {
                 if (index != 0) {
                     std::cout << ',';
                 }
                 candidates[index].print_to(std::cout);
             }
         },
         "1 16 AAA,AAB,ABA,ABB,BAA,BAB,BBA,BBB,"
         "A(AA),A(AB),A(BA),A(BB),B(AA),B(AB),B(BA),B(BB)");
    test("find among size shards partition the deterministic candidate stream",
         [] {
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(I)(atom),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const target = atom(atom)(atom);
             auto const normalized_target = combdsl::detail::
                 normalize_for_combinator_match(target);
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             auto const serial = combdsl::detail::
                 find_combinator_matches_among_normalized_size_shard_until(
                     symbols,
                     *normalized_target,
                     catalog,
                     3,
                     0,
                     1,
                     deadline);

             std::vector<combdsl::detail::catalog_find_shard_match>
                 merged;
             bool ownership_is_exact = true;
             bool every_shard_completed = true;
             for (std::size_t shard_index = 0;
                  shard_index < 3;
                  ++shard_index) {
                 auto shard = combdsl::detail::
                     find_combinator_matches_among_normalized_size_shard_until(
                         symbols,
                         *normalized_target,
                         catalog,
                         3,
                         shard_index,
                         3,
                         deadline);
                 every_shard_completed =
                     every_shard_completed && !shard.timed_out;
                 for (auto& match : shard.matches) {
                     ownership_is_exact = ownership_is_exact &&
                         match.index % 3 == shard_index;
                     merged.push_back(std::move(match));
                 }
             }
             std::ranges::sort(
                 merged,
                 {},
                 &combdsl::detail::catalog_find_shard_match::index);

             bool same_as_serial = merged.size() == serial.matches.size();
             for (std::size_t index = 0;
                  same_as_serial && index < merged.size();
                  ++index) {
                 same_as_serial =
                     merged[index].index == serial.matches[index].index &&
                     combdsl::detail::same_parser_definition_expression(
                         merged[index].expression,
                         serial.matches[index].expression);
             }
             std::cout
                 << !serial.timed_out << ' '
                 << (serial.matches.size() == 8) << ' '
                 << every_shard_completed << ' '
                 << ownership_is_exact << ' '
                 << same_as_serial << ' ';
             for (std::size_t index = 0;
                  index < merged.size();
                  ++index) {
                 if (index != 0) {
                     std::cout << ',';
                 }
                 std::cout << merged[index].index;
             }
         },
         "1 1 1 1 1 0,1,2,3,4,5,6,7");
    test("find among size shards mark partial work timed out",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(I)(atom),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const target = atom(atom)(atom);
             auto const normalized_target = combdsl::detail::
                 normalize_for_combinator_match(target);
             bool stop = false;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.timeout_requested = [&] { return stop; };
             overrides.candidate_observer = [&](auto observation) {
                 if (observation.leaf_count == 3 &&
                     observation.candidate_index == 2 &&
                     observation.stage == combdsl::detail::
                         catalog_find_candidate_observation_stage::
                             after_match) {
                     stop = true;
                 }
             };

             auto const result = combdsl::detail::
                 find_combinator_matches_among_normalized_size_shard_until(
                     symbols,
                     *normalized_target,
                     catalog,
                     3,
                     0,
                     1,
                     combdsl::detail::find_clock::time_point::max());
             std::cout << result.timed_out << ' '
                       << result.matches.size() << ' ';
             for (std::size_t index = 0;
                  index < result.matches.size();
                  ++index) {
                 if (index != 0) {
                     std::cout << ',';
                 }
                 std::cout << result.matches[index].index;
             }
         },
         "1 2 0,1");
    test("find among size shards validate their bounds",
         [] {
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 1> const catalog{atom};
             std::array<combdsl::quoted_expression, 0> const empty_catalog{};
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             auto rejects = [&](auto const& selected_catalog,
                                std::size_t leaf_count,
                                std::size_t shard_index,
                                std::size_t shard_count) {
                 try {
                     static_cast<void>(combdsl::detail::
                         find_combinator_matches_among_normalized_size_shard_until(
                             symbols,
                             atom,
                             selected_catalog,
                             leaf_count,
                             shard_index,
                             shard_count,
                             deadline));
                     return false;
                 } catch (std::invalid_argument const&) {
                     return true;
                 }
             };
             std::cout
                 << rejects(empty_catalog, 1, 0, 1)
                 << rejects(catalog, 0, 0, 1)
                 << rejects(catalog, 1, 0, 0)
                 << rejects(catalog, 1, 1, 1);
         },
         "1111");
#if !defined(__EMSCRIPTEN__)
    test("native find among keeps small sizes serial and overlaps size three",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const producer_thread = std::this_thread::get_id();
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(I)(atom),
             };
             auto const target = atom(atom)(atom);
             std::array<combdsl::quoted_atomic, 0> const symbols{};

             std::vector<combdsl::detail::
                 catalog_find_dispatch_observation> dispatches;
             std::array<std::atomic<std::size_t>, 4> starts{};
             std::atomic<bool> serial_on_producer = true;
             std::atomic<bool> parallel_off_producer = true;
             std::latch zero_started{1};
             std::binary_semaphore peer_finished{0};
             std::atomic<bool> zero_waited_for_peer = false;
             std::atomic<bool> peer_overlapped_zero = false;
             std::atomic<std::size_t> peer_index =
                 std::numeric_limits<std::size_t>::max();
             std::mutex observations_mutex;
             std::vector<std::thread::id> size_three_threads;
             std::vector<std::size_t> completion_order;

             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 4;
             overrides.dispatch_observer = [&](auto observation) {
                 dispatches.push_back(observation);
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.stage == stage::before_match) {
                     starts[observation.leaf_count].fetch_add(1);
                     auto const worker_thread =
                         std::this_thread::get_id();
                     if (observation.leaf_count < 3) {
                         if (worker_thread != producer_thread) {
                             serial_on_producer.store(false);
                         }
                         return;
                     }
                     if (worker_thread == producer_thread) {
                         parallel_off_producer.store(false);
                     }
                     {
                         std::scoped_lock lock(observations_mutex);
                         if (std::find(
                                 size_three_threads.begin(),
                                 size_three_threads.end(),
                                 worker_thread) ==
                             size_three_threads.end()) {
                             size_three_threads.push_back(worker_thread);
                         }
                     }
                     if (observation.candidate_index == 0) {
                         zero_started.count_down();
                         zero_waited_for_peer.store(
                             peer_finished.try_acquire_for(
                                 std::chrono::seconds{5}));
                     } else {
                         zero_started.wait();
                         auto expected =
                             std::numeric_limits<std::size_t>::max();
                         if (peer_index.compare_exchange_strong(
                                 expected,
                                 observation.candidate_index)) {
                             peer_overlapped_zero.store(true);
                         }
                     }
                     return;
                 }
                 if (observation.leaf_count != 3) {
                     return;
                 }
                 {
                     std::scoped_lock lock(observations_mutex);
                     completion_order.push_back(
                         observation.candidate_index);
                 }
                 if (observation.candidate_index ==
                     peer_index.load()) {
                     peer_finished.release();
                 }
             };

             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, target, catalog, false);

             std::vector<combdsl::quoted_expression> expected;
             static_cast<void>(
                 combdsl::detail::for_each_catalog_candidate_of_size(
                     3,
                     catalog,
                     combdsl::detail::find_clock::time_point::max(),
                     [&](combdsl::quoted_expression candidate) {
                         expected.push_back(std::move(candidate));
                         return true;
                     }));
             bool sequential_matches =
                 result.completed_sizes.size() == 3 &&
                 result.completed_sizes[2].size() == 8 &&
                 expected.size() == 16;
             for (std::size_t index = 0;
                  sequential_matches && index < 8;
                  ++index) {
                 sequential_matches =
                     combdsl::detail::
                         same_parser_definition_expression(
                             result.completed_sizes[2][index],
                             expected[index]);
             }
             auto const zero_completion = std::find(
                 completion_order.begin(), completion_order.end(), 0);
             auto const selected_peer = peer_index.load();
             auto const peer_completion = std::find(
                 completion_order.begin(),
                 completion_order.end(),
                 selected_peer);
             auto const dispatches_are_expected =
                 dispatches.size() == 3 &&
                 dispatches[0].leaf_count == 1 &&
                 !dispatches[0].parallel &&
                 dispatches[0].worker_count == 0 &&
                 dispatches[1].leaf_count == 2 &&
                 !dispatches[1].parallel &&
                 dispatches[1].worker_count == 0 &&
                 dispatches[2].leaf_count == 3 &&
                 dispatches[2].parallel &&
                 dispatches[2].worker_count == 3;
             std::cout
                 << dispatches_are_expected << ' '
                 << (starts[1].load() == 2 &&
                     starts[2].load() == 4 &&
                     starts[3].load() == 16) << ' '
                 << serial_on_producer.load() << ' '
                 << parallel_off_producer.load() << ' '
                 << (size_three_threads.size() >= 2) << ' '
                 << peer_overlapped_zero.load() << ' '
                 << zero_waited_for_peer.load() << ' '
                 << (zero_completion != completion_order.end() &&
                     selected_peer !=
                         std::numeric_limits<std::size_t>::max() &&
                     peer_completion != completion_order.end() &&
                     peer_completion < zero_completion) << ' '
                 << sequential_matches << ' '
                 << !result.timed_out;
         },
         "1 1 1 1 1 1 1 1 1 1");
    test("native find among grows and reuses persistent static shards",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const producer_thread = std::this_thread::get_id();
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 1> const catalog{
                 atom,
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const target = atom(atom)(atom);

             std::vector<combdsl::detail::
                 catalog_find_dispatch_observation> dispatches;
             std::array<std::array<std::size_t, 5>, 6>
                 shard_tokens{};
             std::array<std::array<std::size_t, 5>, 6>
                 shard_starts{};
             std::mutex observations_mutex;
             std::atomic<bool> ownership_is_static = true;
             std::atomic<bool> serial_on_producer = true;
             std::atomic<bool> parallel_off_producer = true;
             std::atomic<bool> stop = false;

             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             // Five usable native workers: size three needs two, size
             // four grows the pool to five, and size five reuses all five.
             overrides.reported_hardware_concurrency = 6;
             overrides.timeout_requested = [&] {
                 return stop.load();
             };
             overrides.dispatch_observer = [&](auto observation) {
                 dispatches.push_back(observation);
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.stage != stage::before_match) {
                     return;
                 }
                 auto const current_thread =
                     std::this_thread::get_id();
                 if (observation.leaf_count < 3) {
                     if (current_thread != producer_thread) {
                         serial_on_producer.store(false);
                     }
                     return;
                 }
                 if (current_thread == producer_thread) {
                     parallel_off_producer.store(false);
                 }
                 if (observation.leaf_count == 6) {
                     stop.store(true);
                     return;
                 }
                 if (observation.leaf_count > 5) {
                     return;
                 }

                 auto const active_workers =
                     observation.leaf_count == 3
                         ? std::size_t{2}
                         : std::size_t{5};
                 auto const shard_index =
                     observation.candidate_index % active_workers;
                 auto const token = catalog_find_thread_token();
                 std::scoped_lock lock(observations_mutex);
                 auto& recorded = shard_tokens
                     [observation.leaf_count][shard_index];
                 if (recorded == 0) {
                     recorded = token;
                 } else if (recorded != token) {
                     ownership_is_static.store(false);
                 }
                 ++shard_starts
                     [observation.leaf_count][shard_index];
             };

             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, target, catalog, true);

             auto all_unique_and_present =
                 [&](std::size_t leaf_count,
                     std::size_t worker_count) {
                     for (std::size_t left = 0;
                          left < worker_count;
                          ++left) {
                         if (shard_tokens[leaf_count][left] == 0) {
                             return false;
                         }
                         for (std::size_t right = left + 1;
                              right < worker_count;
                              ++right) {
                             if (shard_tokens[leaf_count][left] ==
                                 shard_tokens[leaf_count][right]) {
                                 return false;
                             }
                         }
                     }
                     return true;
                 };
             auto const dispatches_are_expected =
                 dispatches.size() == 6 &&
                 dispatches[0].leaf_count == 1 &&
                 !dispatches[0].parallel &&
                 dispatches[0].worker_count == 0 &&
                 dispatches[1].leaf_count == 2 &&
                 !dispatches[1].parallel &&
                 dispatches[1].worker_count == 0 &&
                 dispatches[2].leaf_count == 3 &&
                 dispatches[2].parallel &&
                 dispatches[2].worker_count == 2 &&
                 dispatches[3].leaf_count == 4 &&
                 dispatches[3].parallel &&
                 dispatches[3].worker_count == 5 &&
                 dispatches[4].leaf_count == 5 &&
                 dispatches[4].parallel &&
                 dispatches[4].worker_count == 5 &&
                 dispatches[5].leaf_count == 6 &&
                 dispatches[5].parallel &&
                 dispatches[5].worker_count == 5;
             auto const every_shard_ran =
                 shard_starts[3] ==
                     std::array<std::size_t, 5>{1, 1, 0, 0, 0} &&
                 shard_starts[4] ==
                     std::array<std::size_t, 5>{1, 1, 1, 1, 1} &&
                 shard_starts[5] ==
                     std::array<std::size_t, 5>{3, 3, 3, 3, 2};
             auto const workers_are_persistent =
                 shard_tokens[3][0] == shard_tokens[4][0] &&
                 shard_tokens[3][1] == shard_tokens[4][1] &&
                 shard_tokens[4] == shard_tokens[5];
             auto const prior_sizes_are_committed =
                 result.completed_sizes.size() == 5 &&
                 result.completed_sizes[0].empty() &&
                 result.completed_sizes[1].empty() &&
                 result.completed_sizes[2].size() == 1 &&
                 combdsl::detail::same_parser_definition_expression(
                     result.completed_sizes[2].front(), target) &&
                 result.completed_sizes[3].empty() &&
                 result.completed_sizes[4].empty();
             std::cout
                 << dispatches_are_expected << ' '
                 << every_shard_ran << ' '
                 << ownership_is_static.load() << ' '
                 << (all_unique_and_present(3, 2) &&
                     all_unique_and_present(4, 5) &&
                     all_unique_and_present(5, 5)) << ' '
                 << workers_are_persistent << ' '
                 << serial_on_producer.load() << ' '
                 << parallel_off_producer.load() << ' '
                 << result.timed_out << ' '
                 << prior_sizes_are_committed;
         },
         "1 1 1 1 1 1 1 1 1");
    test("native find among caps workers for a small catalog",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 1> const catalog{
                 atom,
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             std::size_t size_three_workers = 0;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 100;
             overrides.dispatch_observer = [&](auto observation) {
                 if (observation.leaf_count == 3) {
                     size_three_workers = observation.worker_count;
                 }
             };
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, atom(atom)(atom), catalog, false);
             std::cout
                 << (size_three_workers == 2) << ' '
                 << !result.timed_out << ' '
                 << (result.completed_sizes.size() == 3) << ' '
                 << (result.completed_sizes.size() == 3 &&
                     result.completed_sizes[2].size() == 1);
         },
         "1 1 1 1");
    test("native find among caps and exhausts ordered modulo shards",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const producer_thread = std::this_thread::get_id();
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(I)(atom),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const target = atom(atom)(atom);
             auto const normalized_target = combdsl::detail::
                 normalize_for_combinator_match(target);
             auto const serial = combdsl::detail::
                 find_combinator_matches_among_normalized_size_shard_until(
                     symbols,
                     *normalized_target,
                     catalog,
                     3,
                     0,
                     1,
                     combdsl::detail::find_clock::time_point::max());

             std::size_t active_workers = 0;
             std::array<std::size_t, 8> shard_tokens{};
             std::array<std::size_t, 8> shard_starts{};
             std::mutex observations_mutex;
             std::atomic<bool> ownership_is_static = true;
             std::atomic<bool> workers_are_off_producer = true;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 100;
             overrides.dispatch_observer = [&](auto observation) {
                 if (observation.leaf_count == 3) {
                     active_workers = observation.worker_count;
                 }
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.leaf_count != 3 ||
                     observation.stage != stage::before_match) {
                     return;
                 }
                 if (std::this_thread::get_id() == producer_thread) {
                     workers_are_off_producer.store(false);
                 }
                 auto const shard_index =
                     observation.candidate_index % shard_tokens.size();
                 auto const token = catalog_find_thread_token();
                 std::scoped_lock lock(observations_mutex);
                 auto& recorded = shard_tokens[shard_index];
                 if (recorded == 0) {
                     recorded = token;
                 } else if (recorded != token) {
                     ownership_is_static.store(false);
                 }
                 ++shard_starts[shard_index];
             };

             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, target, catalog, false);

             bool all_workers_unique = true;
             for (std::size_t left = 0;
                  left < shard_tokens.size();
                  ++left) {
                 all_workers_unique =
                     all_workers_unique &&
                     shard_tokens[left] != 0;
                 for (std::size_t right = left + 1;
                      right < shard_tokens.size();
                      ++right) {
                     all_workers_unique =
                         all_workers_unique &&
                         shard_tokens[left] != shard_tokens[right];
                 }
             }
             bool exact_order =
                 !serial.timed_out &&
                 result.completed_sizes.size() == 3 &&
                 result.completed_sizes[2].size() ==
                     serial.matches.size();
             for (std::size_t index = 0;
                  exact_order && index < serial.matches.size();
                  ++index) {
                 exact_order = combdsl::detail::
                     same_parser_definition_expression(
                         result.completed_sizes[2][index],
                         serial.matches[index].expression);
             }
             std::cout
                 << (active_workers ==
                     combdsl::detail::
                         native_catalog_find_worker_limit) << ' '
                 << (shard_starts ==
                     std::array<std::size_t, 8>{
                         2, 2, 2, 2, 2, 2, 2, 2}) << ' '
                 << ownership_is_static.load() << ' '
                 << all_workers_unique << ' '
                 << workers_are_off_producer.load() << ' '
                 << (serial.matches.size() == 8) << ' '
                 << exact_order << ' '
                 << !result.timed_out;
         },
         "1 1 1 1 1 1 1 1");
    test("native find among discards a timed-out parallel size",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(symbol('b')),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             std::atomic<bool> timeout = false;
             std::atomic<bool> matched_candidate_finished = false;
             std::size_t size_three_workers = 0;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 4;
             overrides.timeout_requested = [&] {
                 return timeout.load();
             };
             overrides.dispatch_observer = [&](auto observation) {
                 if (observation.leaf_count == 3) {
                     size_three_workers = observation.worker_count;
                 }
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.leaf_count == 3 &&
                     observation.candidate_index == 0 &&
                     observation.stage == stage::after_match) {
                     matched_candidate_finished.store(true);
                     timeout.store(true);
                 }
             };
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, atom(atom)(atom), catalog, true);
             std::cout
                 << result.timed_out << ' '
                 << matched_candidate_finished.load() << ' '
                 << (size_three_workers == 3) << ' '
                 << (result.completed_sizes.size() == 2) << ' '
                 << (result.completed_sizes.size() == 2 &&
                     result.completed_sizes[0].empty() &&
                     result.completed_sizes[1].empty());
         },
         "1 1 1 1 1");
    test("native find among final deadline discards a parallel size",
         [] {
             catalog_find_runtime_overrides_reset runtime_reset;
             find_clock_override_reset clock_reset;
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(symbol('b')),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             std::atomic<bool> deadline_reached = false;
             std::atomic<bool> matched_candidate_finished = false;
             std::size_t size_three_workers = 0;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 4;
             overrides.dispatch_observer = [&](auto observation) {
                 if (observation.leaf_count != 3) {
                     return;
                 }
                 size_three_workers = observation.worker_count;
                 // The parallel/serial choice for this size has already
                 // been made. Only the producer thread receives this
                 // override, so every worker continues to share the real
                 // deadline while finalization observes its expiration.
                 combdsl::detail::find_clock_now_override = [&] {
                     return deadline_reached.load()
                         ? combdsl::detail::find_clock::time_point::max()
                         : combdsl::detail::find_clock::now();
                 };
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.leaf_count == 3 &&
                     observation.candidate_index == 0 &&
                     observation.stage == stage::after_match) {
                     matched_candidate_finished.store(true);
                     deadline_reached.store(true);
                 }
             };
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, atom(atom)(atom), catalog, true);
             std::cout
                 << result.timed_out << ' '
                 << matched_candidate_finished.load() << ' '
                 << (size_three_workers == 3) << ' '
                 << (result.completed_sizes.size() == 2) << ' '
                 << (result.completed_sizes.size() == 2 &&
                     result.completed_sizes[0].empty() &&
                     result.completed_sizes[1].empty());
         },
         "1 1 1 1 1");
    test("native find among propagates worker observer exceptions",
         [] {
             catalog_find_runtime_overrides_reset reset;
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 2> const catalog{
                 atom,
                 quote(symbol('b')),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 4;
             overrides.candidate_observer = [](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.leaf_count == 3 &&
                     observation.candidate_index == 3 &&
                     observation.stage == stage::before_match) {
                     throw std::runtime_error(
                         "catalog worker observer failure");
                 }
             };
             bool caught = false;
             try {
                 static_cast<void>(
                     combdsl::find_combinator_matches_among(
                         symbols, atom(atom)(atom), catalog, false));
             } catch (std::runtime_error const& error) {
                 caught = std::string_view(error.what()) ==
                     "catalog worker observer failure";
             }
             overrides.candidate_observer = {};
             auto const recovered =
                 combdsl::find_combinator_matches_among(
                     symbols, atom(atom)(atom), catalog, false);
             std::cout
                 << caught << ' '
                 << !recovered.timed_out << ' '
                 << (recovered.completed_sizes.size() == 3) << ' '
                 << (recovered.completed_sizes.size() == 3 &&
                     recovered.completed_sizes[2].size() == 1);
         },
         "1 1 1 1");
    test("native find among keeps fake-clock searches serial",
         [] {
             catalog_find_runtime_overrides_reset runtime_reset;
             find_clock_override_reset clock_reset;
             auto const producer_thread = std::this_thread::get_id();
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [=] {
                 return epoch;
             };
             auto const atom = quote(symbol('a'));
             std::array<combdsl::quoted_expression, 1> const catalog{
                 atom,
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             std::vector<combdsl::detail::
                 catalog_find_dispatch_observation> dispatches;
             std::array<std::size_t, 4> starts{};
             bool every_candidate_on_producer = true;
             auto& overrides = combdsl::detail::
                 catalog_find_runtime_overrides_override;
             overrides.reported_hardware_concurrency = 100;
             overrides.dispatch_observer = [&](auto observation) {
                 dispatches.push_back(observation);
             };
             overrides.candidate_observer = [&](auto observation) {
                 using stage = combdsl::detail::
                     catalog_find_candidate_observation_stage;
                 if (observation.stage != stage::before_match ||
                     observation.leaf_count >= starts.size()) {
                     return;
                 }
                 every_candidate_on_producer =
                     every_candidate_on_producer &&
                     std::this_thread::get_id() == producer_thread;
                 ++starts[observation.leaf_count];
             };

             auto const target = atom(atom)(atom);
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, target, catalog, false);
             auto const every_dispatch_is_serial =
                 dispatches.size() == 3 &&
                 dispatches[0].leaf_count == 1 &&
                 !dispatches[0].parallel &&
                 dispatches[0].worker_count == 0 &&
                 dispatches[1].leaf_count == 2 &&
                 !dispatches[1].parallel &&
                 dispatches[1].worker_count == 0 &&
                 dispatches[2].leaf_count == 3 &&
                 !dispatches[2].parallel &&
                 dispatches[2].worker_count == 0;
             std::cout
                 << every_dispatch_is_serial << ' '
                 << every_candidate_on_producer << ' '
                 << (starts ==
                     std::array<std::size_t, 4>{0, 1, 1, 2}) << ' '
                 << !result.timed_out << ' '
                 << (result.completed_sizes.size() == 3 &&
                     result.completed_sizes[2].size() == 1 &&
                     combdsl::detail::same_parser_definition_expression(
                         result.completed_sizes[2].front(), target));
         },
         "1 1 1 1 1");
#endif
    test("find among inspection accepts fundamental birds without searching",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             combdsl::detail::find_clock_now_override = [&] {
                 ++clock_calls;
                 return combdsl::detail::find_clock::time_point::max();
             };
             auto const parsed = combdsl::detail::parse_input(
                 "find all among SKIY ?x = x",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << clock_calls << ' '
                       << parsed.is_display_only
                       << parsed.is_find << ' ';
             parsed.expression.print_to(std::cout);
         },
         "0 11 x");
    test("ordinary fixed Find does not use the among deadline",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             combdsl::detail::find_clock_now_override = [&] {
                 ++clock_calls;
                 return combdsl::detail::find_clock::time_point::max();
             };
             parse("find 1 ?xy = x(yx)").print_to(std::cout);
             std::cout << ' ' << clock_calls;
         },
         "?=A 0");
    test("find among registers a user bird revision",
         [] {
             static_cast<void>(parse("references captured"));
             parse("set FindAmongUser = 2 A").print_to(std::cout);
         },
         "FindAmongUser");
    test("find among searches the current user revision",
         parse("find among FindAmongUser ?xy = x(yx)"),
         "?=FindAmongUser");
    test("find among can retain an earlier explicit revision",
         [] {
             static_cast<void>(parse("set FindAmongUser = 2 K"));
             parse("find among FindAmongUser@1 ?xy = x(yx)")
                 .print_to(std::cout);
         },
         "?=FindAmongUser@1");
    test("find among accepts an explicit removed revision",
         [] {
             static_cast<void>(parse("remove FindAmongUser"));
             parse("find among FindAmongUser@1 ?xy = x(yx)")
                 .print_to(std::cout);
         },
         "?=FindAmongUser@1");
    test("find among follows normal live reference semantics",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse(
                 "set FindAmongLive = 2 A"));
             parse("find among FindAmongLive ?xy = x(yx)")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "?=FindAmongLive");
    test("find among deduplicates singleton live and captured references",
         [] {
             static_cast<void>(parse("references live"));
             parse(
                 "find among FindAmongLive FindAmongLive@1 "
                 "?xy = x(yx)")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "?=FindAmongLive");
    test("find among keeps live and captured revisions after redefinition",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse(
                 "set FindAmongLive = 2 S(KA)I"));
             parse(
                 "find among FindAmongLive FindAmongLive@1 "
                 "?xy = x(yx)")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "?=FindAmongLive\n?=FindAmongLive@1");
    test("find among keeps live and captured revisions at threaded sizes",
         [] {
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse("set ThreadRef = 100 I"));
             static_cast<void>(parse("references live"));
             static_cast<void>(parse("set ThreadRef = 100 K"));
             parse(
                 "find among ThreadRef ThreadRef@1 ?x = "
                 "ThreadRef@1 ThreadRef@1 ThreadRef@1 x")
                 .print_to(std::cout);
             static_cast<void>(parse("references captured"));
         },
         "?=ThreadRef@1 ThreadRef@1 ThreadRef@1");
    test_parse_failure(
        "find among rejects an unqualified removed bird",
        "find among FindAmongUser ?xy = x(yx)", 12,
        "indAmongUser is not a defined name");
    test("find among can find a composition larger than four",
         [] {
             static_cast<void>(parse(
                 "set FindAmongLeaf = 100 I"));
             parse(
                 "find among FindAmongLeaf@1 ?x = "
                 "FindAmongLeaf@1 FindAmongLeaf@1 FindAmongLeaf@1 "
                 "FindAmongLeaf@1 FindAmongLeaf@1 x")
                 .print_to(std::cout);
         },
         "?=FindAmongLeaf FindAmongLeaf FindAmongLeaf "
         "FindAmongLeaf FindAmongLeaf");
    test("find among keeps command output at the threaded size boundary",
         [] {
             static_cast<void>(parse(
                 "set FindThreadLeaf = 100 I"));
             parse(
                 "find among FindThreadLeaf ?x = "
                 "FindThreadLeaf FindThreadLeaf FindThreadLeaf x")
                 .print_to(std::cout);
         },
         "?=FindThreadLeaf FindThreadLeaf FindThreadLeaf");
    test("find among times out at the exact ten-second deadline",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [&] {
                 auto const before_deadline = clock_calls++ == 0;
                 return before_deadline
                     ? epoch
                     : epoch + combdsl::detail::find_search_window;
             };
             std::array<combdsl::quoted_expression, 1> const catalog{
                 quote(A),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, quote(A), catalog, true);
             std::cout << result.timed_out << ' '
                       << result.completed_sizes.size() << ' '
                       << clock_calls;
         },
         "1 0 2");
    test("find among timeout keeps the existing no-match report",
         [] {
             std::size_t clock_calls = 0;
             find_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [&] {
                 return clock_calls++ == 0
                     ? epoch
                     : epoch + combdsl::detail::find_search_window;
             };
             auto const parsed = combdsl::detail::parse_input(
                 "find all among A ?xy = x(yx)");
             parsed.expression.print_to(std::cout);
             std::cout << ' ' << parsed.is_find_no_match;
         },
         "No match within search bounds 1");
    test("find among discards a partially completed matching size",
         [] {
             std::size_t clock_calls = 0;
             // The size-two candidate has matched by this boundary,
             // but the completed size has not yet been committed.
             constexpr std::size_t calls_before_deadline = 34;
             find_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [&] {
                 auto const before_deadline =
                     clock_calls++ < calls_before_deadline;
                 return before_deadline
                     ? epoch
                     : epoch + combdsl::detail::find_search_window;
             };
             std::array<combdsl::quoted_expression, 1> const catalog{
                 quote(A),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, quote(A)(A), catalog, true);
             std::cout << result.timed_out << ' '
                       << result.completed_sizes.size() << ' '
                       << (result.completed_sizes.size() == 1 &&
                           result.completed_sizes.front().empty()) << ' '
                       << clock_calls;
         },
         "1 1 1 35");
    test("find all among keeps completed sizes until its deadline",
         [] {
             std::size_t clock_calls = 0;
             constexpr std::size_t calls_before_deadline = 1'000;
             find_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [&] {
                 auto const before_deadline =
                     clock_calls++ < calls_before_deadline;
                 return before_deadline
                     ? epoch
                     : epoch + combdsl::detail::find_search_window;
             };
             std::array<combdsl::quoted_expression, 1> const catalog{
                 quote(A),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, quote(A), catalog, true);
             std::cout << result.timed_out << ' '
                       << (result.completed_sizes.size() >= 2) << ' '
                       << (!result.completed_sizes.empty() &&
                           result.completed_sizes.front().size() == 1) << ' '
                       << clock_calls;
         },
         "1 1 1 1001");
    test("find among without all stops at its first matching size",
         [] {
             std::size_t clock_calls = 0;
             constexpr std::size_t calls_before_deadline = 22;
             find_clock_override_reset reset;
             auto const epoch =
                 combdsl::detail::find_clock::time_point{};
             combdsl::detail::find_clock_now_override = [&] {
                 auto const before_deadline =
                     clock_calls++ < calls_before_deadline;
                 return before_deadline
                     ? epoch
                     : epoch + combdsl::detail::find_search_window;
             };
             std::array<combdsl::quoted_expression, 1> const catalog{
                 quote(A),
             };
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             auto const result =
                 combdsl::find_combinator_matches_among(
                     symbols, quote(A), catalog, false);
             std::cout << result.timed_out << ' '
                       << result.completed_sizes.size() << ' '
                       << (result.completed_sizes.size() == 1 &&
                           result.completed_sizes.front().size() == 1) << ' '
                       << clock_calls;
         },
         "0 1 1 17");
    test("find among public API rejects an empty catalog",
         [] {
             std::array<combdsl::quoted_atomic, 0> const symbols{};
             std::array<combdsl::quoted_expression, 0> const catalog{};
             try {
                 static_cast<void>(
                     combdsl::find_combinator_matches_among(
                         symbols, quote(A), catalog));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         "combdsl::find among catalog cannot be empty");
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
    test("find search catalog includes J and excludes Y",
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
         "10");
    std::array const identity_match_pairs{
        std::pair{quote(B), quote(I)},
        std::pair{quote(C), quote(T)},
        std::pair{quote(M), quote(I)},
        std::pair{quote(N), quote(K)},
        std::pair{quote(Q), quote(I)},
        std::pair{quote(W), quote(K)},
        std::pair{quote(W_star), quote(K)},
        std::pair{quote(Z), quote(I)},
    };
    test("fixed match pair exclusions include every atomic M pair",
         [] {
             std::size_t excluded_count = 0;
             auto const& combinators =
                 combdsl::detail::predefined_bird_combinators();
             bool all_atomic_m_pairs_excluded = true;
             for (auto const& function : combinators) {
                 for (auto const& argument : combinators) {
                     if (combdsl::detail::is_excluded_match_pair(
                             function, argument)) {
                         ++excluded_count;
                     }
                 }
             }
             for (auto const& argument : combinators) {
                 all_atomic_m_pairs_excluded =
                     all_atomic_m_pairs_excluded &&
                     combdsl::detail::is_excluded_match_pair(
                         quote(M), argument);
             }
             std::cout
                 << excluded_count << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(I), quote(A))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(I), quote(Z)) << ' '
                 << all_atomic_m_pairs_excluded
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(Y))
                 << !combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(A)(B)) << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(U), quote(M))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(U), quote(U))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(U), quote(A))
                 << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(B), quote(I))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(C), quote(T))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(I))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(N), quote(K))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(Q), quote(I))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(W), quote(K))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(W_star), quote(K))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(Z), quote(I))
                 << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(A), quote(I));
         },
         "69 11 111 110 11111111 0");
    test("Find prunes M and W applied to K-headed composites",
         [] {
             std::cout
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(K)(A))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(W), quote(K)(A))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(K)(quote(A)(B)))
                 << combdsl::detail::is_excluded_match_pair(
                        quote(W), quote(K)(quote(A)(B)))
                 << ' '
                 << !combdsl::detail::is_excluded_match_pair(
                        quote(M), quote(A)(B))
                 << !combdsl::detail::is_excluded_match_pair(
                        quote(W), quote(A)(B));
         },
         "1111 11");
    test("K-headed M and W exclusions preserve atomic and compound behavior",
         [] {
             std::array const captures{
                 quote(A), quote(A)(B)};
             for (auto const& capture : captures) {
                 auto const expected =
                     combdsl::detail::normalize_for_compare(
                         capture(x));
                 auto const mockingbird =
                     combdsl::detail::normalize_for_compare(
                         quote(M)(quote(K)(capture))(x));
                 auto const warbler =
                     combdsl::detail::normalize_for_compare(
                         quote(W)(quote(K)(capture))(x));
                 std::cout
                     << (expected && mockingbird &&
                         combdsl::detail::
                             same_parser_definition_expression(
                                 *expected, *mockingbird))
                     << (expected && warbler &&
                         combdsl::detail::
                             same_parser_definition_expression(
                                 *expected, *warbler));
             }
         },
         "1111");
    auto identity_trip_patterns = [](combdsl::quoted_expression capture) {
        return std::array{
            quote(B)(capture)(I),
            quote(C)(quote(C)(capture)),
            quote(C_star)(C)(capture),
            quote(C_star)(quote(C_star)(capture)),
            quote(C_star_star)(C_star)(capture),
            quote(G)(T)(capture),
            quote(Q)(capture)(I),
            quote(R)(capture)(T),
            quote(T)(capture)(I),
            quote(W1)(capture)(K),
            quote(Z)(C)(capture),
        };
    };
    test("Find prunes every structural identity trip for arbitrary captures",
         [&] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 for (auto const& pattern :
                      identity_trip_patterns(capture)) {
                     auto const* application =
                         combdsl::detail::takeout_application(pattern);
                     std::cout <<
                         (application != nullptr &&
                          combdsl::detail::is_excluded_match_pair(
                              application->function(),
                              application->argument()));
                 }
             }
         },
         "1111111111111111111111");
    test("Find retains near misses for structural identity trips",
         [] {
             std::array const near_misses{
                 quote(B)(A)(K),
                 quote(C)(quote(B)(A)),
                 quote(C_star)(B)(A),
                 quote(C_star)(quote(C)(A)),
                 quote(C_star_star)(C)(A),
                 quote(G)(I)(A),
                 quote(Q)(A)(K),
                 quote(R)(A)(I),
                 quote(T)(A)(K),
                 quote(W1)(A)(I),
                 quote(Z)(B)(A),
                 quote(Z)(D)(A),
             };
             for (auto const& pattern : near_misses) {
                 auto const* application =
                     combdsl::detail::takeout_application(pattern);
                 std::cout <<
                     (application != nullptr &&
                      !combdsl::detail::is_excluded_match_pair(
                          application->function(),
                          application->argument()));
             }
         },
         "111111111111");
    test("structural identity trip exclusions preserve semantics",
         [&] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 auto const expected =
                     combdsl::detail::normalize_for_compare(
                         capture(x)(y)(z)(w));
                 for (auto const& pattern :
                      identity_trip_patterns(capture)) {
                     auto const normalized =
                         combdsl::detail::normalize_for_compare(
                             pattern(x)(y)(z)(w));
                     std::cout <<
                         (expected && normalized &&
                          combdsl::detail::
                              same_parser_definition_expression(
                                  *expected, *normalized));
                 }
             }
         },
         "1111111111111111111111");
    test("Find retains Z C-star identity trips for arbitrary captures",
         [] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 auto const pattern = quote(Z)(C_star)(capture);
                 auto const* application =
                     combdsl::detail::takeout_application(pattern);
                 auto const expected =
                     combdsl::detail::normalize_for_compare(
                         capture(x)(y)(z)(w));
                 auto const normalized =
                     combdsl::detail::normalize_for_compare(
                         pattern(x)(y)(z)(w));
                 std::cout
                     << (application != nullptr &&
                         !combdsl::detail::is_excluded_match_pair(
                             application->function(),
                             application->argument()))
                     << (expected && normalized &&
                         combdsl::detail::
                             same_parser_definition_expression(
                                 *expected, *normalized));
             }
         },
         "1111");
    test("Find prunes G K trips for arbitrary captures only",
         [] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 auto const g_k = quote(G)(K)(capture);
                 auto const g_t = quote(G)(T)(capture);
                 auto const z_c_star = quote(Z)(C_star)(capture);
                 auto const* g_k_application =
                     combdsl::detail::takeout_application(g_k);
                 auto const* g_t_application =
                     combdsl::detail::takeout_application(g_t);
                 auto const* z_c_star_application =
                     combdsl::detail::takeout_application(z_c_star);
                 std::cout
                     << (g_k_application != nullptr &&
                         combdsl::detail::is_excluded_match_pair(
                             g_k_application->function(),
                             g_k_application->argument()))
                     << (g_t_application != nullptr &&
                         combdsl::detail::is_excluded_match_pair(
                             g_t_application->function(),
                             g_t_application->argument()))
                     << (z_c_star_application != nullptr &&
                         !combdsl::detail::is_excluded_match_pair(
                             z_c_star_application->function(),
                             z_c_star_application->argument()));
             }
         },
         "111111");
    test("G K trip exclusions remain extensionally K I",
         [] {
             auto const expected =
                 combdsl::detail::normalize_for_compare(
                     quote(K)(I)(x)(y));
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 auto const normalized =
                     combdsl::detail::normalize_for_compare(
                         quote(G)(K)(capture)(x)(y));
                 std::cout
                     << (expected && normalized &&
                         combdsl::detail::
                             same_parser_definition_expression(
                                 *expected, *normalized));
             }
         },
         "11");
    test("Find retains G K trip near misses and opposite association",
         [] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 std::array const near_misses{
                     quote(G)(capture)(K),
                     quote(G)(quote(K)(capture)),
                     quote(G)(B)(capture),
                 };
                 for (auto const& pattern : near_misses) {
                     auto const* application =
                         combdsl::detail::takeout_application(pattern);
                     std::cout
                         << (application != nullptr &&
                             !combdsl::detail::is_excluded_match_pair(
                                 application->function(),
                                 application->argument()));
                 }
             }
         },
         "111111");
    auto constant_identity_trip_patterns =
        [](combdsl::quoted_expression capture) {
            return std::array{
                quote(C)(K)(capture),
                quote(R)(capture)(K),
            };
        };
    test("Find prunes constant identity trips for arbitrary captures",
         [&] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 for (auto const& pattern :
                      constant_identity_trip_patterns(capture)) {
                     auto const* application =
                         combdsl::detail::takeout_application(pattern);
                     std::cout <<
                         (application != nullptr &&
                          combdsl::detail::is_excluded_match_pair(
                              application->function(),
                              application->argument()));
                 }
             }
         },
         "1111");
    test("Find retains constant identity trip near misses and associations",
         [] {
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 std::array const near_misses{
                     quote(C)(capture)(K),
                     quote(C)(quote(K)(capture)),
                     quote(R)(K)(capture),
                     quote(R)(capture(quote(K))),
                 };
                 for (auto const& pattern : near_misses) {
                     auto const* application =
                         combdsl::detail::takeout_application(pattern);
                     std::cout <<
                         (application != nullptr &&
                          !combdsl::detail::is_excluded_match_pair(
                              application->function(),
                              application->argument()));
                 }
             }
         },
         "11111111");
    test("constant identity trip exclusions remain extensionally I",
         [&] {
             auto const expected =
                 combdsl::detail::normalize_for_compare(quote(I)(x));
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 for (auto const& pattern :
                      constant_identity_trip_patterns(capture)) {
                     auto const normalized =
                         combdsl::detail::normalize_for_compare(
                             pattern(x));
                     std::cout <<
                         (expected && normalized &&
                          combdsl::detail::
                              same_parser_definition_expression(
                                  *expected, *normalized));
                 }
             }
         },
         "1111");
    test("excluded identity pairs still match I extensionally",
         [&] {
             std::array const symbols{
                 quoted_atomic{x},
                 quoted_atomic{y},
                 quoted_atomic{z},
                 quoted_atomic{w},
             };
             auto const target = quote(x)(y)(z)(w);
             for (auto const& [function, argument] :
                  identity_match_pairs) {
                 std::cout << combdsl::check_for_match(
                     function(argument), symbols, target);
             }
         },
         "11111111");
    test("compact W*K parses as W-star applied to K",
         [] {
             auto const compact = parse("W*K");
             auto const spaced = parse("W* K");
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        compact, quote(W_star)(K))
                 << combdsl::detail::same_parser_definition_expression(
                        compact, spaced)
                 << ' '
                 << combdsl::detail::is_excluded_match_pair(
                        quote(W_star), quote(K))
                 << ' ';
             compact.print_to(std::cout);
         },
         "11 1 W*K");
    test("find omits all eight identity pairs",
         [] {
             std::ostringstream output;
             parse("find all 2 ?xyzw = xyzw").print_to(output);
             auto const padded = '\n' + output.str() + '\n';
             auto const contains_line = [&](std::string_view line) {
                 return padded.find(
                     '\n' + std::string(line) + '\n') !=
                     std::string::npos;
             };
             std::cout
                 << contains_line("?=I")
                 << !contains_line("?=BI")
                 << !contains_line("?=CT")
                 << !contains_line("?=MI")
                 << !contains_line("?=NK")
                 << !contains_line("?=QI")
                 << !contains_line("?=WK")
                 << !contains_line("?=W*K")
                 << !contains_line("?=ZI");
         },
         "111111111");
    test("identity-pair exclusions apply in every nested shape",
         [&] {
             bool left_trip_excluded = true;
             bool right_trip_excluded = true;
             bool left_quad_pair_excluded = true;
             bool split_quad_left_pair_excluded = true;
             bool middle_quad_pair_left_excluded = true;
             bool middle_quad_pair_right_excluded = true;
             bool split_quad_right_pair_excluded = true;
             bool right_quad_pair_excluded = true;
             for (auto const& [function, argument] :
                  identity_match_pairs) {
                 auto const left_trip_mask =
                     combdsl::detail::predefined_bird_trip_shape_mask(
                         function, argument, quote(A));
                 auto const right_trip_mask =
                     combdsl::detail::predefined_bird_trip_shape_mask(
                         quote(A), function, argument);
                 left_trip_excluded = left_trip_excluded &&
                     (left_trip_mask & (std::uint8_t{1} << 0)) == 0;
                 right_trip_excluded = right_trip_excluded &&
                     (right_trip_mask & (std::uint8_t{1} << 1)) == 0;

                 auto const left_quad_mask =
                     combdsl::detail::predefined_bird_quad_shape_mask(
                         function, argument, quote(A), quote(A));
                 auto const middle_quad_mask =
                     combdsl::detail::predefined_bird_quad_shape_mask(
                         quote(A), function, argument, quote(A));
                 auto const right_quad_mask =
                     combdsl::detail::predefined_bird_quad_shape_mask(
                         quote(A), quote(A), function, argument);
                 left_quad_pair_excluded =
                     left_quad_pair_excluded &&
                     (left_quad_mask & (std::uint8_t{1} << 0)) == 0;
                 split_quad_left_pair_excluded =
                     split_quad_left_pair_excluded &&
                     (left_quad_mask & (std::uint8_t{1} << 1)) == 0;
                 middle_quad_pair_left_excluded =
                     middle_quad_pair_left_excluded &&
                     (middle_quad_mask & (std::uint8_t{1} << 2)) == 0;
                 middle_quad_pair_right_excluded =
                     middle_quad_pair_right_excluded &&
                     (middle_quad_mask & (std::uint8_t{1} << 3)) == 0;
                 split_quad_right_pair_excluded =
                     split_quad_right_pair_excluded &&
                     (right_quad_mask & (std::uint8_t{1} << 1)) == 0;
                 right_quad_pair_excluded =
                     right_quad_pair_excluded &&
                     (right_quad_mask & (std::uint8_t{1} << 4)) == 0;
             }
             std::cout
                 << left_trip_excluded
                 << right_trip_excluded << ' '
                 << left_quad_pair_excluded
                 << split_quad_left_pair_excluded
                 << middle_quad_pair_left_excluded
                 << middle_quad_pair_right_excluded
                 << split_quad_right_pair_excluded
                 << right_quad_pair_excluded;
         },
         "11 111111");
    test("M pruning keeps non-K composite arguments in nested shapes",
         [] {
             auto print_mask = [](std::uint8_t mask,
                                  std::size_t shape_count) {
                 for (std::size_t shape = 0;
                      shape < shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(M), quote(A), quote(B)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(A), quote(M), quote(B)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << " | ";
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(M), quote(A), quote(B), quote(C)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(A), quote(M), quote(B), quote(C)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(A), quote(B), quote(M), quote(C)),
                 combdsl::check_for_quads_match_shape_count);
         },
         "01 10 | 00111 11001 10110");
    test("K-headed M and W pruning reaches every nested shape",
         [] {
             auto print_mask = [](std::uint8_t mask,
                                  std::size_t shape_count) {
                 for (std::size_t shape = 0;
                      shape < shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(M), quote(K), quote(A)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(W), quote(K), quote(A)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(M), quote(K), quote(A)(B)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(W), quote(K), quote(A)(B)),
                 combdsl::check_for_trips_match_shape_count);
             std::cout << " | ";
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(M), quote(K), quote(A), quote(B)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(W), quote(K), quote(A), quote(B)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(A), quote(M), quote(K), quote(B)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(A), quote(W), quote(K), quote(B)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(M), quote(K), quote(A)(B), quote(C)),
                 combdsl::check_for_quads_match_shape_count);
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_quad_shape_mask(
                     quote(W), quote(K), quote(A)(B), quote(C)),
                 combdsl::check_for_quads_match_shape_count);
         },
         "00 00 00 00 | "
         "00000 00000 11000 11000 00000 00000");
    test("identity trip pruning preserves the opposite trip shape",
         [] {
             auto print_mask = [](std::uint8_t mask) {
                 for (std::size_t shape = 0;
                      shape <
                          combdsl::check_for_trips_match_shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             bool first_capture = true;
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 if (!first_capture) {
                     std::cout << " | ";
                 }
                 first_capture = false;
                 auto const cases = std::array{
                     std::array{quote(B), capture, quote(I)},
                     std::array{quote(C), quote(C), capture},
                     std::array{quote(C_star), quote(C), capture},
                     std::array{quote(C_star), quote(C_star), capture},
                     std::array{quote(C_star_star), quote(C_star), capture},
                     std::array{quote(G), quote(T), capture},
                     std::array{quote(Q), capture, quote(I)},
                     std::array{quote(R), capture, quote(T)},
                     std::array{quote(T), capture, quote(I)},
                     std::array{quote(W1), capture, quote(K)},
                     std::array{quote(Z), quote(C), capture},
                 };
                 bool first_case = true;
                 for (auto const& trip : cases) {
                     if (!first_case) {
                         std::cout << ' ';
                     }
                     first_case = false;
                     print_mask(
                         combdsl::detail::
                             predefined_bird_trip_shape_mask(
                                 trip[0], trip[1], trip[2]));
                 }
             }
         },
         "01 10 01 10 01 01 01 01 01 01 01 | "
         "01 10 01 10 01 01 01 01 01 01 01");
    test("Z C-star identity trips keep both trip associations",
         [] {
             auto print_mask = [](std::uint8_t mask) {
                 for (std::size_t shape = 0;
                      shape <
                          combdsl::check_for_trips_match_shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(Z), quote(C_star), quote(A)));
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(Z), quote(C_star), quote(A)(B)));
         },
         "11 11");
    test("G K pruning preserves the opposite trip association",
         [] {
             auto print_mask = [](std::uint8_t mask) {
                 for (std::size_t shape = 0;
                      shape <
                          combdsl::check_for_trips_match_shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(G), quote(K), quote(A)));
             std::cout << ' ';
             print_mask(
                 combdsl::detail::predefined_bird_trip_shape_mask(
                     quote(G), quote(K), quote(A)(B)));
         },
         "01 01");
    test("constant identity trip pruning preserves opposite associations",
         [] {
             auto print_mask = [](std::uint8_t mask) {
                 for (std::size_t shape = 0;
                      shape <
                          combdsl::check_for_trips_match_shape_count;
                      ++shape) {
                     std::cout <<
                         ((mask & (std::uint8_t{1} << shape)) != 0);
                 }
             };
             bool first_capture = true;
             for (auto const& capture :
                  std::array{quote(A), quote(A)(B)}) {
                 if (!first_capture) {
                     std::cout << " | ";
                 }
                 first_capture = false;
                 print_mask(
                     combdsl::detail::predefined_bird_trip_shape_mask(
                         quote(C), quote(K), capture));
                 std::cout << ' ';
                 print_mask(
                     combdsl::detail::predefined_bird_trip_shape_mask(
                         quote(R), capture, quote(K)));
             }
         },
         "01 01 | 01 01");
    std::vector<combdsl::quoted_expression> first_pairs;
    first_pairs.reserve(2);
    std::size_t generated_pair_count = 0;
    bool generated_m_headed_pair = false;
    combdsl::detail::for_each_predefined_bird_pair(
        [&](combdsl::quoted_expression pair) {
            auto const* application =
                combdsl::detail::takeout_application(pair);
            generated_m_headed_pair =
                generated_m_headed_pair ||
                (application != nullptr &&
                 combdsl::detail::same_parser_definition_expression(
                     application->function(), quote(M)));
            if (first_pairs.size() < 2) {
                first_pairs.push_back(std::move(pair));
            }
            ++generated_pair_count;
        });
    test("pair generator skips fixed terrible twos",
         [&] {
             std::cout << generated_pair_count << ' '
                       << generated_m_headed_pair << ' ';
             if (first_pairs.size() >= 2) {
                 first_pairs[0].print_to(std::cout);
                 std::cout << ' ';
                 first_pairs[1].print_to(std::cout);
             } else {
                 std::cout << first_pairs.size();
             }
         },
         "831 0 AA AB");
    auto const j_pair_matches =
        combdsl::check_for_pairs_match(
            j_match_symbols,
            j_match_expression);
    test("pair matching searches 831 ordered pairs without Y",
         [&] {
             std::cout << j_pair_matches.size();
         },
         "0");
    test("restricted Find keeps untargeted M composites",
         [] {
             std::array const catalog{quote(M), quote(A)};
             std::size_t pair_count = 0;
             std::size_t trip_count = 0;
             bool contains_flat_m_a = false;
             bool contains_m_composite = false;
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             static_cast<void>(combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     2,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         ++pair_count;
                         contains_flat_m_a =
                             contains_flat_m_a ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(M)(A));
                         return true;
                     }));
             static_cast<void>(combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         ++trip_count;
                         contains_m_composite =
                             contains_m_composite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(M)(quote(A)(A)));
                         return true;
                     }));
             std::cout << pair_count << ' ' << trip_count << ' '
                       << contains_flat_m_a << ' '
                       << contains_m_composite;
         },
         "2 8 0 1");
    test("restricted Find prunes K-headed M and W composites",
         [] {
             std::array const catalog{
                 quote(M), quote(W), quote(K), quote(A)};
             std::size_t trip_count = 0;
             bool contains_m_k_a = false;
             bool contains_w_k_a = false;
             bool contains_saturated_k = false;
             bool contains_m_k_composite = false;
             bool contains_w_k_composite = false;
             bool contains_saturated_k_composite = false;
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             static_cast<void>(combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         ++trip_count;
                         contains_m_k_a = contains_m_k_a ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(M)(quote(K)(A)));
                         contains_w_k_a = contains_w_k_a ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(W)(quote(K)(A)));
                         contains_saturated_k =
                             contains_saturated_k ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(K)(A)(A));
                         return true;
                     }));
             static_cast<void>(combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     4,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         contains_m_k_composite =
                             contains_m_k_composite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(M)(quote(K)(
                                         quote(A)(A))));
                         contains_w_k_composite =
                             contains_w_k_composite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(W)(quote(K)(
                                         quote(A)(A))));
                         contains_saturated_k_composite =
                             contains_saturated_k_composite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(K)(quote(A)(A))(A));
                         return true;
                     }));
             std::cout << trip_count << ' '
                       << contains_m_k_a
                       << contains_w_k_a
                       << contains_saturated_k << ' '
                       << contains_m_k_composite
                       << contains_w_k_composite
                       << contains_saturated_k_composite;
         },
         "80 001 001");
    test("restricted Find prunes structural identity trips at every size",
         [&] {
             std::array const catalog{
                 quote(A), quote(B), quote(C), quote(C_star),
                 quote(C_star_star), quote(G), quote(I), quote(K),
                 quote(Q), quote(R), quote(T), quote(W1), quote(Z)};
             auto const atomic_patterns =
                 identity_trip_patterns(quote(A));
             auto const compound_patterns =
                 identity_trip_patterns(quote(A)(A));
             std::array<bool, atomic_patterns.size()> found_atomic{};
             std::array<bool, compound_patterns.size()> found_compound{};
             bool found_atomic_positive = false;
             bool found_compound_positive = false;
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             auto const atomic_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         for (std::size_t index = 0;
                              index < atomic_patterns.size();
                              ++index) {
                             found_atomic[index] =
                                 found_atomic[index] ||
                                 combdsl::detail::
                                     same_parser_definition_expression(
                                         candidate,
                                         atomic_patterns[index]);
                         }
                         found_atomic_positive =
                             found_atomic_positive ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(K)(A)(A));
                         return true;
                     });
             auto const compound_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     4,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         for (std::size_t index = 0;
                              index < compound_patterns.size();
                              ++index) {
                             found_compound[index] =
                                 found_compound[index] ||
                                 combdsl::detail::
                                     same_parser_definition_expression(
                                         candidate,
                                         compound_patterns[index]);
                         }
                         found_compound_positive =
                             found_compound_positive ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(K)(quote(A)(A))(A));
                         return true;
                     });
             std::cout
                 << (atomic_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << std::ranges::any_of(found_atomic, std::identity{})
                 << found_atomic_positive << ' '
                 << (compound_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << std::ranges::any_of(found_compound, std::identity{})
                 << found_compound_positive;
         },
         "101 101");
    test("restricted Find keeps Z C-star while pruning Z C",
         [] {
             std::array const catalog{
                 quote(A), quote(C), quote(C_star), quote(Z)};
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             bool found_atomic_z_c = false;
             bool found_atomic_z_c_star = false;
             auto const atomic_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         found_atomic_z_c = found_atomic_z_c ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(Z)(C)(A));
                         found_atomic_z_c_star =
                             found_atomic_z_c_star ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(Z)(C_star)(A));
                         return true;
                     });
             bool found_compound_z_c = false;
             bool found_compound_z_c_star = false;
             auto const compound_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     4,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         found_compound_z_c = found_compound_z_c ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(Z)(C)(quote(A)(A)));
                         found_compound_z_c_star =
                             found_compound_z_c_star ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(Z)(C_star)(
                                         quote(A)(A)));
                         return true;
                     });
             std::cout
                 << (atomic_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << found_atomic_z_c
                 << found_atomic_z_c_star << ' '
                 << (compound_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << found_compound_z_c
                 << found_compound_z_c_star;
         },
         "101 101");
    test("restricted Find prunes G K while retaining its association",
         [] {
             std::array const catalog{
                 quote(A), quote(G), quote(I), quote(K)};
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             bool found_atomic_g_k = false;
             bool found_atomic_opposite = false;
             bool found_atomic_control = false;
             auto const atomic_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         found_atomic_g_k = found_atomic_g_k ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(G)(K)(A));
                         found_atomic_opposite =
                             found_atomic_opposite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(G)(quote(K)(A)));
                         found_atomic_control =
                             found_atomic_control ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate, quote(A)(I)(K));
                         return true;
                     });
             bool found_compound_g_k = false;
             bool found_compound_opposite = false;
             bool found_compound_control = false;
             auto const compound_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     4,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         found_compound_g_k = found_compound_g_k ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(G)(K)(quote(A)(A)));
                         found_compound_opposite =
                             found_compound_opposite ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(G)(quote(K)(
                                         quote(A)(A))));
                         found_compound_control =
                             found_compound_control ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(K)(quote(K)(I))(A));
                         return true;
                     });
             std::cout
                 << (atomic_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << found_atomic_g_k
                 << found_atomic_opposite
                 << found_atomic_control << ' '
                 << (compound_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << found_compound_g_k
                 << found_compound_opposite
                 << found_compound_control;
         },
         "1011 1011");
    test("restricted Find prunes constant identity trips at every size",
         [&] {
             std::array const catalog{
                 quote(A), quote(C), quote(K), quote(R)};
             auto const atomic_patterns =
                 constant_identity_trip_patterns(quote(A));
             auto const compound_patterns =
                 constant_identity_trip_patterns(quote(A)(A));
             std::array<bool, atomic_patterns.size()> found_atomic{};
             std::array<bool, compound_patterns.size()> found_compound{};
             std::array<bool, 2> found_atomic_opposite{};
             std::array<bool, 2> found_compound_opposite{};
             auto const deadline =
                 combdsl::detail::find_clock::time_point::max();
             auto const atomic_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     3,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         for (std::size_t index = 0;
                              index < atomic_patterns.size();
                              ++index) {
                             found_atomic[index] =
                                 found_atomic[index] ||
                                 combdsl::detail::
                                     same_parser_definition_expression(
                                         candidate,
                                         atomic_patterns[index]);
                         }
                         found_atomic_opposite[0] =
                             found_atomic_opposite[0] ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(C)(quote(K)(A)));
                         found_atomic_opposite[1] =
                             found_atomic_opposite[1] ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(R)(quote(A)(K)));
                         return true;
                     });
             auto const compound_status = combdsl::detail::
                 for_each_catalog_candidate_of_size(
                     4,
                     std::span<combdsl::quoted_expression const>{catalog},
                     deadline,
                     [&](combdsl::quoted_expression candidate) {
                         for (std::size_t index = 0;
                              index < compound_patterns.size();
                              ++index) {
                             found_compound[index] =
                                 found_compound[index] ||
                                 combdsl::detail::
                                     same_parser_definition_expression(
                                         candidate,
                                         compound_patterns[index]);
                         }
                         found_compound_opposite[0] =
                             found_compound_opposite[0] ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(C)(quote(K)(
                                         quote(A)(A))));
                         found_compound_opposite[1] =
                             found_compound_opposite[1] ||
                             combdsl::detail::
                                 same_parser_definition_expression(
                                     candidate,
                                     quote(R)(
                                         quote(A)(A)(K)));
                         return true;
                     });
             std::cout
                 << (atomic_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << std::ranges::any_of(found_atomic, std::identity{})
                 << std::ranges::all_of(
                        found_atomic_opposite, std::identity{}) << ' '
                 << (compound_status == combdsl::detail::
                         catalog_find_enumeration_status::completed)
                 << std::ranges::any_of(found_compound, std::identity{})
                 << std::ranges::all_of(
                        found_compound_opposite, std::identity{});
         },
         "101 101");
    test("native restricted Find omits M applied to an atomic bird",
         parse("find among M A ?xyz = MAxyz"),
         "?=AA");
    std::vector<combdsl::quoted_expression> first_trips;
    first_trips.reserve(2);
    std::size_t generated_trip_count = 0;
    bool generated_saturated_k = false;
    bool generated_right_partial_k = false;
    bool generated_left_sk = false;
    bool generated_right_sk = false;
    bool generated_right_identity = false;
    bool generated_left_flat_m = false;
    bool generated_right_flat_m = false;
    bool generated_right_composite_m = false;
    bool generated_right_m_k_a = false;
    bool generated_right_w_k_a = false;
    auto const excluded_identity_trips =
        identity_trip_patterns(quote(A));
    auto const excluded_constant_identity_trips =
        constant_identity_trip_patterns(quote(A));
    bool generated_excluded_identity_trip = false;
    bool generated_excluded_constant_identity_trip = false;
    bool generated_z_c_trip = false;
    bool generated_z_c_star_trip = false;
    bool generated_g_k_trip = false;
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
            generated_left_flat_m =
                generated_left_flat_m ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(M)(A)(B));
            generated_right_flat_m =
                generated_right_flat_m ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(A)(quote(M)(B)));
            generated_right_composite_m =
                generated_right_composite_m ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(M)(quote(A)(B)));
            generated_right_m_k_a =
                generated_right_m_k_a ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(M)(quote(K)(A)));
            generated_right_w_k_a =
                generated_right_w_k_a ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(W)(quote(K)(A)));
            generated_excluded_identity_trip =
                generated_excluded_identity_trip ||
                std::ranges::any_of(
                    excluded_identity_trips,
                    [&](auto const& excluded) {
                        return combdsl::detail::
                            same_parser_definition_expression(
                                trip, excluded);
                    });
            generated_excluded_constant_identity_trip =
                generated_excluded_constant_identity_trip ||
                std::ranges::any_of(
                    excluded_constant_identity_trips,
                    [&](auto const& excluded) {
                        return combdsl::detail::
                            same_parser_definition_expression(
                                trip, excluded);
                    });
            generated_z_c_trip = generated_z_c_trip ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(Z)(C)(A));
            generated_z_c_star_trip = generated_z_c_star_trip ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(Z)(C_star)(A));
            generated_g_k_trip = generated_g_k_trip ||
                combdsl::detail::same_parser_definition_expression(
                    trip, quote(G)(K)(A));
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
                       << generated_right_identity << ' '
                       << generated_left_flat_m
                       << generated_right_flat_m
                       << generated_right_composite_m << ' '
                       << generated_right_m_k_a
                       << generated_right_w_k_a << ' '
                       << generated_excluded_identity_trip
                       << generated_excluded_constant_identity_trip << ' '
                       << generated_z_c_trip
                       << generated_z_c_star_trip << ' '
                       << generated_g_k_trip
                       << ' ';
             if (first_trips.size() >= 2) {
                 first_trips[0].print_to(std::cout);
                 std::cout << ' ';
                 first_trips[1].print_to(std::cout);
             } else {
                 std::cout << first_trips.size();
             }
         },
         "47622 01010 001 00 00 01 0 AAA A(AA)");
    constexpr auto trip_candidate_slot_count =
        combdsl::check_for_match_combinator_count *
        combdsl::check_for_match_combinator_count *
        combdsl::check_for_match_combinator_count *
        combdsl::check_for_trips_match_shape_count;
    std::vector<bool> trip_candidate_slots(
        trip_candidate_slot_count);
    std::size_t trip_column_candidate_count = 0;
    std::size_t nonempty_trip_column_count = 0;
    bool duplicate_trip_column_candidate = false;
    for (std::size_t column_index = 0;
         column_index <
             combdsl::check_for_trips_match_column_count;
         ++column_index) {
        auto const count_before_column =
            trip_column_candidate_count;
        combdsl::detail::for_each_predefined_bird_trip_column_at(
            column_index,
            [&](std::size_t candidate_index,
                combdsl::quoted_expression) {
                duplicate_trip_column_candidate =
                    duplicate_trip_column_candidate ||
                    candidate_index >= trip_candidate_slots.size() ||
                    trip_candidate_slots[candidate_index];
                if (candidate_index < trip_candidate_slots.size()) {
                    trip_candidate_slots[candidate_index] = true;
                }
                ++trip_column_candidate_count;
            });
        nonempty_trip_column_count +=
            trip_column_candidate_count != count_before_column;
    }
    test("trip columns cover every candidate exactly once",
         [&] {
             std::cout << trip_column_candidate_count << ' '
                       << nonempty_trip_column_count << ' '
                       << duplicate_trip_column_candidate;
         },
         "47622 1674 0");
#if !defined(__EMSCRIPTEN__)
    test("native find dispatch uses worker threads",
         [] {
             constexpr std::size_t job_count = 64;
             std::array<std::atomic<unsigned>, job_count>
                 seen{};
             std::latch first_workers_started{3};
             std::mutex worker_threads_mutex;
             std::vector<std::thread::id> worker_threads;
             auto const producer_thread =
                 std::this_thread::get_id();
             std::atomic<bool> ran_on_producer = false;
             combdsl::detail::dispatch_native_find_work<
                 std::size_t>(
                 job_count,
                 [&](auto&& submit) {
                     for (std::size_t job = 0;
                          job < job_count;
                          ++job) {
                         if (!submit(job)) {
                             break;
                         }
                     }
                 },
                 [&](std::size_t job) {
                     bool first_job_for_worker = false;
                     {
                         std::scoped_lock lock(
                             worker_threads_mutex);
                         auto const worker_thread =
                             std::this_thread::get_id();
                         if (std::find(
                                 worker_threads.begin(),
                                 worker_threads.end(),
                                 worker_thread) ==
                             worker_threads.end()) {
                             worker_threads.push_back(
                                 worker_thread);
                             first_job_for_worker = true;
                         }
                     }
                     if (first_job_for_worker) {
                         first_workers_started.count_down();
                     }
                     first_workers_started.wait();
                     if (std::this_thread::get_id() ==
                         producer_thread) {
                         ran_on_producer.store(true);
                     }
                     seen[job].fetch_add(1);
                 },
                 4);
             std::size_t processed = 0;
             bool exactly_once = true;
             for (auto const& count : seen) {
                 auto const value = count.load();
                 processed += value;
                 exactly_once = exactly_once && value == 1;
             }
             std::cout << processed << ' '
                       << exactly_once << ' '
                       << !ran_on_producer.load() << ' '
                       << worker_threads.size();
         },
         "64 1 1 3");
    test("native find dispatch repeatedly prefills busy worker slot",
         [] {
             constexpr std::size_t job_count = 1024;
             std::array<std::atomic<unsigned>, job_count>
                 seen{};
             std::binary_semaphore processing{0};
             std::binary_semaphore successor_submitted{0};
             std::atomic<bool> all_prefilled = true;
             combdsl::detail::dispatch_native_find_work<
                 std::size_t>(
                 job_count,
                 [&](auto&& submit) {
                     if (!submit(0)) {
                         all_prefilled.store(false);
                         return;
                     }
                     for (std::size_t job = 1;
                          job < job_count;
                          ++job) {
                         processing.acquire();
                         if (!submit(job)) {
                             all_prefilled.store(false);
                             return;
                         }
                         successor_submitted.release();
                     }
                 },
                 [&](std::size_t job) {
                     if (job + 1 < job_count) {
                         processing.release();
                         if (!successor_submitted
                                 .try_acquire_for(
                                     std::chrono::seconds{2})) {
                             all_prefilled.store(false);
                         }
                     }
                     seen[job].fetch_add(1);
                 },
                 2);
             bool exactly_once = true;
             for (auto const& count : seen) {
                 exactly_once = exactly_once &&
                     count.load() == 1;
             }
             std::cout << all_prefilled.load() << ' '
                       << exactly_once;
         },
         "1 1");
    test("native find dispatch propagates worker exceptions",
         [] {
             bool all_caught = true;
             for (std::size_t repetition = 0;
                  repetition < 16;
                  ++repetition) {
                 try {
                     combdsl::detail::dispatch_native_find_work<
                         std::size_t>(
                         16,
                         [&](auto&& submit) {
                             for (std::size_t job = 0;
                                  job < 16;
                                  ++job) {
                                 if (!submit(job)) {
                                     break;
                                 }
                             }
                         },
                         [](std::size_t job) {
                             if (job == 3) {
                                 throw std::runtime_error(
                                     "worker failure");
                             }
                         },
                         4);
                     all_caught = false;
                 } catch (std::runtime_error const& error) {
                     all_caught = all_caught &&
                         std::string_view(error.what()) ==
                             "worker failure";
                 }
             }
             std::cout << all_caught;
         },
         "1");
    test("native find dispatch drops prefetched work on failure",
         [] {
             std::latch first_work_started{1};
             std::binary_semaphore second_work_submitted{0};
             std::atomic<bool> prefilled = false;
             std::atomic<bool> second_work_processed = false;
             bool second_work_accepted = false;
             bool failure_caught = false;
             try {
                 combdsl::detail::dispatch_native_find_work<
                     std::size_t>(
                     2,
                     [&](auto&& submit) {
                         static_cast<void>(submit(0));
                         first_work_started.wait();
                         second_work_accepted = submit(1);
                         if (second_work_accepted) {
                             second_work_submitted.release();
                         }
                     },
                     [&](std::size_t work) {
                         if (work == 0) {
                             first_work_started.count_down();
                             prefilled.store(
                                 second_work_submitted
                                     .try_acquire_for(
                                         std::chrono::seconds{2}));
                             throw std::runtime_error(
                                 "prefilled worker failure");
                         }
                         second_work_processed.store(true);
                     },
                     2);
             } catch (std::runtime_error const& error) {
                 failure_caught =
                     std::string_view(error.what()) ==
                     "prefilled worker failure";
             }
             std::cout << prefilled.load() << ' '
                       << second_work_accepted << ' '
                       << !second_work_processed.load() << ' '
                       << failure_caught;
         },
         "1 1 1 1");
    test("native find dispatch propagates generator exceptions",
         [] {
             try {
                 combdsl::detail::dispatch_native_find_work<
                     std::size_t>(
                     4,
                     [](auto&& submit) {
                         static_cast<void>(submit(0));
                         throw std::runtime_error(
                             "generator failure");
                     },
                     [](std::size_t) {},
                     4);
             } catch (std::runtime_error const& error) {
                 std::cout << error.what();
             }
         },
         "generator failure");
    test("native find dispatch reports final worker failure",
         [] {
             std::latch worker_started{1};
             std::atomic<bool> release_worker = false;
             try {
                 combdsl::detail::dispatch_native_find_work<
                     std::size_t>(
                     1,
                     [&](auto&& submit) {
                         static_cast<void>(submit(0));
                         worker_started.wait();
                         release_worker.store(true);
                     },
                     [&](std::size_t) {
                         worker_started.count_down();
                         while (!release_worker.load()) {
                             std::this_thread::yield();
                         }
                         throw std::runtime_error(
                             "final worker failure");
                     },
                     2);
             } catch (std::runtime_error const& error) {
                 std::cout << error.what();
             }
         },
         "final worker failure");
    test("native find dispatch survives repeated wakeup races",
         [] {
             constexpr std::size_t repetition_count = 64;
             constexpr std::size_t job_count = 32;
             bool exactly_once = true;
             for (std::size_t repetition = 0;
                  repetition < repetition_count;
                  ++repetition) {
                 std::array<std::atomic<unsigned>, job_count>
                     seen{};
                 std::latch first_batch_started{4};
                 std::mutex worker_threads_mutex;
                 std::vector<std::thread::id> worker_threads;
                 combdsl::detail::dispatch_native_find_work<
                     std::size_t>(
                     job_count,
                     [&](auto&& submit) {
                         for (std::size_t job = 0;
                              job < job_count;
                              ++job) {
                             if (!submit(job)) {
                                 break;
                             }
                         }
                     },
                     [&](std::size_t job) {
                         bool first_job_for_worker = false;
                         {
                             std::scoped_lock lock(
                                 worker_threads_mutex);
                             auto const worker_thread =
                                 std::this_thread::get_id();
                             if (std::find(
                                     worker_threads.begin(),
                                     worker_threads.end(),
                                     worker_thread) ==
                                 worker_threads.end()) {
                                 worker_threads.push_back(
                                     worker_thread);
                                 first_job_for_worker = true;
                             }
                         }
                         if (first_job_for_worker) {
                             first_batch_started.count_down();
                         }
                         first_batch_started.wait();
                         if (
                             (job + repetition) % 3 == 0) {
                             std::this_thread::yield();
                         }
                         seen[job].fetch_add(1);
                     },
                     5);
                 for (auto const& count : seen) {
                     exactly_once = exactly_once &&
                         count.load() == 1;
                 }
             }
             std::cout << exactly_once;
         },
         "1");
#endif
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
         "26 AAA A(AA)");
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
    constexpr std::size_t b_index = 1;
    constexpr std::size_t c_index = 2;
    constexpr std::size_t c_star_index = 3;
    constexpr std::size_t c_star_star_index = 4;
    constexpr std::size_t e_index = 6;
    constexpr std::size_t g_index = 8;
    constexpr std::size_t h_index = 9;
    constexpr std::size_t i_index = 10;
    constexpr std::size_t k_index = 12;
    constexpr std::size_t l_index = 13;
    constexpr std::size_t m_index = 14;
    constexpr std::size_t q_index = 17;
    constexpr std::size_t r_index = 20;
    constexpr std::size_t s_index = 21;
    constexpr std::size_t t_index = 22;
    constexpr std::size_t u_index = 23;
    constexpr std::size_t w_index = 25;
    constexpr std::size_t w1_index = 28;
    constexpr std::size_t z_index = 29;
    constexpr auto combinator_count =
        combdsl::check_for_match_combinator_count;
    auto const l_question_k_s_column_index =
        ((l_index * combinator_count + k_index) *
             combinator_count +
         s_index) *
            combdsl::check_for_quads_match_shape_count +
        2;
    std::vector<combdsl::quoted_expression>
        l_question_k_s_column;
    std::vector<std::size_t> l_question_k_s_indices;
    bool l_question_k_s_contains_i = false;
    bool l_question_k_s_matches_tuple_generator = true;
    combdsl::detail::for_each_predefined_bird_quad_column_at(
        l_question_k_s_column_index,
        [&](std::size_t candidate_index,
            combdsl::quoted_expression quad) {
            l_question_k_s_contains_i =
                l_question_k_s_contains_i ||
                combdsl::detail::same_parser_definition_expression(
                    quad,
                    quote(L)(quote(I)(K))(S));
            auto const tuple_index =
                candidate_index /
                combdsl::check_for_quads_match_shape_count;
            auto const shape_index =
                candidate_index %
                combdsl::check_for_quads_match_shape_count;
            bool found_same_tuple_candidate = false;
            combdsl::detail::for_each_predefined_bird_quad_at(
                tuple_index,
                [&](std::size_t tuple_shape_index,
                    combdsl::quoted_expression tuple_quad) {
                    if (tuple_shape_index == shape_index) {
                        found_same_tuple_candidate =
                            combdsl::detail::
                                same_parser_definition_expression(
                                    quad, tuple_quad);
                    }
                });
            l_question_k_s_matches_tuple_generator =
                l_question_k_s_matches_tuple_generator &&
                found_same_tuple_candidate;
            l_question_k_s_indices.push_back(candidate_index);
            l_question_k_s_column.push_back(std::move(quad));
        });
    test("quad column L(?K)S varies all eligible birds",
         [&] {
             std::cout << l_question_k_s_column.size() << ' '
                       << l_question_k_s_contains_i << ' '
                       << std::ranges::is_sorted(
                              l_question_k_s_indices) << ' '
                       << l_question_k_s_matches_tuple_generator;
             if (!l_question_k_s_column.empty()) {
                 std::cout << ' ';
                 l_question_k_s_column.front().print_to(std::cout);
                 std::cout << ' ';
                 l_question_k_s_column.back().print_to(std::cout);
             }
         },
         "25 0 1 1 L(AK)S L(ZK)S");
    test("quad generator prunes K-headed M and W applications",
         [&] {
             auto print_presence = [](auto const& presence) {
                 for (auto const present : presence) {
                     std::cout << present;
                 }
             };
             print_presence(quad_shape_presence(
                 m_index, k_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 w_index, k_index, a_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, m_index, k_index, a_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 a_index, w_index, k_index, a_index));
         },
         "00000 00000 11000 11000");
    test("quad generator prunes every nested structural identity trip",
         [&] {
             std::array const middle_capture_cases{
                 std::pair{b_index, i_index},
                 std::pair{q_index, i_index},
                 std::pair{r_index, t_index},
                 std::pair{t_index, i_index},
                 std::pair{w1_index, k_index},
             };
             bool middle_capture_pruned = true;
             for (auto const [first, terminal] :
                  middle_capture_cases) {
                 middle_capture_pruned =
                     middle_capture_pruned &&
                     !quad_shape_presence(
                         first, a_index, terminal, b_index)[0] &&
                     !quad_shape_presence(
                         first, a_index, b_index, terminal)[2] &&
                     !quad_shape_presence(
                         b_index, first, a_index, terminal)[3];
             }

             std::array const last_capture_cases{
                 std::pair{c_star_index, c_index},
                 std::pair{c_star_star_index, c_star_index},
                 std::pair{g_index, t_index},
                 std::pair{z_index, c_index},
             };
             bool last_capture_pruned = true;
             for (auto const [first, second] :
                  last_capture_cases) {
                 auto const leading = quad_shape_presence(
                     first, second, a_index, b_index);
                 last_capture_pruned =
                     last_capture_pruned &&
                     !leading[0] && !leading[1] &&
                     !quad_shape_presence(
                         b_index, first, second, a_index)[3];
             }

             std::array const right_capture_cases{
                 std::pair{c_index, c_index},
                 std::pair{c_star_index, c_star_index},
             };
             bool right_capture_pruned = true;
             for (auto const [first, second] :
                  right_capture_cases) {
                 auto const leading = quad_shape_presence(
                     first, second, a_index, b_index);
                 right_capture_pruned =
                     right_capture_pruned &&
                     !leading[2] && !leading[4] &&
                     !quad_shape_presence(
                         b_index, first, second, a_index)[4];
             }

             std::cout
                 << middle_capture_cases.size() << ':'
                 << middle_capture_pruned << ' '
                 << last_capture_cases.size() << ':'
                 << last_capture_pruned << ' '
                 << right_capture_cases.size() << ':'
                 << right_capture_pruned;
         },
         "5:1 4:1 2:1");
    test("quad generator keeps Z C-star identity trips in every shape",
         [&] {
             auto const direct = quad_shape_presence(
                 z_index, c_star_index, a_index, b_index);
             auto const nested = quad_shape_presence(
                 b_index, z_index, c_star_index, a_index);
             std::cout
                 << direct[0] << direct[1] << ' '
                 << nested[3];
         },
         "11 1");
    test("quad generator prunes G K trips in every affected shape",
         [&] {
             auto print_presence = [](auto const& presence) {
                 for (auto const present : presence) {
                     std::cout << present;
                 }
             };
             print_presence(quad_shape_presence(
                 g_index, k_index, a_index, b_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 b_index, g_index, k_index, a_index));
         },
         "00101 11101");
    test("quad generator prunes constant identity trips in every shape",
         [&] {
             auto print_presence = [](auto const& presence) {
                 for (auto const present : presence) {
                     std::cout << present;
                 }
             };
             print_presence(quad_shape_presence(
                 c_index, k_index, a_index, b_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 b_index, c_index, k_index, a_index));
             std::cout << " | ";
             print_presence(quad_shape_presence(
                 r_index, a_index, k_index, b_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 r_index, a_index, b_index, k_index));
             std::cout << ' ';
             print_presence(quad_shape_presence(
                 b_index, r_index, a_index, k_index));
         },
         "00101 11101 | 01111 11011 11101");
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
         "00001 10000 10110 | "
         "00111 11001 10110 | "
         "00001 10000 10110 | "
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
        combdsl::detail::basis_label("X"));
    test("recursive function atom prints like its name",
         recursive_x, "X");
    test("takeout matches the same recursive function atom",
         takeout(quoted_atomic{recursive_x}, recursive_x), "I");
    test("recursive function atom does not match a symbol",
         takeout(quoted_atomic{recursive_x}, quote(x)), "Kx");
    test("symbol atom does not match a recursive function",
         takeout(quoted_atomic{x}, recursive_x), "KX");
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
    test("eval stops below the reductions needed for normal form",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(3),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 },
                 2);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::step_limit_reached) << '/';
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/12/Ix\n");
    test("read-only redex probing agrees with reduction",
         [] {
             std::vector<combdsl::quoted_expression> expressions;
             for (auto source : {
                      "x", "Ix", "Kx", "Kxy", "Sxy", "Sxyz",
                      "SKx", "YI", "M", "Mx", "K(Ix)",
                      "I(K(Ix))"}) {
                 expressions.push_back(parse(source));
             }
             expressions.push_back(single_step(parse("YI")));
             expressions.push_back(
                 combdsl::detail::make_quoted_pending_sk(
                     parse("Ix")));

             bool agrees = true;
             for (bool basis_step : {false, true}) {
                 for (bool reduce_partial_k_argument : {false, true}) {
                     combdsl::detail::reduction_options const options{
                         .basis_step = basis_step,
                         .reduce_partial_k_argument =
                             reduce_partial_k_argument,
                     };
                     for (auto const& expression : expressions) {
                         auto const probed =
                             combdsl::detail::has_next_redex(
                                 expression, options);
                         auto const reduced =
                             combdsl::detail::reduce_next_redex(
                                 expression, options).has_value();
                         agrees = agrees && probed == reduced;
                     }
                 }
             }
             std::cout << agrees;
         },
         "1");
    test("eval completes at the exact step limit",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(3),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 },
                 3);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/123/x\n");
    test("eval resets its step limit and resumes the same expression",
         [] {
             std::vector<std::size_t> reports;
             std::vector<std::size_t> pauses;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(5),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 },
                 2,
                 [&pauses](std::size_t reductions) {
                     pauses.push_back(reductions);
                     return true;
                 });
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/';
             for (auto reductions : pauses) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/12345/22/I(I(Ix))\nIx\nx\n");
    test("eval can cancel at an interactive step limit",
         [] {
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(5),
                 output,
                 input,
                 false,
                 evaluation_progress_callback{},
                 2,
                 [](std::size_t) { return false; });
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::cancelled) << '/'
                       << output.str();
         },
         "1/I(I(Ix))\n");
    test("zero step limit grants one reduction after each resume",
         [] {
             std::vector<std::size_t> pauses;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(2),
                 output,
                 input,
                 false,
                 evaluation_progress_callback{},
                 0,
                 [&pauses](std::size_t reductions) {
                     pauses.push_back(reductions);
                     return true;
                 });
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : pauses) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/01/I(Ix)\nIx\nx\n");
    test("zero step limit leaves a reducible expression unchanged",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(2),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 },
                 0);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::step_limit_reached) << '/'
                       << reports.size() << '/' << output.str();
         },
         "1/0/I(Ix)\n");
    test("zero step limit still recognizes normal form",
         [] {
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 quote(x), output, input, false,
                 evaluation_progress_callback{}, 0);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/'
                       << output.str();
         },
         "1/x\n");
    test("off step limit permits unrestricted evaluation",
         [] {
             std::vector<std::size_t> reports;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome = combdsl::eval_with_outcome(
                 repeated_identity_expression(2),
                 output,
                 input,
                 false,
                 [&reports](std::size_t reductions) {
                     reports.push_back(reductions);
                 },
                 std::nullopt);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : reports) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/12/x\n");
    test("step count resets for each evaluation",
         [] {
             auto run = [] {
                 std::istringstream input;
                 std::ostringstream output;
                 auto const outcome = combdsl::eval_with_outcome(
                     repeated_identity_expression(2),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     1);
                 return std::pair{outcome, output.str()};
             };
             auto const first = run();
             auto const second = run();
             std::cout << (first.first ==
                 combdsl::evaluation_outcome::step_limit_reached)
                       << (second.first ==
                 combdsl::evaluation_outcome::step_limit_reached)
                       << '/' << first.second << second.second;
         },
         "11/Ix\nIx\n");
    test("single step run stops cleanly at its step limit",
         [] {
             std::istringstream input;
             std::ostringstream output;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     repeated_identity_expression(3),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     2);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::step_limit_reached) << '/'
                       << output.str();
         },
         "1/I(Ix)\nIx\n");
    test("single step run completes at the exact step limit",
         [] {
             std::istringstream input;
             std::ostringstream output;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     repeated_identity_expression(3),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     3);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/'
                       << output.str();
         },
         "1/I(Ix)\nIx\nx\n");
    test("single step run resumes with a reset limit count",
         [] {
             std::vector<std::size_t> pauses;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     repeated_identity_expression(3),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     1,
                     [&pauses](std::size_t reductions) {
                         pauses.push_back(reductions);
                         return true;
                     });
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : pauses) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/11/I(Ix)\nIx\nx\n");
    test("zero step single step run prints the unchanged expression",
         [] {
             std::istringstream input;
             std::ostringstream output;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     repeated_identity_expression(2),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     0);
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::step_limit_reached) << '/'
                       << output.str();
         },
         "1/I(Ix)\n");
    test("zero step single step run grants one reduction after resume",
         [] {
             std::vector<std::size_t> pauses;
             std::istringstream input;
             std::ostringstream output;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     repeated_identity_expression(2),
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     0,
                     [&pauses](std::size_t reductions) {
                         pauses.push_back(reductions);
                         return true;
                     });
             std::cout << (outcome ==
                 combdsl::evaluation_outcome::completed) << '/';
             for (auto reductions : pauses) {
                 std::cout << reductions;
             }
             std::cout << '/' << output.str();
         },
         "1/01/I(Ix)\nIx\nx\n");
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
    test("eval delegates SIGINT resume decisions to a callback",
         [&] {
             std::istringstream input("unused\n");
             std::ostringstream output;
             std::size_t callback_count = 0;
             auto const outcome = combdsl::eval_with_outcome(
                 interrupting_identity_expression(quote(x)),
                 output,
                 input,
                 false,
                 evaluation_progress_callback{},
                 std::nullopt,
                 combdsl::evaluation_step_limit_callback{},
                 [&callback_count] {
                     ++callback_count;
                     return false;
                 });
             std::cout
                 << (outcome ==
                             combdsl::evaluation_outcome::cancelled
                         ? "cancelled"
                         : "wrong")
                 << '/' << callback_count << '/' << output.str();
         },
         "cancelled/1/x\n");
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
    test("color step needs no space before digit-ending basis",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(Q1)));
         },
         std::string{"  I"} +
             red_argument("Q1") +
             "\n->" +
             red_argument("Q1") +
             "\n");
    test("color step keeps space after digit-ending basis",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(Q1)(Q3)));
         },
         std::string{"  I"} +
             red_argument("Q1") +
             " Q3\n->" +
             red_argument("Q1") +
             " Q3\n");
    test("color step keeps punctuation-ending basis markup compact",
         [&] {
             static_cast<void>(
                 color_step_html(quote(I)(W_star)));
         },
         std::string{"  I"} +
             red_argument("W*") +
             "\n->" +
             red_argument("W*") +
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
    test("terminal color step needs no space before digit-ending basis",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(I)(Q1)));
         },
         std::string{"  I"} +
             terminal_red_argument("Q1") +
             "\n->" +
             terminal_red_argument("Q1") +
             "\n");
    test("terminal color step keeps space after digit-ending basis",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(I)(Q1)(Q3)));
         },
         std::string{"  I"} +
             terminal_red_argument("Q1") +
             " Q3\n->" +
             terminal_red_argument("Q1") +
             " Q3\n");
    test("terminal color step keeps punctuation-ending basis compact",
         [&] {
             static_cast<void>(
                 color_step_ansi(quote(I)(C_star_star)));
         },
         std::string{"  I"} +
             terminal_red_argument("C**") +
             "\n->" +
             terminal_red_argument("C**") +
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
    test("single step run delegates SIGINT resume decisions to a callback",
         [&] {
             std::istringstream input("unused\n");
             interrupt_on_flush_buffer buffer;
             std::ostream output(&buffer);
             std::size_t callback_count = 0;
             auto const outcome =
                 combdsl::single_step_run_with_outcome(
                     quoted_ski_x,
                     output,
                     input,
                     false,
                     evaluation_progress_callback{},
                     std::nullopt,
                     combdsl::evaluation_step_limit_callback{},
                     [&callback_count] {
                         ++callback_count;
                         return false;
                     });
             std::cout
                 << (outcome ==
                             combdsl::evaluation_outcome::cancelled
                         ? "cancelled"
                         : "wrong")
                 << '/' << callback_count << '/' << buffer.str();
         },
         "cancelled/1/");
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
    test("seven-character basis", seven_character_basis, "123456x");
    test("fifteen-character basis",
         fifteen_character_basis, "A12345678901234");
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
    test("Cardinal star bases need no space on either side",
         quote(Cstar)(C_star)(Vstar)(C_star_star)(copied_name_basis),
         "CstarC*VstarC**Alias");
    test("Warbler star bases need no space on either side",
         quote(copied_name_basis)(W_star)(Cstar)(W_star_star)(Vstar),
         "AliasW*CstarW**Vstar");
    test("a lowercase-ending basis before W-star round trips compactly",
         [] {
             auto const spaced = parse("Cstar W*");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarW*");
    static_cast<void>(basis("CstarW*", 1, I));
    test("an exact longer punctuation basis wins compact parsing",
         single_step(parse("CstarW*x")),
         "x");
    test("a custom punctuation-ending basis needs no surrounding spaces",
         quote(Cstar)(tail_plus_basis)(Vstar),
         "CstarTail+Vstar");
    test("a parsed punctuation-ending basis needs no surrounding spaces",
         parse("Cstar Tail+ Vstar"),
         "CstarTail+Vstar");
    test("a one-character punctuation basis needs no surrounding spaces",
         quote(Cstar)(plus_basis)(Vstar),
         "Cstar+Vstar");
    test("punctuation-ending bases remain compact around applications",
         quote(Cstar)
             (quote(W_star)(Vstar))
             (C_star_star)
             (quote(tail_plus_basis)(plus_basis)),
         "Cstar(W*Vstar)C**(Tail++)");
    test("a live user punctuation-ending basis round trips compactly",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse("set UserTail+ = 1 I"));
             auto const spaced = parse("Cstar UserTail+ Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
             static_cast<void>(parse("references captured"));
         },
         "1 CstarUserTail+Vstar");
    test("an explicit punctuation-ending sole revision prints bare",
         [] {
             auto const spaced = parse("Cstar UserTail+@1 Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserTail+Vstar");
    test("a removed captured punctuation sole revision round trips bare",
         [] {
             static_cast<void>(parse("remove UserTail+"));
             auto const spaced = parse("Cstar UserTail+@1 Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserTail+Vstar");
    static_cast<void>(basis("LeftDigit1", 1, I));
    test("digit-ending basis needs no leading space after a primitive",
         quote(K)(Q1), "KQ1");
    test("digit-ending basis needs no leading space after a symbol",
         quote(x)(Q1), "xQ1");
    test("digit-ending basis needs no leading space after another basis",
         quote(Cstar)(Q1), "CstarQ1");
    test("custom digit-ending basis needs no space before it",
         quote(x)(parse("LeftDigit1")), "xLeftDigit1");
    test("digit-ending basis output round trips through parser",
         [] {
             auto const spaced = parse(
                 "K Q1 Q3 Q1 K Q1 Cstar Q1 W* x");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 KQ1 Q3 Q1 KQ1 CstarQ1 W*x");
    test("recursive digit-ending name round trips after mixed-case basis",
         [] {
             static_cast<void>(parse(
                 "define RecDigit1 x = CstarRecDigit1 Vstar"));
             auto const definitions = set_list();
             auto const line_position = definitions.rfind('\n');
             auto const line = definitions.substr(
                 line_position == std::string::npos
                     ? 0
                     : line_position + 1);
             auto const inspected = combdsl::detail::parse_input(
                 line,
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << line << ' '
                       << (inspected.replaced_definition.empty()
                               ? "same"
                               : "changed");
         },
         "define RecDigit1 x = CstarRecDigit1 Vstar same");
    static_cast<void>(basis("XLeftDigit1", 0, I));
    test("exact longer digit-ending basis wins compact parsing",
         single_step(parse("XLeftDigit1")), "I");
    test("an ordinary explicit sole revision prints without its suffix",
         [] {
             static_cast<void>(parse("references captured"));
             static_cast<void>(parse("set PlainCaptured = 1 I"));
             auto const spaced = parse("K PlainCaptured@1");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 K PlainCaptured");
    test("a live user digit-ending basis round trips without a space before",
         [] {
             static_cast<void>(parse("references live"));
             static_cast<void>(parse("set UserDigit1 = 1 I"));
             auto const spaced = parse("Cstar UserDigit1 x Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
             static_cast<void>(parse("references captured"));
         },
         "1 CstarUserDigit1x Vstar");
    test("a live user digit-ending basis keeps a space after it",
         [] {
             static_cast<void>(parse("references live"));
             auto const spaced = parse("Cstar UserDigit1 Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
             static_cast<void>(parse("references captured"));
         },
         "1 CstarUserDigit1 Vstar");
    test("an explicit digit-ending sole revision prints bare",
         [] {
             auto const spaced = parse("Cstar UserDigit1@1 x Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserDigit1x Vstar");
    test("an explicit digit-ending sole revision keeps a space after it",
         [] {
             auto const spaced = parse("Cstar UserDigit1@1 Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserDigit1 Vstar");
    test_parse_failure(
        "an unavailable compact captured digit revision stays unknown",
        "CstarUserDigit1@2", 0, "unknown operand");
    test_parse_failure(
        "an invalid prefix before a valid captured digit revision stays unknown",
        "PUserDigit1@1", 0, "unknown operand");
    test("a removed digit-ending sole revision round trips bare",
         [] {
             static_cast<void>(parse("remove UserDigit1"));
             auto const spaced = parse("Cstar UserDigit1@1 x Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserDigit1x Vstar");
    test("a removed digit-ending sole revision keeps a space after it",
         [] {
             auto const spaced = parse("Cstar UserDigit1@1 Vstar");
             std::ostringstream rendered;
             spaced.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        spaced, reparsed)
                 << ' ' << rendered.str();
         },
         "1 CstarUserDigit1 Vstar");
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
    test("digit-ending basis before a symbol stays compact",
         quote(Q1)(x), "Q1x");
    test("digit-ending basis before a UTF-8 symbol stays compact",
         quote(Q1)(circle), "Q1\xE2\x97\x8F");
    test("digit-ending basis before a primitive keeps a space",
         quote(Q1)(K), "Q1 K");
    test("digit-ending basis before another digit-ending basis keeps a space",
         quote(Q1)(Q3), "Q1 Q3");
    test("digit-ending basis before an ordinary basis keeps a space",
         quote(Q1)(Cstar), "Q1 Cstar");
    test("digit-ending basis before a punctuation basis keeps a space",
         quote(Q1)(W_star), "Q1 W*");
    test("nested digit-ending basis before a symbol stays compact",
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
    test("an uppercase fallback run after a multicharacter basis is split",
         quote(copied_name_basis)(C)(s)(t)(a)(r),
         "Alias C star");
    test("a registered mixed-case atom keeps exact precedence after Alias",
         [&] {
             auto const parsed = parse("Alias Cstar");
             auto const expected = quote(copied_name_basis)(Cstar);
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        parsed, expected)
                 << ' ';
             parsed.print_to(std::cout);
         },
         "1 Alias Cstar");
    test("the split fallback after a multicharacter basis round trips",
         [&] {
             auto const expression =
                 quote(copied_name_basis)(C)(s)(t)(a)(r);
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 Alias C star");
    test("an uppercase fallback run after an exact name stays separated",
         [&] {
             auto const expression = quote(copied_name_basis)(C)(K)(x);
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 Alias CK x");
    test("a punctuation basis preserves a separated uppercase fallback run",
         [&] {
             auto const expression =
                 quote(copied_name_basis)(C)(W_star)(x);
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 Alias CW* x");
    test("the split fallback after a word remains parser-readable",
         [] {
             auto const expression =
                 parse(R"("word")")(C)(s)(t)(a)(r);
             std::ostringstream rendered;
             expression.print_to(rendered);
             static_cast<void>(parse(rendered.str()));
             std::cout << rendered.str();
         },
         "word C star");
    test("the split fallback after an integer round trips",
         [] {
             auto const expression =
                 parse("42")(C)(s)(t)(a)(r);
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 42 C star");
    test("single-character basis after primitive", K(M), "KM");
    test("single-character basis after symbol", x(M), "xM");
    test("opaque print_as_operand_to remains opaque",
         x(operand_named_value{}), "x<operand>");
    test("a spaced lowercase run after a primitive remains symbols",
         [] {
             auto const parsed = parse("S foo");
             auto const expected = quote(S)(f)(o)(o);
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        parsed, expected);
         },
         "1");
    test("longer basis remains atomic", parse("Sfoo"), "Sfoo");
    test("the longer basis and spaced lowercase symbols stay distinct",
         [] {
             auto const named = parse("Sfoo");
             auto const symbols = parse("S foo");
             std::cout
                 << !combdsl::detail::same_parser_definition_expression(
                        named, symbols);
         },
         "1");
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
    test("BazTest zxy", (bazTest)(z)(x)(y), "uy(z(yx))x");
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

    auto numeric_name_preserved_expression =
        K(std::make_unique<int>(31));
    bool numeric_basis_rejected = false;
    try {
        static_cast<void>(basis(
            "7", 1, std::move(numeric_name_preserved_expression)));
    } catch (std::invalid_argument const&) {
        numeric_basis_rejected = true;
    }
    check(numeric_basis_rejected);
    auto numeric_name_preserved_pointer =
        std::move(numeric_name_preserved_expression)(0);
    check(*numeric_name_preserved_pointer == 31);

    auto lowercase_name_preserved_expression =
        K(std::make_unique<int>(37));
    bool lowercase_basis_rejected = false;
    try {
        static_cast<void>(basis(
            "lower", 1,
            std::move(lowercase_name_preserved_expression)));
    } catch (std::invalid_argument const&) {
        lowercase_basis_rejected = true;
    }
    check(lowercase_basis_rejected);
    auto lowercase_name_preserved_pointer =
        std::move(lowercase_name_preserved_expression)(0);
    check(*lowercase_name_preserved_pointer == 37);

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

    test("removed mixed-case boundary setup defines Gsingle",
         parse("define Gsingle x=x"), "Gsingle");
    auto removed_mixed_case_snapshot = parse("Gsingle x");
    test("a current mixed-case function has a token boundary",
         removed_mixed_case_snapshot, "Gsingle x");
    test("removed mixed-case boundary setup removes Gsingle",
         parse("remove Gsingle"), "Gsingle");
    test("a retained removed mixed-case name adds a safe boundary",
         [&removed_mixed_case_snapshot] {
             std::ostringstream rendered;
             removed_mixed_case_snapshot.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        removed_mixed_case_snapshot, reparsed)
                 << ' ' << rendered.str();
         },
         "1 Gsingle x");
    test("a removed mixed-case function prints unambiguously",
         [] {
             auto const expression = parse("Gsingle x");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 Gsingle x");
    test("a removed mixed-case argument prints unambiguously",
         [] {
             auto const expression = parse("x Gsingle");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout
                 << combdsl::detail::same_parser_definition_expression(
                        expression, reparsed)
                 << ' ' << rendered.str();
         },
         "1 x Gsingle");

    test("the C++ basis API accepts a name beginning with equals",
         basis("=CppApi", 1, I), "=CppApi");
    test_parse_failure(
        "set does not mistake its assignment equals for a basis name",
        "set = 3 C", 6, "expected '='");
    test_parse_failure(
        "a leading equals token still needs a separate assignment equals",
        "set == 3 C", 7, "expected '='");
    test("set accepts equals as a basis name",
         parse("set = = 3 C"), "=");
    test("show accepts equals as a basis name",
         parse("show ="), "arity:3 C");
    test("an equals basis is self-delimiting before symbols",
         parse("= x y z"), "=xyz");
    test("an equals basis evaluates from compact expression syntax",
         [] { parse_eval("=xyz"); }, "xzy\n");
    test("set accepts a longer name beginning with equals",
         parse("set =bar = 3 C"), "=bar");
    test("show accepts a longer name beginning with equals",
         parse("show =bar"), "arity:3 C");
    test("a lowercase-ending equals name prints with a safe boundary",
         parse("=bar x y z"), "=bar xyz");
    test("a longer equals name evaluates through that boundary",
         [] { parse_eval("=bar x y z"); }, "xzy\n");
    test("define distinguishes an equals name from its symbol list",
         parse("define = bar = rab"), "=");
    test("the equals define binds each symbol after the name",
         [] { parse_eval("=xyz"); }, "zyx\n");
    test("show reports the equals define's arity",
         [] {
             std::ostringstream shown;
             parse("show =").print_to(shown);
             std::cout << shown.str().starts_with("arity:3 ");
         },
         "1");
    test("the equals name retains both revisions",
         [] {
             std::ostringstream revisions;
             parse("revisions =").print_to(revisions);
             auto const value = revisions.str();
             std::cout
                 << value.starts_with("=@1 arity:3 C [captured]\n")
                 << (value.find("=@2 arity:3 ") != std::string::npos)
                 << value.ends_with("[captured] [current]");
         },
         "111");
    test("set preserves later equals bytes in a leading-equals name",
         parse("set =foo=bar = 1 I"), "=foo=bar");
    test("show preserves later equals bytes in that name",
         parse("show =foo=bar"), "arity:1 I");
    test("a leading-equals name with an interior equals round trips",
         parse("=foo=bar x"), "=foo=bar x");
    test("the interior-equals name evaluates normally",
         [] { parse_eval("=foo=bar x"); }, "x\n");
    test("find among resolves the whole interior-equals name",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among =foo=bar ?x=x",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.is_find
                 << parsed.catalog_find_command.has_value()
                 << (parsed.catalog_find_command &&
                     parsed.catalog_find_command->catalog.size() == 1);
         },
         "111");
    test("compare accepts a leading-equals basis on a parenthesized left side",
         [] {
             static_cast<void>(parse(
                 "compare ?x (=foo=bar x) = x"));
             std::cout << "accepted";
         },
         "accepted");
    test("revisions accepts the full interior-equals name",
         parse("revisions =foo=bar"),
         "=foo=bar arity:1 I [captured] [current]");
    test("captured setup accepts equals as a body reference",
         parse("set captured EqualCaptured = 3 ="),
         "EqualCaptured");
    test("live setup accepts equals as a body reference",
         parse("set live EqualLive = 3 ="), "EqualLive");
    test("usedby reports a direct equals-name dependency",
         parse("usedby EqualCaptured"),
         "EqualCaptured directly uses: =");
    test("dependson accepts equals as its queried name",
         parse("dependson ="),
         "= is directly depended on by: EqualCaptured EqualLive");
    test("set can redefine equals after a define",
         parse("set = = 3 C"), "=");
    test("a captured equals reference retains the define revision",
         [] { parse_eval("EqualCaptured xyz"); }, "zyx\n");
    test("a live equals reference follows the set revision",
         [] { parse_eval("EqualLive xyz"); }, "xzy\n");
    test("remove accepts equals as a basis name",
         parse("remove ="), "=");
    test_parse_failure(
        "show treats a removed equals name as removed",
        "show =", 5, "= is not a defined name");
    test("a captured equals reference survives removal",
         [] { parse_eval("EqualCaptured xyz"); }, "zyx\n");
    test("a live equals reference retains its removed target",
         [] { parse_eval("EqualLive xyz"); }, "xzy\n");
    test("an explicit removed equals revision remains usable",
         [] { parse_eval("=@3xyz"); }, "xzy\n");
    test("set can restore an equals name after removal",
         parse("set = = 3 I"), "=");
    test("show reports the restored equals definition",
         parse("show ="), "arity:3 I");
    test("a live equals reference follows the restored revision",
         [] { parse_eval("EqualLive xyz"); }, "xyz\n");
    test("a captured equals reference remains frozen after restoration",
         [] { parse_eval("EqualCaptured xyz"); }, "zyx\n");
    test("remove accepts the full interior-equals name",
         parse("remove =foo=bar"), "=foo=bar");
    test_parse_failure(
        "show treats the interior-equals name as removed",
        "show =foo=bar", 5,
        "=foo=bar is not a defined name");
    test("a removed singleton interior-equals name remains parseable",
         [] { parse_eval("=foo=bar x"); }, "x\n");
    test("the set list preserves unambiguous equals-name commands",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find("set = = 3 C") !=
                     std::string::npos)
                 << (definitions.find("set =bar = 3 C") !=
                     std::string::npos)
                 << (definitions.find("define = bar = rab") !=
                     std::string::npos)
                 << (definitions.find("set =foo=bar = 1 I") !=
                     std::string::npos)
                 << (definitions.find("remove =") !=
                     std::string::npos)
                 << definitions.ends_with("remove =foo=bar");
         },
         "111111");

    test("the C++ basis API accepts a name beginning with question mark",
         basis("?CppApi", 1, I), "?CppApi");
    test("the C++ basis API separates a marker-shaped operand after K",
         K(basis("?api=", 0, C)), "K ?api=");
    test_parse_failure(
        "set does not mistake a question name for its assignment",
        "set ? 3 C", 6, "expected '='");
    test_parse_failure(
        "a longer leading question token still needs an assignment",
        "set ?? 3 C", 7, "expected '='");
    test_parse_failure(
        "an adjacent equals remains part of a lone question name",
        "set ?= 3 C", 7, "expected '='");
    test_parse_failure(
        "a leading question name needs whitespace before assignment",
        "set ?bar=3 C", 11, "expected '='");
    test_parse_failure(
        "set question version suffix reaches the normal diagnostic",
        "set ?Bad@1 = I", 8,
        "version suffix is not allowed in a definition name");
    test_parse_failure(
        "define question version suffix reaches the normal diagnostic",
        "define ?Bad@1 x = x", 11,
        "version suffix is not allowed in a definition name");
    test_parse_failure(
        "remove question version suffix reaches the normal diagnostic",
        "remove ?Bad@1", 11,
        "version suffix is not allowed in a removal name");
    test_parse_failure(
        "revisions question suffix reaches the normal diagnostic",
        "revisions ?Bad@1", 14,
        "version suffix is not allowed in a revisions name");
    test_parse_failure(
        "an unregistered marker-shaped prefix leaves an empty catalog",
        "find among ?x=xx", 11,
        "expected at least one bird name");

    test("ordinary ampersand accepts a compact assignment",
         parse("set &=1 I"), "&");
    test("ordinary longer ampersand accepts a compact assignment",
         parse("set &bar=1 I"), "&bar");
    test("ordinary ampersand stops its name at the first equals",
         parse("set &foo=bar"), "&foo");
    test("the old special ampersand whole name is not registered",
         [] {
             try {
                 static_cast<void>(parse("show &foo=bar"));
             } catch (parse_error const&) {
                 std::cout << "not registered";
             }
         },
         "not registered");
    test("ordinary ampersand retains compact define splitting",
         parse("define &bar = rab"), "&");
    test("ordinary internal ampersand retains compact assignment",
         parse("set A&B=1 I"), "A&B");
    test("ordinary internal ampersand evaluates normally",
         [] { parse_eval("A&B x"); }, "x\n");

    test("set accepts question mark as a basis name",
         parse("set ? = 3 C"), "?");
    test("show accepts question mark as a basis name",
         parse("show ?"), "arity:3 C");
    test("a question basis is self-delimiting before symbols",
         parse("? x y z"), "?xyz");
    test("a question basis evaluates from compact expression syntax",
         [] { parse_eval("?xyz"); }, "xzy\n");
    test("set accepts a longer name beginning with question mark",
         parse("set ?bar = 3 C"), "?bar");
    test("set accepts another question-prefixed name",
         parse("set ?foo = 1 I"), "?foo");
    test("show accepts a longer question name",
         parse("show ?bar"), "arity:3 C");
    test("an exact longer question name wins over its bare prefix",
         single_step(parse("?bar x y z"), true), "Cxyz");
    test("a lowercase-ending question name prints with a safe boundary",
         parse("?bar x y z"), "?bar xyz");
    test("a longer question name evaluates through that boundary",
         [] { parse_eval("?bar x y z"); }, "xzy\n");
    test("define distinguishes a bare question name from its symbols",
         parse("define ? bar = rab"), "?");
    test("the bare question define binds each listed symbol",
         [] { parse_eval("?xyz"); }, "zyx\n");
    test("compact define retains the one-character question grammar",
         parse("define ?bar = rab"), "?");
    test("compact question define does not replace the longer name",
         [] { parse_eval("?xyz"); }, "zyx\n");
    test("show still reports the longer question set definition",
         parse("show ?bar"), "arity:3 C");
    test("the bare question name retains set and define revisions",
         [] {
             std::ostringstream revisions;
             parse("revisions ?").print_to(revisions);
             auto const value = revisions.str();
             std::cout
                 << value.starts_with("?@1 arity:3 C [captured]\n")
                 << (value.find("?@2 arity:3 ") != std::string::npos)
                 << value.ends_with("[captured] [current]");
         },
         "111");
    test("set preserves a later equals byte in a question name",
         parse("set ?foo=bar = 1 I"), "?foo=bar");
    test("show preserves the question name's interior equals",
         parse("show ?foo=bar"), "arity:1 I");
    test("an interior-equals question name round trips",
         parse("?foo=bar x"), "?foo=bar x");
    test("the interior-equals question name evaluates normally",
         [] { parse_eval("?foo=bar x"); }, "x\n");
    test("set preserves a later question byte in a question name",
         parse("set ?foo?bar = 1 I"), "?foo?bar");
    test("the interior-question name evaluates normally",
         [] { parse_eval("?foo?bar x"); }, "x\n");
    test("set accepts a question name ending in equals",
         parse("set ?x= = B"), "?x=");
    test("set accepts a second marker-shaped question name",
         parse("set ?y= = C"), "?y=");
    test("inspect recognizes an exact question-prefixed reference",
         [] {
             auto const value = parse("inspect ?foo=bar x");
             std::ostringstream output;
             value.print_to(output);
             std::cout
                 << (output.str().find("free symbols: x") !=
                     std::string::npos)
                 << (output.str().find(
                        "  ?foo=bar [captured]") !=
                     std::string::npos);
         },
         "11");
    test("find among distinguishes question names from its first marker",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ? ?bar ?foo ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.is_find
                 << parsed.catalog_find_command.has_value()
                 << (parsed.catalog_find_command &&
                     parsed.catalog_find_command->catalog.size() == 3);
         },
         "111");
    test("an exact whole marker-shaped name remains a catalog bird",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?foo=bar ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("an exact marker-shaped name resolves inside a compact group",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I?foo=bar ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "21");
    test("a long registered prefix blocks marker fallback in a compact group",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?foo=barI ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "21");
    test("set accepts an exact name overlapping a compact catalog group",
         parse("set ?foo=barI = K"), "?foo=barI");
    test("an exact whole group wins over its registered compact prefix",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?foo=barI ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("a prefix shorter than compact equals cannot steal a marker",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?foo=x",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "13x");
    test("find among resolves a question-name revision group",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?foo@1 ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("the first unresolved marker-shaped item starts the query",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?x= ?z= ?y=",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "11?y=");
    test("an unregistered spaced question name remains a query marker",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?x = ?y=",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "11?y=");
    test_parse_failure(
        "find among still rejects an empty sequential catalog",
        "find among ?q=q", 11,
        "expected at least one bird name");
    test_parse_failure(
        "a fully resolved sequential catalog still requires a marker",
        "find among S K ?x= ?y=", 22, "expected '?'");
    test_parse_failure(
        "the first unresolved ordinary group errors before a later marker",
        "find among ?x= Missing ?z=zz", 16,
        "issing is not a defined name");
    test_parse_failure(
        "a registered marker prefix makes its compact suffix ordinary",
        "find among I ?x=xx", 16,
        "xx is not a defined name");
    test_parse_failure(
        "a registered marker prefix exposes a longer ordinary suffix",
        "find among ?x=barI ?q=q", 14,
        "barI is not a defined name");
    test_parse_failure(
        "an invalid revision follows a usable compact marker prefix",
        "find among ?x=@999 ?q=q", 14,
        "@999 is not a defined name");
    test_parse_failure(
        "a marker-shaped compact suffix is not a query marker",
        "find among I?z=zz", 13,
        "z=zz is not a defined name");
    test("the user sequential catalog parses four registered birds",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among S K ?x= ?y= ?z= ?y=",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
             parsed.catalog_find_command->symbols[0]
                 .expression().print_to(std::cout);
             for (auto const& bird :
                  parsed.catalog_find_command->catalog) {
                 std::cout << '|';
                 bird.print_to(std::cout);
             }
             std::cout << '|';
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "41z|S|K|?x=|?y=|?y=");
    test("the user sequential catalog finds the exact requested expression",
         parse("find among S K ?x= ?y= ?z= ?y="),
         "?=K ?y=");
    test("a leading-question operand prints after K with a safe boundary",
         parse("K ?y="), "K ?y=");
    test("the spaced leading-question operand print reparses identically",
         [] {
             auto const expression = parse("K ?y=");
             std::ostringstream printed;
             expression.print_to(printed);
             auto const reparsed = parse(printed.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     expression, reparsed);
         },
         "1");
    test("a leading-question function beside another operand round trips",
         [] {
             auto const expression = parse("?y= K");
             std::ostringstream printed;
             expression.print_to(printed);
             auto const reparsed = parse(printed.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     expression, reparsed);
         },
         "1");
    test("registered compact marker names split greedily in one group",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?x=?y= ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "21");
    test("set accepts a whole name overlapping compact marker names",
         parse("set ?x=?y= = K"), "?x=?y=");
    test("an exact whole compact group wins over greedy marker names",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?x=?y= ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("set creates the first revision of a marker-shaped name",
         parse("set ?rev= = I"), "?rev=");
    test("set creates a second revision of a marker-shaped name",
         parse("set ?rev= = S"), "?rev=");
    test("current and explicit marker-shaped revisions remain catalog birds",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?rev= ?rev=@1 ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "21");
    test("a multiply revised marker-shaped name can be removed",
         parse("remove ?rev="), "?rev=");
    test("an explicit removed revision remains a catalog bird",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?rev=@1 ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("a removed multi-revision bare name becomes the query marker",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?rev=K",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "13K");
    test_parse_failure(
        "an invalid revision after an unavailable prefix reaches the target",
        "find among I ?rev=@999", 18,
        "unknown operand");
    test_parse_failure(
        "a removed multi-revision marker leaves a start catalog empty",
        "find among ?rev=K", 11,
        "expected at least one bird name");
    test("set creates a marker-shaped singleton for removal",
         parse("set ?gone= = I"), "?gone=");
    test("a marker-shaped singleton can be removed",
         parse("remove ?gone="), "?gone=");
    test("a removed singleton marker-shaped name remains a catalog bird",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?gone= ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "11");
    test("a removed singleton question name remains a Find target",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among S K ?z= ?gone=",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "?gone=");
    test("an explicit removed question revision remains a Find target",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among S K ?z= ?rev=@1",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             parsed.catalog_find_command->target.print_to(std::cout);
         },
         "?rev=@1");
    test("an unregistered spaced question marker accepts its target",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?space = ?y=",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "15");
    test("set creates a spaced-marker question name",
         parse("set ?space = I"), "?space");
    test("a current spaced-marker name and equals are catalog birds",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?space = ?q=K",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "31");
    test("a spaced-marker question singleton can be removed",
         parse("remove ?space"), "?space");
    test("a removed spaced-marker singleton and equals remain catalog birds",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among I ?space = ?q=K",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.catalog_find_command->catalog.size()
                 << parsed.catalog_find_command->symbols.size();
         },
         "31");
    test("find among accepts an explicit bare-question revision",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 "find among ?@1 ?q=q",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.is_find
                 << parsed.catalog_find_command.has_value()
                 << (parsed.catalog_find_command &&
                     parsed.catalog_find_command->catalog.size() == 1);
         },
         "111");
    test("compare keeps its marker contextual before a question basis",
         parse("compare ?x ?foo=bar x = x"),
         "both reduce to: xx");
    test("abstract keeps its marker contextual after question registration",
         parse("abstract ?x=x"), "?=I");
    test("revisions accepts the complete interior-equals question name",
         parse("revisions ?foo=bar"),
         "?foo=bar arity:1 I [captured] [current]");
    test("captured setup accepts question mark as a body reference",
         parse("set captured QCaptured = 3 ?"),
         "QCaptured");
    test("live setup accepts question mark as a body reference",
         parse("set live QLive = 3 ?"), "QLive");
    test("usedby reports a direct question-name dependency",
         parse("usedby QCaptured"),
         "QCaptured directly uses: ?");
    test("dependson accepts question mark as its queried name",
         parse("dependson ?"),
         "? is directly depended on by: QCaptured QLive");
    test("usedby path traverses a question-name dependency",
         parse("usedby path QCaptured ?"),
         "QCaptured uses ? via:\n"
         "  QCaptured -> ?@2  [captured]");
    test("set can redefine question mark after define",
         parse("set ? = 3 C"), "?");
    test("a captured question reference retains the define revision",
         [] { parse_eval("QCaptured xyz"); }, "zyx\n");
    test("a live question reference follows the set revision",
         [] { parse_eval("QLive xyz"); }, "xzy\n");
    test("remove accepts question mark as a basis name",
         parse("remove ?"), "?");
    test_parse_failure(
        "show treats a removed question name as removed",
        "show ?", 5, "? is not a defined name");
    test("a captured question reference survives removal",
         [] { parse_eval("QCaptured xyz"); }, "zyx\n");
    test("a live question reference retains its removed target",
         [] { parse_eval("QLive xyz"); }, "xzy\n");
    test("an explicit removed question revision remains usable",
         [] { parse_eval("?@3xyz"); }, "xzy\n");
    test("set can restore a question name after removal",
         parse("set ? = 3 I"), "?");
    test("show reports the restored question definition",
         parse("show ?"), "arity:3 I");
    test("a live question reference follows the restored revision",
         [] { parse_eval("QLive xyz"); }, "xyz\n");
    test("a captured question reference remains frozen after restoration",
         [] { parse_eval("QCaptured xyz"); }, "zyx\n");
    test("remove accepts the full interior-question name",
         parse("remove ?foo?bar"), "?foo?bar");
    test("a removed singleton interior-question name remains parseable",
         [] { parse_eval("?foo?bar x"); }, "x\n");
    test("remove accepts the full interior-equals question name",
         parse("remove ?foo=bar"), "?foo=bar");
    test_parse_failure(
        "show treats the interior-equals question name as removed",
        "show ?foo=bar", 5,
        "?foo=bar is not a defined name");
    test("a removed singleton interior-equals question name is parseable",
         [] { parse_eval("?foo=bar x"); }, "x\n");
    test("the set list preserves unambiguous question-name commands",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find("set ?bar = 3 C") !=
                     std::string::npos)
                 << (definitions.find("set ?foo = 1 I") !=
                     std::string::npos)
                 << (definitions.find("define ? bar = rab") !=
                     std::string::npos)
                 << (definitions.find("define ?bar = rab") ==
                     std::string::npos)
                 << (definitions.find("set ?foo=bar = 1 I") !=
                     std::string::npos)
                 << (definitions.find("set ?foo?bar = 1 I") !=
                     std::string::npos)
                 << (definitions.find("set ?x= = 0 B") !=
                     std::string::npos)
                 << (definitions.find("set ?y= = 0 C") !=
                     std::string::npos)
                 << (definitions.find("set ?foo=barI = 0 K") !=
                     std::string::npos)
                 << (definitions.find("set ?x=?y= = 0 K") !=
                     std::string::npos)
                 << (definitions.find("remove ?rev=") !=
                     std::string::npos)
                 << (definitions.find("remove ?gone=") !=
                     std::string::npos)
                 << (definitions.find("remove ?space") !=
                     std::string::npos)
                 << (definitions.find("set &foo = 0 bar") !=
                     std::string::npos)
                 << (definitions.find("remove ?") !=
                     std::string::npos)
                 << definitions.ends_with("remove ?foo=bar");
         },
         "1111111111111111");

    test("set accepts a visible backslash as a basis name",
         parse(R"(set \ = 3 C)"), R"(\)");
    test("show accepts the bare backslash basis name",
         parse(R"(show \)"), "arity:3 C");
    test("the bare backslash basis has exact precedence over the raw value",
         single_step(parse(R"(\ x y z)"), true),
         "Cxyz");
    test("the bare backslash basis evaluates normally",
         [] { parse_eval(R"(\ x y z)"); }, "xzy\n");
    test("set accepts a longer visible backslash-prefixed name",
         parse(R"(set \foo = 1 I)"), R"(\foo)");
    test("show accepts the longer backslash-prefixed name",
         parse(R"(show \foo)"), "arity:1 I");
    test("an exact longer backslash name wins over the bare name prefix",
         single_step(parse(R"(\foo x)"), true),
         "Ix");
    test("a lowercase-ending backslash name prints with a safe boundary",
         parse(R"(\foo x)"), R"(\foo x)");
    test("the longer backslash-prefixed name evaluates normally",
         [] { parse_eval(R"(\foo x)"); }, "x\n");
    test("define distinguishes a bare backslash name from its symbols",
         parse(R"(define \ bar = rab)"), R"(\)");
    test("the backslash define binds every symbol after the name",
         [] { parse_eval(R"(\ x y z)"); }, "zyx\n");
    test("the bare backslash name retains set and define revisions",
         [] {
             std::ostringstream revisions;
             parse(R"(revisions \)").print_to(revisions);
             auto const value = revisions.str();
             std::cout
                 << value.starts_with(
                        R"(\@1 arity:3 C [captured])" "\n")
                 << (value.find(R"(\@2 arity:3 )") !=
                     std::string::npos)
                 << value.ends_with("[captured] [current]");
         },
         "111");
    test("inspect recognizes the exact backslash-prefixed reference",
         [] {
             std::ostringstream inspected;
             parse(R"(inspect \foo x)").print_to(inspected);
             auto const value = inspected.str();
             std::cout
                 << (value.find(R"(  \foo [captured])") !=
                     std::string::npos)
                 << (value.find("free symbols: x") !=
                     std::string::npos);
         },
         "11");
    test("find among resolves an exact backslash-prefixed catalog name",
         [] {
             auto parsed = combdsl::detail::parse_input(
                 R"(find among \foo ?q=q)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout
                 << parsed.is_find
                 << parsed.catalog_find_command.has_value()
                 << (parsed.catalog_find_command &&
                     parsed.catalog_find_command->catalog.size() == 1);
         },
         "111");
    test("compare accepts a backslash-prefixed basis",
         parse(R"(compare ?x \foo x = x)"),
         "both reduce to: xx");
    test("captured setup accepts a bare backslash body reference",
         parse(R"(set captured SlashCaptured = 3 \)"),
         "SlashCaptured");
    test("live setup accepts a bare backslash body reference",
         parse(R"(set live SlashLive = 3 \)"),
         "SlashLive");
    test("usedby reports a direct backslash-name dependency",
         parse("usedby SlashCaptured"),
         R"(SlashCaptured directly uses: \)");
    test("dependson accepts the bare backslash queried name",
         parse(R"(dependson \)"),
         R"(\ is directly depended on by: SlashCaptured SlashLive)");
    test("usedby path traverses a backslash-name dependency",
         [] {
             std::ostringstream path;
             parse(R"(usedby path SlashCaptured \)").print_to(path);
             auto const value = path.str();
             std::cout
                 << value.starts_with(
                        R"(SlashCaptured uses \ via:)" "\n")
                 << (value.find("SlashCaptured") !=
                     std::string::npos)
                 << (value.find(R"(-> \@2  [captured])") !=
                     std::string::npos);
         },
         "111");
    test("set can redefine the bare backslash after define",
         parse(R"(set \ = 3 C)"), R"(\)");
    test("a captured backslash reference retains its define revision",
         [] { parse_eval("SlashCaptured xyz"); }, "zyx\n");
    test("a live backslash reference follows the latest set revision",
         [] { parse_eval("SlashLive xyz"); }, "xzy\n");
    test("remove accepts the bare backslash basis name",
         parse(R"(remove \)"), R"(\)");
    test_parse_failure(
        "show treats a removed bare backslash name as removed",
        R"(show \)", 5,
        R"(\ is not a defined name)");
    test("a captured backslash reference survives removal",
         [] { parse_eval("SlashCaptured xyz"); }, "zyx\n");
    test("a live backslash reference retains its removed target",
         [] { parse_eval("SlashLive xyz"); }, "xzy\n");
    test("an explicit removed backslash revision remains usable",
         [] { parse_eval(R"(\@3 x y z)"); },
         "xzy\n");
    test("set can restore the bare backslash name after removal",
         parse(R"(set \ = 3 I)"), R"(\)");
    test("a live backslash reference follows the restored revision",
         [] { parse_eval("SlashLive xyz"); }, "xyz\n");
    test("a captured backslash reference stays frozen after restoration",
         [] { parse_eval("SlashCaptured xyz"); }, "zyx\n");
    test("disconnected path output beginning with slash stays unquoted",
         [] {
             static_cast<void>(parse("set ZAside = 1 I"));
             parse(R"(usedby path \ ZAside)")
                 .print_to(std::cout);
         },
         R"(ZAside and \ have no dependency path)");
    test("the quoted raw backslash remains literal after registration",
         [] {
             auto const parsed = parse(R"("\\")");
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     parsed, quote(std::string_view(R"(\)")));
         },
         "1");
    test("a pre-registration raw backslash snapshot survives collision",
         [&unregistered_literal_backslash] {
             std::ostringstream rendered;
             unregistered_literal_backslash.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     unregistered_literal_backslash, reparsed)
                 << ' ' << rendered.str();
         },
         R"(1 "\\")");
    test("a raw backslash application prints past the registered prefix",
         [&unregistered_literal_backslash_application] {
             std::ostringstream rendered;
             unregistered_literal_backslash_application.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     unregistered_literal_backslash_application,
                     reparsed)
                 << ' ' << rendered.str();
         },
         R"(1 "\\"foo)");
    test("the longest direct backslash name within 15 bytes is accepted",
         parse(R"(set \12345678901234 = 1 I)"),
         R"(\12345678901234)");
    constexpr std::string_view overlong_direct_backslash_name =
        R"(set \123456789012345 = 1 I)";
    test_parse_failure(
        "a direct backslash name still observes the 15-byte limit",
        overlong_direct_backslash_name,
        overlong_direct_backslash_name.find(R"(\123456789012345)") +
            15,
        "combdsl::basis names are limited to 15 characters");
    test_parse_failure(
        "the rejected overlong backslash name is not registered",
        R"(show \123456789012345)", 5,
        R"(\123456789012345 is not a defined name)");
    test("remove accepts the maximum-length backslash name",
         parse(R"(remove \12345678901234)"),
         R"(\12345678901234)");
    test("remove accepts the longer backslash-prefixed basis name",
         parse(R"(remove \foo)"), R"(\foo)");
    test("a removed singleton backslash-prefixed name remains parseable",
         [] { parse_eval(R"(\foo x y z)"); },
         "xyz\n");
    test("the set list journals replayable visible backslash commands",
         [] {
             auto const definitions = set_list();
             std::cout
                 << (definitions.find(R"(set \ = 3 C)") !=
                     std::string::npos)
                 << (definitions.find(R"(set \foo = 1 I)") !=
                     std::string::npos)
                 << (definitions.find(R"(define \ bar = rab)") !=
                     std::string::npos)
                 << (definitions.find(
                        R"(set captured SlashCaptured = 3 \)") !=
                     std::string::npos)
                 << (definitions.find(
                        R"(set live SlashLive = 3 \)") !=
                     std::string::npos)
                 << (definitions.find(R"(remove \)") !=
                     std::string::npos)
                 << definitions.ends_with(R"(remove \foo)");
         },
         "1111111");
    test("a direct escaped quote can be saved",
         parse(R"(set DirectRawQuote = 0 "a\" b")"),
         "DirectRawQuote");
    test("a direct escaped quote is journaled in C-style spelling",
         [] {
             std::cout << set_list().ends_with(
                 R"(set DirectRawQuote = 0 "a\" b")");
         },
         "1");
    test("the journaled direct raw bare quote replays",
         parse(R"(set DirectRawQuote = 0 "a\" b")"),
         "DirectRawQuote");
    test("the replayed direct raw bare quote retains its value",
         [] { parse_eval("DirectRawQuote"); }, "a\" b\n");
    test("a raw one-byte backslash define parses a compact self-reference",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 R"(define \ x = \x)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << parsed.is_definition;
         },
         "1");
    test("a longer backslash define parses a compact self-reference",
         [] {
             auto const parsed = combdsl::detail::parse_input(
                 R"(define \foo x = \foo x)",
                 combdsl::detail::parser_definition_mode::
                     inspect_definitions);
             std::cout << parsed.is_definition;
         },
         "1");
    test("the C++ basis API sees the direct one-byte backslash collision",
         [] {
             try {
                 static_cast<void>(basis(R"(\)", 1, I));
             } catch (std::invalid_argument const& error) {
                 std::cout << error.what();
             }
         },
         R"(combdsl::basis name is already user-defined: \)");
    test("the parser gives an exact one-byte backslash name precedence",
         parse(R"(\)"), R"(\)");
    test("word openers remain distinct after slash registrations",
         parse(R"("word")"), "word");
    test("a quoted literal slash remains distinct after all registrations",
         parse(R"("\\")"), R"("\\")");
    test("a one-byte backslash basis recognizes an adjacent symbol",
         parse(R"(\x)"), R"(\ x)");
    test("a one-byte backslash application prints round-trippably",
         [] {
             auto const expression = parse(R"(\x)");
             std::ostringstream rendered;
             expression.print_to(rendered);
             auto const reparsed = parse(rendered.str());
             std::cout <<
                 combdsl::detail::same_parser_definition_expression(
                     expression, reparsed)
                 << ' ' << rendered.str();
         },
         R"(1 \ x)");

    test("an atomic user-defined CO basis is not an Albatross pattern",
         [] {
             static_cast<void>(parse("define CO x = x"));
             static_cast<void>(parse("define AtomicNamedCO = CO"));
             parse("show AtomicNamedCO").print_to(std::cout);
         },
         "arity:0 CO");

    std::cout << tests_run << " test(s) run, "
              << test_failures << " failed\n";

    return test_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
