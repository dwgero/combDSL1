/*
 * Combinator Studio
 * Part of C++ Combinator DSL
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

#include "load_set_list.hpp"

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct evaluation_result {
    bool success;
    bool definition;
    bool recover_worker;
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

struct load_result {
    bool success;
    std::size_t loaded;
    std::size_t line;
    std::string error;
};

struct definition_inspection_result {
    bool success;
    bool definition;
    std::string replacement;
    std::string error;
};

std::optional<combdsl::quoted_expression> stepped_expression;

[[nodiscard]] load_result load_set_list_input(
    std::string const& source,
    std::string const& filename) {
    auto const result =
        combdsl::web_detail::load_set_list(source);
    return {
        result.success,
        result.loaded,
        result.fatal_line,
        combdsl::web_detail::format_file_load_diagnostics(
            filename, result)};
}

[[nodiscard]] definition_inspection_result inspect_definition_input(
    std::string const& source) {
    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(
            escaped_source,
            combdsl::detail::parser_definition_mode::
                inspect_definitions);
        return {
            true,
            parsed.is_definition,
            std::move(parsed.replaced_definition),
            {}};
    } catch (std::exception const& error) {
        return {false, false, {}, error.what()};
    } catch (...) {
        return {
            false,
            false,
            {},
            "unknown definition inspection error"};
    }
}

[[nodiscard]] combdsl::evaluation_progress_callback
make_evaluation_progress_callback(std::size_t request_id) {
    constexpr double heartbeat_interval_ms = 100.0;
    auto last_heartbeat_at = emscripten_get_now();
    std::size_t sequence = 0;
    return [
        request_id,
        last_heartbeat_at,
        sequence
    ](std::size_t reductions) mutable {
        auto const now = emscripten_get_now();
        if (now - last_heartbeat_at < heartbeat_interval_ms) {
            return;
        }

        last_heartbeat_at = now;
        auto message = emscripten::val::object();
        message.set("type", std::string{"eval-progress"});
        message.set("id", static_cast<double>(request_id));
        message.set(
            "sequence", static_cast<double>(++sequence));
        message.set(
            "reductions", static_cast<double>(reductions));
        emscripten::val::global("self").call<void>(
            "postMessage", message);
    };
}

[[nodiscard]] evaluation_result parse_eval_input(
    std::string const& source,
    std::size_t request_id) {
    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {true, false, false, output.str(), {}};
        }

        try {
            auto progress =
                make_evaluation_progress_callback(request_id);
            combdsl::eval(
                std::move(parsed.expression), output, input, false,
                progress);
            return {true, false, false, output.str(), {}};
        } catch (std::exception const& error) {
            return {false, false, true, {}, error.what()};
        } catch (...) {
            return {
                false, false, true, {},
                "unknown evaluation error"};
        }
    } catch (std::exception const& error) {
        return {false, false, false, {}, error.what()};
    } catch (...) {
        return {
            false, false, false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] evaluation_result single_step_run_input(
    std::string const& source,
    bool basis_step,
    std::size_t request_id) {
    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {true, false, false, output.str(), {}};
        }

        try {
            auto progress =
                make_evaluation_progress_callback(request_id);
            combdsl::single_step_run(
                std::move(parsed.expression), output, input, basis_step,
                progress);
            return {true, false, false, output.str(), {}};
        } catch (std::exception const& error) {
            return {false, false, true, {}, error.what()};
        } catch (...) {
            return {
                false, false, true, {},
                "unknown evaluation error"};
        }
    } catch (std::exception const& error) {
        return {false, false, false, {}, error.what()};
    } catch (...) {
        return {
            false, false, false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] evaluation_result color_step_run_input(
    std::string const& source,
    bool basis_step,
    std::size_t request_id) {
    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }
        if (parsed.is_display_only) {
            std::ostringstream output;
            combdsl::detail::print_quoted_html(
                output, parsed.expression);
            output << '\n';
            return {true, false, false, output.str(), {}};
        }

        try {
            auto expression = std::move(parsed.expression);
            std::ostringstream output;
            bool reduced = false;
            auto progress =
                make_evaluation_progress_callback(request_id);
            combdsl::detail::evaluation_progress_reporter reporter(
                progress);

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
                reporter.completed_reduction();
            }

            return {true, false, false, output.str(), {}};
        } catch (std::exception const& error) {
            return {false, false, true, {}, error.what()};
        } catch (...) {
            return {
                false, false, true, {},
                "unknown evaluation error"};
        }
    } catch (std::exception const& error) {
        return {false, false, false, {}, error.what()};
    } catch (...) {
        return {
            false, false, false, {}, "unknown parsing error"};
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
        if (parsed.is_display_only) {
            std::ostringstream output;
            parsed.expression.print_to(output);
            output << '\n';
            return {
                true, false, true, false, output.str(), {}};
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
        .field("recoverWorker", &evaluation_result::recover_worker)
        .field("output", &evaluation_result::output)
        .field("error", &evaluation_result::error);

    emscripten::value_object<single_step_result>("SingleStepResult")
        .field("success", &single_step_result::success)
        .field("reduced", &single_step_result::reduced)
        .field("complete", &single_step_result::complete)
        .field("definition", &single_step_result::definition)
        .field("output", &single_step_result::output)
        .field("error", &single_step_result::error);

    emscripten::value_object<load_result>("LoadResult")
        .field("success", &load_result::success)
        .field("loaded", &load_result::loaded)
        .field("line", &load_result::line)
        .field("error", &load_result::error);

    emscripten::value_object<definition_inspection_result>(
        "DefinitionInspectionResult")
        .field("success", &definition_inspection_result::success)
        .field("definition", &definition_inspection_result::definition)
        .field(
            "replacement",
            &definition_inspection_result::replacement)
        .field("error", &definition_inspection_result::error);

    emscripten::function(
        "inspectDefinition", &inspect_definition_input);
    emscripten::function("parseEval", &parse_eval_input);
    emscripten::function("singleStepRun", &single_step_run_input);
    emscripten::function("colorStepRun", &color_step_run_input);
    emscripten::function("beginSingleStep", &begin_single_step_input);
    emscripten::function("takeSingleStep", &take_single_step);
    emscripten::function("setList", &combdsl::set_list);
    emscripten::function("loadSetList", &load_set_list_input);
}
