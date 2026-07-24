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

#include <emscripten/bind.h>

#include <exception>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct evaluation_result {
    bool success;
    bool definition;
    std::string output;
    std::string error;
};

struct single_step_result {
    bool success;
    bool reduced;
    bool complete;
    bool definition;
    std::string output;
    std::string error;
};

std::optional<combdsl::quoted_expression> stepped_expression;

[[nodiscard]] evaluation_result parse_eval_input(std::string const& source) {
    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, {}, {}};
        }
        combdsl::eval(std::move(parsed.expression), output, input);
        return {true, false, output.str(), {}};
    } catch (std::exception const& error) {
        return {false, false, {}, error.what()};
    } catch (...) {
        return {false, false, {}, "unknown evaluation error"};
    }
}

[[nodiscard]] evaluation_result single_step_run_input(
    std::string const& source, bool basis_step) {
    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, {}, {}};
        }
        combdsl::single_step_run(
            std::move(parsed.expression), output, input, basis_step);
        return {true, false, output.str(), {}};
    } catch (std::exception const& error) {
        return {false, false, {}, error.what()};
    } catch (...) {
        return {false, false, {}, "unknown evaluation error"};
    }
}

[[nodiscard]] evaluation_result color_step_run_input(
    std::string const& source, bool basis_step) {
    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, {}, {}};
        }
        auto expression = std::move(parsed.expression);
        std::ostringstream output;
        bool reduced = false;

        for (;;) {
            std::ostringstream step_output;
            auto next = combdsl::color_step(
                expression, step_output, basis_step);
            if (combdsl::detail::quoted_access::root(next) ==
                combdsl::detail::quoted_access::root(expression)) {
                if (reduced) {
                    combdsl::detail::print_quoted_html(
                        output, expression);
                    output << '\n';
                }
                break;
            }

            output << step_output.str();
            expression = std::move(next);
            reduced = true;
        }

        return {true, false, output.str(), {}};
    } catch (std::exception const& error) {
        return {false, false, {}, error.what()};
    } catch (...) {
        return {false, false, {}, "unknown evaluation error"};
    }
}

[[nodiscard]] single_step_result begin_single_step_input(
    std::string const& source) {
    stepped_expression.reset();

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, false, true, true, {}, {}};
        }
        stepped_expression.emplace(std::move(parsed.expression));
        return {true, false, false, false, {}, {}};
    } catch (std::exception const& error) {
        return {false, false, false, false, {}, error.what()};
    } catch (...) {
        return {
            false, false, false, false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] single_step_result take_single_step(
    bool basis_step, bool colorize) {
    if (!stepped_expression.has_value()) {
        return {
            false, false, false, false, {},
            "no expression is ready to step"};
    }

    try {
        std::ostringstream output;
        auto next = colorize
            ? combdsl::color_step(
                  *stepped_expression, output, basis_step)
            : combdsl::single_step(
                  *stepped_expression, basis_step);
        if (combdsl::detail::quoted_access::root(next) ==
            combdsl::detail::quoted_access::root(*stepped_expression)) {
            stepped_expression.reset();
            return {true, false, true, false, {}, {}};
        }

        stepped_expression = std::move(next);
        auto following = combdsl::single_step(
            *stepped_expression, basis_step);
        bool complete =
            combdsl::detail::quoted_access::root(following) ==
            combdsl::detail::quoted_access::root(
                *stepped_expression);
        if (colorize) {
            if (complete) {
                combdsl::detail::print_quoted_html(
                    output, *stepped_expression);
                output << '\n';
            }
        } else {
            stepped_expression->print_to(output);
            output << '\n';
        }
        if (complete) {
            stepped_expression.reset();
        }
        return {
            true, true, complete, false, output.str(), {}};
    } catch (std::exception const& error) {
        stepped_expression.reset();
        return {false, false, false, false, {}, error.what()};
    } catch (...) {
        stepped_expression.reset();
        return {
            false, false, false, false, {},
            "unknown evaluation error"};
    }
}

} // namespace

EMSCRIPTEN_BINDINGS(combdsl_browser) {
    emscripten::value_object<evaluation_result>("EvaluationResult")
        .field("success", &evaluation_result::success)
        .field("definition", &evaluation_result::definition)
        .field("output", &evaluation_result::output)
        .field("error", &evaluation_result::error);

    emscripten::value_object<single_step_result>("SingleStepResult")
        .field("success", &single_step_result::success)
        .field("reduced", &single_step_result::reduced)
        .field("complete", &single_step_result::complete)
        .field("definition", &single_step_result::definition)
        .field("output", &single_step_result::output)
        .field("error", &single_step_result::error);

    emscripten::function("parseEval", &parse_eval_input);
    emscripten::function("singleStepRun", &single_step_run_input);
    emscripten::function("colorStepRun", &color_step_run_input);
    emscripten::function("beginSingleStep", &begin_single_step_input);
    emscripten::function("takeSingleStep", &take_single_step);
}
