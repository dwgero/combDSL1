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

#include <combdsl/combinators.hpp>

#include <emscripten/bind.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

struct load_result {
    bool success;
    std::size_t loaded;
    std::size_t line;
    std::string error;
};

std::optional<combdsl::quoted_expression> stepped_expression;

[[nodiscard]] bool blank_record(std::string_view record) noexcept {
    return std::ranges::all_of(record, [](char value) {
        return value == ' ' || value == '\t' || value == '\r' ||
               value == '\n' || value == '\f' || value == '\v';
    });
}

[[nodiscard]] load_result load_set_list_input(
    std::string const& source) {
    combdsl::detail::registered_parser_basis_table previous_bases;
    std::vector<std::string> previous_definitions;

    try {
        std::lock_guard lock(
            combdsl::detail::parser_basis_registry_mutex());
        previous_bases = combdsl::detail::parser_basis_registry();
        previous_definitions =
            combdsl::detail::parser_definition_registry();
    } catch (std::exception const& error) {
        return {false, 0, 0, error.what()};
    } catch (...) {
        return {false, 0, 0, "unknown loading error"};
    }

    auto restore_registry = [&] {
        std::lock_guard lock(
            combdsl::detail::parser_basis_registry_mutex());
        combdsl::detail::parser_basis_registry().swap(previous_bases);
        combdsl::detail::parser_definition_registry().swap(
            previous_definitions);
    };

    std::size_t loaded = 0;
    std::size_t record_start = 0;
    std::size_t record_line = 1;
    std::size_t current_line = 1;
    std::size_t error_line = 0;
    bool inside_word = false;

    auto load_record = [&](std::size_t record_end) {
        auto const record = std::string_view(source).substr(
            record_start, record_end - record_start);
        if (blank_record(record)) {
            return;
        }

        error_line = record_line;
        static_cast<void>(
            combdsl::parse(combdsl::input_escape(record)));
        ++loaded;
    };

    try {
        for (std::size_t position = 0;
             position < source.size();
             ++position) {
            auto const value = source[position];
            if (value == '"') {
                inside_word = !inside_word;
                continue;
            }
            if (value != '\r' && value != '\n') {
                continue;
            }

            auto const crlf =
                value == '\r' &&
                position + 1 < source.size() &&
                source[position + 1] == '\n';
            if (!inside_word) {
                load_record(position);
                if (crlf) {
                    ++position;
                }
                ++current_line;
                record_start = position + 1;
                record_line = current_line;
                continue;
            }

            if (crlf) {
                ++position;
            }
            ++current_line;
        }

        if (record_start < source.size()) {
            load_record(source.size());
        }
        return {true, loaded, 0, {}};
    } catch (std::exception const& error) {
        restore_registry();
        return {false, 0, error_line, error.what()};
    } catch (...) {
        restore_registry();
        return {false, 0, error_line, "unknown parsing error"};
    }
}

[[nodiscard]] evaluation_result parse_eval_input(std::string const& source) {
    std::istringstream input;
    std::ostringstream output;

    try {
        auto escaped_source = combdsl::input_escape(source);
        auto parsed = combdsl::detail::parse_input(escaped_source);
        if (parsed.is_definition) {
            return {true, true, {}, {}};
        }
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {true, false, output.str(), {}};
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
        if (parsed.is_display_only) {
            parsed.expression.print_to(output);
            output << '\n';
            return {true, false, output.str(), {}};
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
        if (parsed.is_display_only) {
            std::ostringstream output;
            combdsl::detail::print_quoted_html(
                output, parsed.expression);
            output << '\n';
            return {true, false, output.str(), {}};
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

    emscripten::function("parseEval", &parse_eval_input);
    emscripten::function("singleStepRun", &single_step_run_input);
    emscripten::function("colorStepRun", &color_step_run_input);
    emscripten::function("beginSingleStep", &begin_single_step_input);
    emscripten::function("takeSingleStep", &take_single_step);
    emscripten::function("setList", &combdsl::set_list);
    emscripten::function("loadSetList", &load_set_list_input);
}
