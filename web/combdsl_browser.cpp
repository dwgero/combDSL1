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
#include <utility>

namespace {

constexpr double evaluation_heartbeat_interval_ms = 100.0;
constexpr double evaluation_slice_budget_ms = 8.0;
constexpr std::size_t evaluation_progress_interval = 1000;

struct evaluation_result {
    bool success;
    bool definition;
    bool recover_worker;
    std::string output;
    std::string error;
    std::size_t reductions = 0;
    bool limit_reached = false;
};

struct single_step_result {
    bool success;
    bool reduced;
    bool complete;
    bool definition;
    std::string output;
    std::string error;
    bool limit_reached = false;
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
    bool display_only;
    bool show_all;
    bool find;
    std::string replacement;
    std::string error;
    bool step_limit_command = false;
    bool step_limit_enabled = false;
    std::size_t step_limit = 0;
};

std::optional<combdsl::quoted_expression> stepped_expression;
std::optional<std::size_t> stepped_limit;
std::size_t stepped_reductions = 0;

struct limited_evaluation_state {
    limited_evaluation_state(
        combdsl::quoted_expression expression_,
        std::size_t request_id_,
        std::size_t step_limit_,
        bool check_at_limit_)
        : expression(std::move(expression_)),
          request_id(request_id_),
          step_limit(step_limit_),
          check_at_limit(check_at_limit_),
          last_heartbeat_at(emscripten_get_now()) {}

    combdsl::quoted_expression expression;
    std::size_t request_id;
    std::size_t step_limit;
    bool check_at_limit;
    std::size_t window_reductions = 0;
    std::size_t total_reductions = 0;
    std::size_t progress_sequence = 0;
    double last_heartbeat_at;
};

std::optional<limited_evaluation_state> limited_evaluation;

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
        if (auto const command =
                combdsl::parse_step_limit_command(source)) {
            return {
                true,
                false,
                false,
                false,
                false,
                {},
                {},
                true,
                command->enabled,
                command->limit};
        }
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(
            escaped_source,
            combdsl::detail::parser_definition_mode::
                inspect_definitions);
        return {
            true,
            parsed.is_definition,
            parsed.is_display_only,
            parsed.is_show_all,
            parsed.is_find,
            std::move(parsed.replaced_definition),
            {}};
    } catch (std::exception const& error) {
        return {
            false, false, false, false, false, {}, error.what()};
    } catch (...) {
        return {
            false,
            false,
            false,
            false,
            false,
            {},
            "unknown definition inspection error"};
    }
}

[[nodiscard]] combdsl::evaluation_progress_callback
make_evaluation_progress_callback(
    std::size_t request_id,
    std::size_t& completed_reductions) {
    auto last_heartbeat_at = emscripten_get_now();
    std::size_t sequence = 0;
    return [
        request_id,
        &completed_reductions,
        last_heartbeat_at,
        sequence
    ](std::size_t reductions) mutable {
        completed_reductions = reductions;
        auto const now = emscripten_get_now();
        auto const reached_progress_milestone =
            reductions % evaluation_progress_interval == 0;
        if (!reached_progress_milestone &&
            now - last_heartbeat_at <
                evaluation_heartbeat_interval_ms) {
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

void reset_limited_evaluation() noexcept {
    limited_evaluation.reset();
}

void report_limited_evaluation_progress(
    limited_evaluation_state& state) {
    auto const now = emscripten_get_now();
    auto const reached_progress_milestone =
        state.total_reductions % evaluation_progress_interval == 0;
    if (!reached_progress_milestone &&
        now - state.last_heartbeat_at <
            evaluation_heartbeat_interval_ms) {
        return;
    }

    state.last_heartbeat_at = now;
    auto message = emscripten::val::object();
    message.set("type", std::string{"eval-progress"});
    message.set(
        "id", static_cast<double>(state.request_id));
    message.set(
        "sequence",
        static_cast<double>(++state.progress_sequence));
    message.set(
        "reductions",
        static_cast<double>(state.total_reductions));
    emscripten::val::global("self").call<void>(
        "postMessage", message);
}

[[nodiscard]] evaluation_result continue_limited_eval_input(
    std::size_t request_id) {
    if (!limited_evaluation ||
        limited_evaluation->request_id != request_id) {
        return {
            false,
            false,
            false,
            {},
            "no matching evaluation is ready to resume"};
    }

    std::ostringstream output;
    try {
        auto& state = *limited_evaluation;
        auto const slice_started_at = emscripten_get_now();
        combdsl::detail::reduction_options const options{
            .basis_step = false,
            .reduce_partial_k_argument = false,
        };
        bool slice_budget_reached = false;

        while (state.window_reductions < state.step_limit) {
            auto reduced = combdsl::detail::reduce_next_redex(
                state.expression, options);
            if (!reduced) {
                state.expression.print_to(output);
                output << '\n';
                auto const total_reductions =
                    state.total_reductions;
                reset_limited_evaluation();
                return {
                    true,
                    false,
                    false,
                    output.str(),
                    {},
                    total_reductions,
                    false};
            }

            state.expression = std::move(*reduced);
            ++state.window_reductions;
            ++state.total_reductions;
            report_limited_evaluation_progress(state);
            if (state.window_reductions < state.step_limit &&
                emscripten_get_now() - slice_started_at >=
                    evaluation_slice_budget_ms) {
                slice_budget_reached = true;
                break;
            }
        }

        auto const total_reductions = state.total_reductions;
        if (slice_budget_reached) {
            return {
                true,
                false,
                false,
                {},
                {},
                total_reductions,
                true};
        }

        if (!state.check_at_limit) {
            return {
                true,
                false,
                false,
                {},
                {},
                total_reductions,
                true};
        }

        auto const limit_reached =
            combdsl::detail::has_next_redex(
                state.expression, options);
        if (limit_reached) {
            return {
                true,
                false,
                false,
                {},
                {},
                total_reductions,
                true};
        }

        state.expression.print_to(output);
        output << '\n';
        reset_limited_evaluation();
        return {
            true,
            false,
            false,
            output.str(),
            {},
            total_reductions,
            false};
    } catch (std::exception const& error) {
        reset_limited_evaluation();
        return {false, false, true, {}, error.what()};
    } catch (...) {
        reset_limited_evaluation();
        return {
            false,
            false,
            true,
            {},
            "unknown evaluation error"};
    }
}

[[nodiscard]] evaluation_result begin_limited_eval_input(
    std::string const& source,
    std::size_t request_id,
    std::size_t step_limit,
    bool check_at_limit) {
    reset_limited_evaluation();
    if (step_limit == 0) {
        return {
            false,
            false,
            false,
            {},
            "step limit must be greater than zero"};
    }
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {
                true, parsed.is_definition, false,
                output.str(), {}};
        }
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }

        limited_evaluation.emplace(
            std::move(parsed.expression), request_id, step_limit,
            check_at_limit);
        return continue_limited_eval_input(request_id);
    } catch (std::exception const& error) {
        reset_limited_evaluation();
        return {false, false, false, {}, error.what()};
    } catch (...) {
        reset_limited_evaluation();
        return {
            false, false, false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] evaluation_result resume_limited_eval_input(
    std::size_t request_id,
    std::size_t step_limit,
    bool check_at_limit) {
    if (!limited_evaluation ||
        limited_evaluation->request_id != request_id) {
        return {
            false,
            false,
            false,
            {},
            "no matching evaluation is ready to resume"};
    }
    if (step_limit == 0) {
        return {
            false,
            false,
            false,
            {},
            "step limit must be greater than zero"};
    }

    limited_evaluation->step_limit = step_limit;
    limited_evaluation->check_at_limit = check_at_limit;
    limited_evaluation->window_reductions = 0;
    return continue_limited_eval_input(request_id);
}

[[nodiscard]] evaluation_result parse_eval_input(
    std::string const& source,
    std::size_t request_id,
    bool step_limit_enabled,
    std::size_t step_limit) {
    if (step_limit_enabled) {
        return begin_limited_eval_input(
            source, request_id, step_limit, true);
    }

    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {
                true, parsed.is_definition, false,
                output.str(), {}};
        }
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }

        try {
            std::size_t completed_reductions = 0;
            auto progress =
                make_evaluation_progress_callback(
                    request_id, completed_reductions);
            auto const outcome = combdsl::eval_with_outcome(
                std::move(parsed.expression),
                output,
                input,
                false,
                progress,
                step_limit_enabled
                    ? std::optional{step_limit}
                    : std::nullopt);
            return {
                true, false, false, output.str(), {},
                completed_reductions,
                outcome ==
                    combdsl::evaluation_outcome::
                        step_limit_reached};
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
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {
                true, parsed.is_definition, false,
                output.str(), {}};
        }
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }

        try {
            std::size_t completed_reductions = 0;
            auto progress =
                make_evaluation_progress_callback(
                    request_id, completed_reductions);
            combdsl::single_step_run(
                std::move(parsed.expression), output, input, basis_step,
                progress);
            return {
                true, false, false, output.str(), {},
                completed_reductions};
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

[[nodiscard]] evaluation_result color_step_html_run_input(
    std::string const& source,
    bool basis_step,
    std::size_t request_id) {
    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_display_only) {
            std::ostringstream output;
            combdsl::detail::print_quoted_html(
                output, parsed.expression);
            output << '\n';
            return {
                true, parsed.is_definition, false,
                output.str(), {}};
        }
        if (parsed.is_definition) {
            return {true, true, false, {}, {}};
        }

        try {
            auto expression = std::move(parsed.expression);
            std::ostringstream output;
            bool reduced = false;
            std::size_t completed_reductions = 0;
            auto progress =
                make_evaluation_progress_callback(
                    request_id, completed_reductions);
            combdsl::detail::evaluation_progress_reporter reporter(
                progress);

            for (;;) {
                std::ostringstream step_output;
                auto next = combdsl::color_step_html(
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

            return {
                true, false, false, output.str(), {},
                completed_reductions};
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

void reset_stepped_evaluation() noexcept {
    stepped_expression.reset();
    stepped_limit.reset();
    stepped_reductions = 0;
}

[[nodiscard]] bool same_stepped_expression(
    combdsl::quoted_expression const& left,
    combdsl::quoted_expression const& right) noexcept {
    return combdsl::detail::quoted_access::root(left) ==
           combdsl::detail::quoted_access::root(right);
}

[[nodiscard]] single_step_result begin_single_step_input(
    std::string const& source,
    bool basis_step,
    bool step_limit_enabled,
    std::size_t step_limit) {
    reset_stepped_evaluation();

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_display_only) {
            std::ostringstream output;
            parsed.expression.print_to(output);
            output << '\n';
            return {
                true, false, true, parsed.is_definition,
                output.str(), {}};
        }
        if (parsed.is_definition) {
            return {true, false, true, true, {}, {}};
        }
        stepped_expression.emplace(std::move(parsed.expression));
        stepped_limit = step_limit_enabled
            ? std::optional{step_limit}
            : std::nullopt;
        if (stepped_limit && *stepped_limit == 0) {
            auto const limit_reached =
                combdsl::detail::has_next_redex(
                    *stepped_expression,
                    combdsl::detail::reduction_options{
                        .basis_step = basis_step,
                    });
            std::ostringstream output;
            stepped_expression->print_to(output);
            output << '\n';
            reset_stepped_evaluation();
            return {
                true,
                false,
                true,
                false,
                output.str(),
                {},
                limit_reached};
        }
        return {true, false, false, false, {}, {}};
    } catch (std::exception const& error) {
        reset_stepped_evaluation();
        return {false, false, false, false, {}, error.what()};
    } catch (...) {
        reset_stepped_evaluation();
        return {
            false, false, false, false, {}, "unknown parsing error"};
    }
}

[[nodiscard]] single_step_result take_single_step(
    bool basis_step, bool colorize, bool look_ahead) {
    if (!stepped_expression.has_value()) {
        return {
            false, false, false, false, {},
            "no expression is ready to step"};
    }

    try {
        std::ostringstream output;
        auto next = colorize
            ? combdsl::color_step_html(
                  *stepped_expression, output, basis_step)
            : combdsl::single_step(
                  *stepped_expression, basis_step);
        if (same_stepped_expression(
                next, *stepped_expression)) {
            if (colorize && !look_ahead) {
                output.str({});
                output.clear();
                combdsl::detail::print_quoted_html(
                    output, *stepped_expression);
                output << '\n';
            }
            auto final_output = output.str();
            reset_stepped_evaluation();
            return {
                true, false, true, false,
                std::move(final_output), {}};
        }

        stepped_expression = std::move(next);
        ++stepped_reductions;
        bool complete = false;
        auto const limit_exhausted =
            stepped_limit &&
            stepped_reductions >= *stepped_limit;
        if (look_ahead || limit_exhausted) {
            complete = !combdsl::detail::has_next_redex(
                *stepped_expression,
                combdsl::detail::reduction_options{
                    .basis_step = basis_step,
                });
        }
        auto const limit_reached =
            limit_exhausted && !complete;
        auto const stopped = complete || limit_reached;
        if (colorize) {
            if (stopped) {
                combdsl::detail::print_quoted_html(
                    output, *stepped_expression);
                output << '\n';
            }
        } else {
            stepped_expression->print_to(output);
            output << '\n';
        }
        if (complete) {
            reset_stepped_evaluation();
        }
        return {
            true,
            true,
            stopped,
            false,
            output.str(),
            {},
            limit_reached};
    } catch (std::exception const& error) {
        reset_stepped_evaluation();
        return {false, false, false, false, {}, error.what()};
    } catch (...) {
        reset_stepped_evaluation();
        return {
            false, false, false, false, {},
            "unknown evaluation error"};
    }
}

[[nodiscard]] bool resume_stepped_evaluation() noexcept {
    if (!stepped_expression) {
        return false;
    }

    stepped_reductions = 0;
    return true;
}

} // namespace

EMSCRIPTEN_BINDINGS(combdsl_browser) {
    emscripten::value_object<evaluation_result>("EvaluationResult")
        .field("success", &evaluation_result::success)
        .field("definition", &evaluation_result::definition)
        .field("recoverWorker", &evaluation_result::recover_worker)
        .field("output", &evaluation_result::output)
        .field("error", &evaluation_result::error)
        .field("reductions", &evaluation_result::reductions)
        .field("limitReached", &evaluation_result::limit_reached);

    emscripten::value_object<single_step_result>("SingleStepResult")
        .field("success", &single_step_result::success)
        .field("reduced", &single_step_result::reduced)
        .field("complete", &single_step_result::complete)
        .field("definition", &single_step_result::definition)
        .field("output", &single_step_result::output)
        .field("error", &single_step_result::error)
        .field("limitReached", &single_step_result::limit_reached);

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
            "displayOnly",
            &definition_inspection_result::display_only)
        .field("showAll", &definition_inspection_result::show_all)
        .field("find", &definition_inspection_result::find)
        .field(
            "replacement",
            &definition_inspection_result::replacement)
        .field("error", &definition_inspection_result::error)
        .field(
            "stepLimitCommand",
            &definition_inspection_result::step_limit_command)
        .field(
            "stepLimitEnabled",
            &definition_inspection_result::step_limit_enabled)
        .field(
            "stepLimit",
            &definition_inspection_result::step_limit);

    emscripten::function(
        "inspectDefinition", &inspect_definition_input);
    emscripten::function("parseEval", &parse_eval_input);
    emscripten::function(
        "beginLimitedEval", &begin_limited_eval_input);
    emscripten::function(
        "resumeLimitedEval", &resume_limited_eval_input);
    emscripten::function("singleStepRun", &single_step_run_input);
    emscripten::function("colorStepRun", &color_step_html_run_input);
    emscripten::function("beginSingleStep", &begin_single_step_input);
    emscripten::function("takeSingleStep", &take_single_step);
    emscripten::function(
        "resumeSingleStep", &resume_stepped_evaluation);
    emscripten::function("setList", &combdsl::set_list);
    emscripten::function("loadSetList", &load_set_list_input);
}
