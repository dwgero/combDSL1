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

#pragma once

#include <combdsl/combinators.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace combdsl::web_detail {

struct file_parse_diagnostic {
    std::size_t line;
    std::size_t position;
    std::string detail;
};

struct set_list_load_result {
    bool success;
    bool aborted;
    std::size_t loaded;
    std::vector<file_parse_diagnostic> diagnostics;
    std::size_t fatal_line;
    std::string fatal_error;
};

inline constexpr std::size_t maximum_file_parse_errors = 15;

[[nodiscard]] inline bool blank_record(
    std::string_view record) noexcept {
    return std::ranges::all_of(record, [](char value) {
        return value == ' ' || value == '\t' || value == '\r' ||
               value == '\n' || value == '\f' || value == '\v';
    });
}

[[nodiscard]] inline set_list_load_result load_set_list(
    std::string_view source) {
    std::lock_guard transaction_lock(
        detail::parser_definition_transaction_mutex());
    detail::registered_parser_basis_table previous_bases;
    detail::parser_basis_version_history previous_versions;
    detail::parser_live_binding_table previous_live_bindings;
    detail::registered_parser_basis_table previous_live_targets;
    detail::parser_definition_history previous_definitions;
    bool previous_snapshot_enabled = true;

    try {
        std::lock_guard lock(
            detail::parser_basis_registry_mutex());
        previous_bases = detail::parser_basis_registry();
        previous_versions =
            detail::parser_basis_version_registry();
        previous_live_bindings =
            detail::parser_live_binding_registry();
        for (auto const& [name, binding] :
             previous_live_bindings) {
            if (binding) {
                auto target = std::atomic_load(&binding->target);
                if (!target) {
                    continue;
                }
                previous_live_targets.emplace(
                    name, std::move(target));
            }
        }
        previous_definitions =
            detail::parser_definition_registry();
        previous_snapshot_enabled =
            detail::parser_snapshot_enabled();
    } catch (std::exception const& error) {
        return {
            false, false, 0, {}, 0, error.what()};
    } catch (...) {
        return {
            false, false, 0, {}, 0, "unknown loading error"};
    }

    auto restore_registry = [&] {
        std::lock_guard lock(
            detail::parser_basis_registry_mutex());
        detail::parser_basis_registry().swap(previous_bases);
        detail::parser_basis_version_registry().swap(
            previous_versions);
        for (auto const& [name, binding] :
             previous_live_bindings) {
            if (!binding) {
                continue;
            }
            auto const target = previous_live_targets.find(name);
            auto restored_target =
                target == previous_live_targets.end()
                    ? detail::registered_parser_basis_ptr{}
                    : target->second;
            std::atomic_store(
                &binding->target, std::move(restored_target));
        }
        detail::parser_live_binding_registry().swap(
            previous_live_bindings);
        detail::parser_definition_registry().swap(
            previous_definitions);
        std::swap(
            detail::parser_snapshot_enabled(),
            previous_snapshot_enabled);
    };

    std::size_t loaded = 0;
    std::size_t record_start = 0;
    std::size_t record_line = 1;
    std::size_t current_line = 1;
    std::size_t error_line = 0;
    std::vector<file_parse_diagnostic> diagnostics;
    bool inside_word = false;
    bool aborted = false;

    auto load_record = [&](std::size_t record_end) {
        auto const record = source.substr(
            record_start, record_end - record_start);
        if (blank_record(record)) {
            return true;
        }

        error_line = record_line;
        try {
            static_cast<void>(
                parse(input_escape(record)));
            ++loaded;
        } catch (parse_error const& error) {
            diagnostics.push_back({
                error_line,
                error.position(),
                std::string(error.detail())});
            if (diagnostics.size() >=
                maximum_file_parse_errors) {
                aborted = true;
                return false;
            }
        }
        return true;
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
                if (!load_record(position)) {
                    break;
                }
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

        if (!aborted && record_start < source.size()) {
            static_cast<void>(load_record(source.size()));
        }
        if (!diagnostics.empty()) {
            restore_registry();
            return {
                false, aborted, 0, std::move(diagnostics), 0, {}};
        }
        return {true, false, loaded, {}, 0, {}};
    } catch (std::exception const& error) {
        restore_registry();
        return {
            false, false, 0, std::move(diagnostics),
            error_line, error.what()};
    } catch (...) {
        restore_registry();
        return {
            false, false, 0, std::move(diagnostics),
            error_line, "unknown parsing error"};
    }
}

inline void append_load_message(
    std::string& messages,
    std::string_view message) {
    if (!messages.empty()) {
        messages.push_back('\n');
    }
    messages += message;
}

[[nodiscard]] inline std::string format_file_load_error(
    std::string_view filename,
    std::size_t line,
    std::string_view message) {
    std::string result("Error while loading file ");
    result += filename;
    if (line != 0) {
        result += " on line ";
        result += std::to_string(line);
    }
    result += ": ";
    result += message;
    return result;
}

[[nodiscard]] inline std::string format_file_parse_error(
    std::string_view filename,
    file_parse_diagnostic const& diagnostic) {
    auto const displayed_position =
        diagnostic.position ==
            std::numeric_limits<std::size_t>::max()
            ? diagnostic.position
            : diagnostic.position + 1;

    std::string result = "Parse error in file ";
    result += filename;
    result += " on line ";
    result += std::to_string(diagnostic.line);
    result += " at position ";
    result += std::to_string(displayed_position);
    result += ": ";
    result += diagnostic.detail;
    return result;
}

[[nodiscard]] inline std::string format_file_load_diagnostics(
    std::string_view filename,
    set_list_load_result const& load_result) {
    std::string result;
    for (auto const& diagnostic : load_result.diagnostics) {
        append_load_message(
            result,
            format_file_parse_error(filename, diagnostic));
    }
    if (!load_result.fatal_error.empty()) {
        append_load_message(
            result,
            format_file_load_error(
                filename,
                load_result.fatal_line,
                load_result.fatal_error));
    }
    if (!load_result.success && !load_result.aborted) {
        append_load_message(
            result,
            "Errors are preventing any changes from being made");
    }
    if (load_result.aborted) {
        append_load_message(
            result,
            "Too many errors, aborting with no changes made");
    }
    return result;
}

} // namespace combdsl::web_detail
