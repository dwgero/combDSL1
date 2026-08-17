/*
 * Combinator Read-Eval-Print Loop
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

#include "combdsl/combinators.hpp"
#include "combdsl/color_step_ansi.hpp"

#include "fmt/color.h"

#include "web/load_set_list.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "readline/history.h"
#include "readline/readline.h"

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view crepl_version = "2.12.4";
constexpr std::string_view no_further_reductions_message =
    "No further reductions";

[[nodiscard]] bool stream_is_terminal(std::FILE* stream) noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stream)) != 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(::fileno(stream)) != 0;
#else
    static_cast<void>(stream);
    return false;
#endif
}

void write_red_message(
    std::ostream& output,
    std::string_view message,
    bool use_terminal_color) {
    if (!output.good()) {
        return;
    }
    if (use_terminal_color) {
        auto const rendered = fmt::format(
            fmt::fg(fmt::color::red), "{}", message);
        output.write(
            rendered.data(),
            static_cast<std::streamsize>(rendered.size()));
        return;
    }
    output.write(
        message.data(),
        static_cast<std::streamsize>(message.size()));
}

void print_red_message_line(
    std::ostream& output,
    std::string_view message,
    bool use_terminal_color) {
    write_red_message(output, message, use_terminal_color);
    output.put('\n');
    output.flush();
}

[[nodiscard]] std::string step_limit_message(
    std::size_t reductions) {
    auto message = std::string("[step limit reached after ");
    message += std::to_string(reductions);
    message += reductions == 1 ? " step]" : " steps]";
    return message;
}

inline constexpr std::string_view crepl_resume_prompt_suffix =
    " Press Enter to resume; press q or Q to quit.\n";

[[nodiscard]] combdsl::evaluation_step_limit_callback
make_step_limit_callback(
    bool enabled,
    std::ostream& output,
    bool use_terminal_color,
    std::function<void()> before_prompt = {});

[[nodiscard]] combdsl::evaluation_interrupt_callback
make_interrupt_callback(
    bool enabled,
    std::ostream& output,
    bool use_terminal_color,
    std::function<void()> before_prompt = {});

void report_evaluation_outcome(
    combdsl::evaluation_outcome outcome,
    std::ostream& output,
    bool use_terminal_color,
    std::optional<std::size_t> step_limit = std::nullopt) {
    if (outcome == combdsl::evaluation_outcome::cancelled) {
        print_red_message_line(
            output, "[cancelled]", use_terminal_color);
        return;
    }
    if (outcome ==
            combdsl::evaluation_outcome::step_limit_reached &&
        step_limit) {
        print_red_message_line(
            output,
            step_limit_message(*step_limit),
            use_terminal_color);
    }
}

void print_crepl_banner(std::ostream& output) {
    output << "Combinator Read-Eval-Print Loop, version "
           << crepl_version;
}

[[nodiscard]] bool is_command_whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

enum class stepping_mode {
    none,
    single,
    key
};

enum class mode_command_kind {
    single,
    key,
    basis,
    colorize
};

struct mode_command {
    mode_command_kind kind;
    bool enabled;
};

enum class help_detail {
    brief,
    full
};

struct completion_candidates {
    std::string_view const* data = nullptr;
    std::size_t size = 0;
};

constexpr std::array<std::string_view, 28> command_completion_candidates = {
    "about", "abstract", "basis", "birds", "colorize", "compare",
    "define", "depends", "depends-on", "dependson", "exit", "find", "help",
    "inspect", "key", "load", "quit", "references", "remove",
    "revisions", "save", "set", "show", "single", "step", "used",
    "used-by", "usedby"};
constexpr std::array<std::string_view, 1> step_completion_candidates = {
    "step"};
constexpr std::array<std::string_view, 1>
    step_limit_completion_candidates = {"limit"};
constexpr std::array<std::string_view, 1>
    step_limit_option_completion_candidates = {"off"};
constexpr std::array<std::string_view, 2> toggle_completion_candidates = {
    "off", "on"};
constexpr std::array<std::string_view, 2> help_completion_candidates = {
    "brief", "full"};
constexpr std::array<std::string_view, 1>
    abstract_question_completion_candidates = {"?"};
constexpr std::array<std::string_view, 2>
    abstract_trace_completion_candidates = {"steps", "ministeps"};
constexpr std::array<std::string_view, 2>
    definition_reference_completion_candidates = {
        "captured", "live"};
constexpr std::array<std::string_view, 1>
    depends_on_completion_candidates = {"on"};
constexpr std::array<std::string_view, 1>
    used_by_completion_candidates = {"by"};
constexpr std::array<std::string_view, 1>
    dependency_all_completion_candidates = {"all"};
constexpr std::array<std::string_view, 2>
    uses_dependency_option_completion_candidates = {"all", "path"};
constexpr std::array<std::string_view, 1>
    dependency_path_between_completion_candidates = {"between"};
constexpr std::array<std::string_view, 1>
    dependency_path_and_completion_candidates = {"and"};
constexpr std::array<std::string_view, 1> show_completion_candidates = {
    "all"};
constexpr std::array<std::string_view, 6> find_completion_candidates = {
    "all", "among", "1", "2", "3", "4"};
constexpr std::array<std::string_view, 5>
    find_after_all_completion_candidates = {
        "among", "1", "2", "3", "4"};
std::string last_save_filename = "set_list.cmb";
std::array<std::string_view, 1> save_filename_completion_candidates;
std::string last_load_filename = "set_list.cmb";
std::array<std::string_view, 1> load_filename_completion_candidates;

constexpr int persistent_history_limit = 500;
constexpr std::string_view settings_header = "crepl-settings 1";

struct crepl_persistence_paths {
    std::filesystem::path directory;
    std::string history;
    std::filesystem::path settings;
};

[[nodiscard]] std::optional<std::filesystem::path> environment_path(
    char const* name) {
    auto const* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

[[nodiscard]] std::optional<crepl_persistence_paths>
make_crepl_persistence_paths() noexcept {
    try {
        auto home = environment_path("HOME");
#if defined(_WIN32)
        if (!home) {
            home = environment_path("USERPROFILE");
        }
#endif
        if (!home) {
            return std::nullopt;
        }
        auto const directory = *home / ".crepl";

        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::nullopt;
        }
        std::filesystem::permissions(
            directory,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            error);
        if (error) {
            return std::nullopt;
        }

        return crepl_persistence_paths{
            directory,
            (directory / "history").string(),
            directory / "settings"};
    } catch (...) {
        return std::nullopt;
    }
}

class scoped_history_operation_lock {
public:
    explicit scoped_history_operation_lock(
        crepl_persistence_paths const& paths) noexcept {
#if defined(__unix__) || defined(__APPLE__)
        do {
            descriptor_ = ::open(paths.directory.c_str(), O_RDONLY);
        } while (descriptor_ < 0 && errno == EINTR);
        int result = -1;
        if (descriptor_ >= 0) {
            do {
                result = ::flock(descriptor_, LOCK_EX);
            } while (result != 0 && errno == EINTR);
        }
        if (descriptor_ >= 0 && result != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#else
        static_cast<void>(paths);
#endif
    }

    scoped_history_operation_lock(
        scoped_history_operation_lock const&) = delete;
    scoped_history_operation_lock& operator=(
        scoped_history_operation_lock const&) = delete;

    [[nodiscard]] bool acquired() const noexcept {
#if defined(__unix__) || defined(__APPLE__)
        return descriptor_ >= 0;
#else
        return true;
#endif
    }

    ~scoped_history_operation_lock() {
#if defined(__unix__) || defined(__APPLE__)
        if (descriptor_ >= 0) {
            ::flock(descriptor_, LOCK_UN);
            ::close(descriptor_);
        }
#endif
    }

private:
#if defined(__unix__) || defined(__APPLE__)
    int descriptor_ = -1;
#endif
};

[[nodiscard]] std::optional<std::filesystem::path>
resolve_history_storage_path(
    crepl_persistence_paths const& paths) noexcept {
    try {
        auto resolved = std::filesystem::path(paths.history);
        for (int links = 0; links < 40; ++links) {
            std::error_code error;
            auto const status =
                std::filesystem::symlink_status(resolved, error);
            if (error == std::errc::no_such_file_or_directory) {
                return resolved;
            }
            if (error) {
                return std::nullopt;
            }
            if (!std::filesystem::is_symlink(status)) {
                return resolved;
            }
            auto target = std::filesystem::read_symlink(resolved, error);
            if (error) {
                return std::nullopt;
            }
            if (target.is_relative()) {
                target = resolved.parent_path() / target;
            }
            resolved = target.lexically_normal();
        }
    } catch (...) {
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
read_persistent_history_lines(
    crepl_persistence_paths const& paths) {
    std::error_code error;
    auto const exists = std::filesystem::exists(paths.history, error);
    if (error) {
        return std::nullopt;
    }
    if (!exists) {
        return std::vector<std::string>{};
    }

    std::ifstream input(paths.history, std::ios::in | std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
    }
    if (!input.eof()) {
        return std::nullopt;
    }
    return lines;
}

[[nodiscard]] bool write_persistent_history_lines(
    crepl_persistence_paths const& paths,
    std::vector<std::string> const& lines) noexcept {
    try {
        auto const destination = resolve_history_storage_path(paths);
        if (!destination) {
            return false;
        }
        auto temporary = *destination;
#if defined(_WIN32)
        auto const process_id = _getpid();
#else
        auto const process_id = getpid();
#endif
        temporary += "." + std::to_string(process_id) + ".tmp";
        {
            std::ofstream output(
                temporary,
                std::ios::out | std::ios::binary | std::ios::trunc);
            for (auto const& line : lines) {
                output << line << '\n';
            }
            output.close();
            if (!output) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }

        std::error_code ignored;
        std::filesystem::permissions(
            temporary,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            ignored);

#if defined(_WIN32)
        auto const installed = MoveFileExW(
            temporary.c_str(),
            destination->c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (installed == 0) {
            std::filesystem::remove(temporary, ignored);
            return false;
        }
#else
        std::filesystem::rename(
            temporary, *destination, ignored);
        if (ignored) {
            std::filesystem::remove(temporary, ignored);
            return false;
        }
#endif
        return true;
    } catch (...) {
        return false;
    }
}

template<typename Mutation>
void mutate_persistent_history(
    crepl_persistence_paths const& paths,
    Mutation&& mutation) noexcept {
    try {
        scoped_history_operation_lock const lock(paths);
        if (!lock.acquired()) {
            return;
        }
        auto lines = read_persistent_history_lines(paths);
        if (!lines || !std::invoke(mutation, *lines)) {
            return;
        }
        static_cast<void>(write_persistent_history_lines(paths, *lines));
    } catch (...) {
    }
}

struct serialized_history_segment {
    std::size_t entry = 0;
    std::string_view line;
};

[[nodiscard]] std::vector<serialized_history_segment>
serialize_session_history(
    std::vector<std::string> const& entries) {
    std::vector<serialized_history_segment> segments;
    for (std::size_t entry = 0; entry < entries.size(); ++entry) {
        auto const source = std::string_view(entries[entry]);
        std::size_t start = 0;
        while (true) {
            auto const newline = source.find('\n', start);
            if (newline == std::string_view::npos) {
                segments.push_back(
                    {entry, source.substr(start)});
                break;
            }
            segments.push_back(
                {entry, source.substr(start, newline - start)});
            start = newline + 1;
        }
    }
    return segments;
}

void append_serialized_history_entry(
    std::vector<std::string>& lines,
    std::string_view source) {
    std::size_t start = 0;
    while (true) {
        auto const newline = source.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.emplace_back(source.substr(start));
            return;
        }
        lines.emplace_back(source.substr(start, newline - start));
        start = newline + 1;
    }
}

[[nodiscard]] bool is_gnu_history_timestamp(
    std::string_view line) noexcept {
    return line.size() >= 2 && line.front() == '#' &&
        line[1] >= '0' && line[1] <= '9';
}

[[nodiscard]] bool uses_gnu_history_timestamps(
    std::vector<std::string> const& disk_lines) noexcept {
    return !disk_lines.empty() &&
        is_gnu_history_timestamp(disk_lines.front());
}

struct persistent_history_alignment {
    std::vector<std::optional<std::size_t>> disk_owners;
    std::vector<bool> complete_entries;
};

struct persistent_history_match {
    std::size_t segment = 0;
    std::size_t disk = 0;
};

template<typename Matches>
[[nodiscard]] std::optional<std::vector<persistent_history_match>>
align_persistent_history_sequences(
    std::size_t segment_size,
    std::size_t disk_size,
    Matches&& matches) {
    constexpr std::size_t maximum_edit_distance = 1024;
    constexpr std::size_t maximum_match_attempts = 32 * 1024 * 1024;

    // Common concurrent changes are pure appends or pure removals.  Match
    // those in linear time before applying the bounded general diff, choosing
    // the earliest possible duplicate on each side just as the prior LCS did.
    if (segment_size <= disk_size) {
        std::vector<persistent_history_match> result;
        result.reserve(segment_size);
        std::size_t disk = 0;
        for (std::size_t segment = 0;
             segment < segment_size;
             ++segment) {
            while (disk < disk_size &&
                   !std::invoke(matches, segment, disk)) {
                ++disk;
            }
            if (disk == disk_size) {
                result.clear();
                break;
            }
            result.push_back({segment, disk});
            ++disk;
        }
        if (result.size() == segment_size) {
            return result;
        }
    }
    if (disk_size <= segment_size) {
        std::vector<persistent_history_match> result;
        result.reserve(disk_size);
        std::size_t segment = 0;
        for (std::size_t disk = 0; disk < disk_size; ++disk) {
            while (segment < segment_size &&
                   !std::invoke(matches, segment, disk)) {
                ++segment;
            }
            if (segment == segment_size) {
                result.clear();
                break;
            }
            result.push_back({segment, disk});
            ++segment;
        }
        if (result.size() == disk_size) {
            return result;
        }
    }

    if (segment_size >
            static_cast<std::size_t>(
                std::numeric_limits<std::ptrdiff_t>::max()) ||
        disk_size >
            static_cast<std::size_t>(
                std::numeric_limits<std::ptrdiff_t>::max())) {
        return std::nullopt;
    }
    auto const length_difference = segment_size > disk_size
        ? segment_size - disk_size
        : disk_size - segment_size;
    if (length_difference > maximum_edit_distance) {
        return std::nullopt;
    }

    auto const distance_limit = std::min(
        maximum_edit_distance,
        segment_size <=
                std::numeric_limits<std::size_t>::max() - disk_size
            ? segment_size + disk_size
            : maximum_edit_distance);
    auto const offset = static_cast<std::ptrdiff_t>(distance_limit + 1);
    std::vector<std::ptrdiff_t> furthest(
        2 * distance_limit + 3, -1);
    furthest[static_cast<std::size_t>(offset + 1)] = 0;
    std::vector<std::vector<std::ptrdiff_t>> trace;
    trace.reserve(distance_limit + 1);
    std::size_t match_attempts = 0;
    std::optional<std::size_t> final_distance;

    for (std::size_t distance = 0;
         distance <= distance_limit;
         ++distance) {
        trace.push_back(furthest);
        auto const signed_distance =
            static_cast<std::ptrdiff_t>(distance);
        for (auto diagonal = -signed_distance;
             diagonal <= signed_distance;
             diagonal += 2) {
            auto const diagonal_index =
                static_cast<std::size_t>(offset + diagonal);
            std::ptrdiff_t segment = 0;
            if (diagonal == -signed_distance ||
                (diagonal != signed_distance &&
                 furthest[diagonal_index - 1] <
                     furthest[diagonal_index + 1])) {
                segment = furthest[diagonal_index + 1];
            } else {
                segment = furthest[diagonal_index - 1] + 1;
            }
            auto disk = segment - diagonal;
            while (segment >= 0 && disk >= 0 &&
                   static_cast<std::size_t>(segment) < segment_size &&
                   static_cast<std::size_t>(disk) < disk_size) {
                if (++match_attempts > maximum_match_attempts) {
                    return std::nullopt;
                }
                if (!std::invoke(
                        matches,
                        static_cast<std::size_t>(segment),
                        static_cast<std::size_t>(disk))) {
                    break;
                }
                ++segment;
                ++disk;
            }
            furthest[diagonal_index] = segment;
            if (static_cast<std::size_t>(segment) >= segment_size &&
                static_cast<std::size_t>(disk) >= disk_size) {
                final_distance = distance;
                break;
            }
        }
        if (final_distance) {
            break;
        }
    }
    if (!final_distance) {
        return std::nullopt;
    }

    std::vector<persistent_history_match> result;
    result.reserve(std::min(segment_size, disk_size));
    auto segment = static_cast<std::ptrdiff_t>(segment_size);
    auto disk = static_cast<std::ptrdiff_t>(disk_size);
    for (auto distance = *final_distance; distance > 0; --distance) {
        auto const& previous = trace[distance];
        auto const signed_distance =
            static_cast<std::ptrdiff_t>(distance);
        auto const diagonal = segment - disk;
        auto const diagonal_index =
            static_cast<std::size_t>(offset + diagonal);
        auto const previous_diagonal =
            diagonal == -signed_distance ||
                (diagonal != signed_distance &&
                 previous[diagonal_index - 1] <
                     previous[diagonal_index + 1])
            ? diagonal + 1
            : diagonal - 1;
        auto const previous_segment = previous[
            static_cast<std::size_t>(offset + previous_diagonal)];
        auto const previous_disk =
            previous_segment - previous_diagonal;
        while (segment > previous_segment && disk > previous_disk) {
            result.push_back(
                {static_cast<std::size_t>(segment - 1),
                 static_cast<std::size_t>(disk - 1)});
            --segment;
            --disk;
        }
        segment = previous_segment;
        disk = previous_disk;
    }
    while (segment > 0 && disk > 0) {
        result.push_back(
            {static_cast<std::size_t>(segment - 1),
             static_cast<std::size_t>(disk - 1)});
        --segment;
        --disk;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

template<typename Matches>
[[nodiscard]] std::vector<persistent_history_match>
align_persistent_history_entry_blocks(
    std::vector<serialized_history_segment> const& segments,
    std::vector<std::string> const& disk_lines,
    std::vector<std::size_t> const& disk_content,
    Matches&& matches) {
    constexpr std::size_t maximum_match_attempts = 32 * 1024 * 1024;
    std::unordered_map<std::string_view, std::vector<std::size_t>>
        occurrences;
    occurrences.reserve(disk_content.size());
    for (std::size_t disk = 0; disk < disk_content.size(); ++disk) {
        auto const persisted =
            std::string_view(disk_lines[disk_content[disk]]);
        occurrences[persisted].push_back(disk);
        if (!persisted.empty() && persisted.back() == '\r') {
            occurrences[persisted.substr(0, persisted.size() - 1)]
                .push_back(disk);
        }
    }

    std::vector<persistent_history_match> result;
    result.reserve(std::min(segments.size(), disk_content.size()));
    std::size_t disk_cursor = 0;
    std::size_t match_attempts = 0;
    for (std::size_t first = 0; first < segments.size();) {
        auto const owner = segments[first].entry;
        auto last = first + 1;
        while (last < segments.size() &&
               segments[last].entry == owner) {
            ++last;
        }
        auto const segment_count = last - first;
        auto const found = occurrences.find(segments[first].line);
        if (found != occurrences.end()) {
            auto candidate = std::lower_bound(
                found->second.begin(),
                found->second.end(),
                disk_cursor);
            for (; candidate != found->second.end(); ++candidate) {
                if (*candidate > disk_content.size() ||
                    segment_count > disk_content.size() - *candidate) {
                    break;
                }
                auto const first_disk_line = disk_content[*candidate];
                auto complete = true;
                for (std::size_t offset = 0;
                     offset < segment_count;
                     ++offset) {
                    if (++match_attempts > maximum_match_attempts) {
                        return result;
                    }
                    if (disk_content[*candidate + offset] !=
                            first_disk_line + offset ||
                        !std::invoke(
                            matches, first + offset, *candidate + offset)) {
                        complete = false;
                        break;
                    }
                }
                if (!complete) {
                    continue;
                }
                for (std::size_t offset = 0;
                     offset < segment_count;
                     ++offset) {
                    result.push_back(
                        {first + offset, *candidate + offset});
                }
                disk_cursor = *candidate + segment_count;
                break;
            }
        }
        first = last;
    }
    return result;
}

[[nodiscard]] std::optional<persistent_history_alignment>
align_session_history_with_disk(
    std::vector<std::string> const& session_entries,
    std::vector<std::string> const& disk_lines) {
    auto const segments = serialize_session_history(session_entries);
    auto const segment_size = segments.size();
    auto const timestamps = uses_gnu_history_timestamps(disk_lines);
    std::vector<std::size_t> disk_content;
    disk_content.reserve(disk_lines.size());
    for (std::size_t index = 0; index < disk_lines.size(); ++index) {
        if (!timestamps ||
            !is_gnu_history_timestamp(disk_lines[index])) {
            disk_content.push_back(index);
        }
    }
    auto const segment_matches_disk =
        [&segments, &disk_lines, &disk_content](
            std::size_t segment,
            std::size_t disk) {
            auto const session = segments[segment].line;
            auto const& persisted = disk_lines[disk_content[disk]];
            return session == persisted ||
                (!persisted.empty() && persisted.back() == '\r' &&
                 session == std::string_view(persisted).substr(
                     0, persisted.size() - 1));
        };
    auto matches = align_persistent_history_sequences(
        segment_size,
        disk_content.size(),
        segment_matches_disk);
    if (!matches) {
        matches = align_persistent_history_entry_blocks(
            segments,
            disk_lines,
            disk_content,
            segment_matches_disk);
    }

    persistent_history_alignment alignment{
        std::vector<std::optional<std::size_t>>(disk_lines.size()),
        std::vector<bool>(session_entries.size(), false)};
    std::vector<std::size_t> expected_segments(
        session_entries.size());
    std::vector<std::size_t> matched_segments(
        session_entries.size());
    std::vector<std::optional<std::size_t>> last_matched_disk_line(
        session_entries.size());
    std::vector<bool> contiguous_entries(
        session_entries.size(), true);
    for (auto const& segment : segments) {
        ++expected_segments[segment.entry];
    }

    for (auto const& match : *matches) {
        auto const owner = segments[match.segment].entry;
        auto const disk_line = disk_content[match.disk];
        if (last_matched_disk_line[owner] &&
            disk_line != *last_matched_disk_line[owner] + 1) {
            contiguous_entries[owner] = false;
        }
        last_matched_disk_line[owner] = disk_line;
        alignment.disk_owners[disk_line] = owner;
        ++matched_segments[owner];
    }

    for (std::size_t entry = 0; entry < session_entries.size(); ++entry) {
        alignment.complete_entries[entry] =
            matched_segments[entry] == expected_segments[entry] &&
            contiguous_entries[entry];
    }
    for (auto& owner : alignment.disk_owners) {
        if (owner && !alignment.complete_entries[*owner]) {
            owner.reset();
        }
    }
    return alignment;
}

struct persistent_history_record {
    std::size_t start = 0;
    std::size_t end = 0;
    std::optional<std::size_t> owner;
};

[[nodiscard]] std::vector<persistent_history_record>
make_persistent_history_records(
    std::vector<std::string> const& disk_lines,
    persistent_history_alignment const& alignment) {
    std::vector<persistent_history_record> records;
    auto const timestamps = uses_gnu_history_timestamps(disk_lines);
    std::optional<std::size_t> pending_prefix;

    for (std::size_t index = 0; index < disk_lines.size(); ++index) {
        auto const owner = alignment.disk_owners[index];
        if (timestamps && is_gnu_history_timestamp(disk_lines[index])) {
            if (!pending_prefix) {
                pending_prefix = index;
            }
            continue;
        }

        // When owner is empty, plain Readline files have no multiline
        // boundary for concurrent lines, so keep the raw run opaque. This can
        // defer exact cap convergence until a clean later session.
        if (!pending_prefix && !records.empty() &&
            records.back().owner == owner &&
            records.back().end == index) {
            records.back().end = index + 1;
        } else {
            records.push_back(
                {pending_prefix.value_or(index), index + 1, owner});
        }
        pending_prefix.reset();
    }
    return records;
}

void cap_persistent_history_records(
    std::vector<std::string>& disk_lines,
    std::vector<persistent_history_record> const& records) {
    auto const limit = static_cast<std::size_t>(persistent_history_limit);
    if (records.size() <= limit) {
        return;
    }
    auto const first_retained = records[records.size() - limit].start;
    disk_lines.erase(
        disk_lines.begin(), disk_lines.begin() + first_retained);
}

void load_persistent_history(
    crepl_persistence_paths const& paths) noexcept {
    stifle_history(persistent_history_limit);
    scoped_history_operation_lock const lock(paths);
    if (!lock.acquired()) {
        return;
    }
    read_history(paths.history.c_str());
}

void append_persistent_history(
    crepl_persistence_paths const& paths,
    std::vector<std::string> const& session_entries,
    std::string_view source) noexcept {
    mutate_persistent_history(
        paths,
        [&session_entries, source](std::vector<std::string>& lines) {
            append_serialized_history_entry(lines, source);
            auto const alignment = align_session_history_with_disk(
                session_entries, lines);
            if (!alignment) {
                return false;
            }
            cap_persistent_history_records(
                lines,
                make_persistent_history_records(lines, *alignment));
            return true;
        });
}

void remove_persistent_history_entry(
    crepl_persistence_paths const& paths,
    std::vector<std::string> const& session_lines,
    std::size_t session_index) noexcept {
    mutate_persistent_history(
        paths,
        [&session_lines, session_index](
            std::vector<std::string>& disk_lines) {
            if (session_index >= session_lines.size()) {
                return false;
            }
            auto const alignment = align_session_history_with_disk(
                session_lines, disk_lines);
            if (!alignment ||
                !alignment->complete_entries[session_index]) {
                return false;
            }
            auto const records = make_persistent_history_records(
                disk_lines, *alignment);
            std::vector<bool> remove(disk_lines.size(), false);
            auto removed_entry = false;
            for (auto const& record : records) {
                if (record.owner && *record.owner == session_index) {
                    std::fill(
                        remove.begin() + record.start,
                        remove.begin() + record.end,
                        true);
                    removed_entry = true;
                }
            }
            if (!removed_entry) {
                return false;
            }
            std::vector<std::string> retained;
            retained.reserve(disk_lines.size());
            for (std::size_t index = 0; index < disk_lines.size(); ++index) {
                if (!remove[index]) {
                    retained.push_back(std::move(disk_lines[index]));
                }
            }
            disk_lines = std::move(retained);
            return true;
        });
}

struct readline_history_recall_state {
    enum class bell_style {
        audible,
        visible,
    };

    crepl_persistence_paths const* persistence = nullptr;
    rl_getc_func_t* underlying_getc = nullptr;
    rl_voidfunc_t* underlying_redisplay = nullptr;
    std::string line;
    std::size_t input_serial = 0;
    std::size_t observed_input_serial = 0;
    int latest_input_character = EOF;
    int history_position = 0;
    int point = 0;
    int end = 0;
    int mark = 0;
    bool mark_active = false;
    std::vector<std::string> canonical_history;
    std::optional<int> pristine_history_position;
    std::optional<bell_style> suppressed_bell_style;
    bool have_snapshot = false;
};

readline_history_recall_state readline_history_recall;

void capture_canonical_readline_history() {
    auto& canonical = readline_history_recall.canonical_history;
    canonical.clear();
    canonical.reserve(static_cast<std::size_t>(history_length));
    auto const* entries = history_list();
    for (int index = 0; index < history_length; ++index) {
        canonical.emplace_back(
            entries[index] != nullptr && entries[index]->line != nullptr
            ? entries[index]->line
            : "");
    }
}

void remember_canonical_readline_history(std::string_view source) {
    auto& canonical = readline_history_recall.canonical_history;
    canonical.emplace_back(source);
    if (canonical.size() > static_cast<std::size_t>(history_length)) {
        canonical.erase(
            canonical.begin(),
            canonical.begin() +
                (canonical.size() -
                 static_cast<std::size_t>(history_length)));
    }
}

[[nodiscard]] bool last_history_entry_matches(
    std::string_view source) noexcept {
    auto const& canonical =
        readline_history_recall.canonical_history;
    return !canonical.empty() && source == canonical.back();
}

[[nodiscard]] std::string current_readline_line() {
    return rl_line_buffer == nullptr
        ? std::string{}
        : std::string(
            rl_line_buffer, static_cast<std::size_t>(rl_end));
}

[[nodiscard]] bool readline_buffer_matches_history_position(
    int position) noexcept {
    auto const& canonical = readline_history_recall.canonical_history;
    if (position < 0 || position >= history_length ||
        static_cast<std::size_t>(position) >= canonical.size() ||
        rl_line_buffer == nullptr) {
        return false;
    }
    return std::string_view(
               rl_line_buffer, static_cast<std::size_t>(rl_end)) ==
        canonical[static_cast<std::size_t>(position)];
}

void snapshot_readline_editor() {
    auto& state = readline_history_recall;
    state.line = current_readline_line();
    state.history_position = where_history();
    state.point = rl_point;
    state.end = rl_end;
    state.mark = rl_mark;
    state.mark_active = rl_mark_active_p() != 0;
    state.have_snapshot = true;
}

void restore_readline_bell_style() noexcept {
    auto& state = readline_history_recall;
    if (!state.suppressed_bell_style) {
        return;
    }
    auto const* style =
        *state.suppressed_bell_style ==
                readline_history_recall_state::bell_style::visible
        ? "visible"
        : "audible";
    if (rl_variable_bind("bell-style", style) == 0) {
        state.suppressed_bell_style.reset();
    }
}

void suppress_pristine_history_delete_bell(int character) {
    auto& state = readline_history_recall;
    if (character != 4 ||
        RL_ISSTATE(RL_STATE_READCMD) == 0 ||
        RL_ISSTATE(RL_STATE_MOREINPUT) != 0 ||
        rl_key_sequence_length != 0 ||
        !state.have_snapshot ||
        !state.pristine_history_position) {
        return;
    }

    auto const position = where_history();
    auto const unchanged_editor =
        position == *state.pristine_history_position &&
        position == state.history_position &&
        current_readline_line() == state.line &&
        rl_point == state.point && rl_end == state.end &&
        rl_mark == state.mark &&
        (rl_mark_active_p() != 0) == state.mark_active &&
        readline_buffer_matches_history_position(position);
    if (!unchanged_editor) {
        return;
    }

    int binding_type = ISFUNC;
    constexpr char ctrl_d[] = {'\004'};
    auto* const binding = rl_function_of_keyseq_len(
        ctrl_d, 1, rl_get_keymap(), &binding_type);
    if (binding_type != ISFUNC || binding != rl_delete) {
        return;
    }

    // rl_delete rings at end-of-line before the post-dispatch observer can
    // replace the recalled entry.  Silence only that known native dispatch;
    // the configured binding and every other use of Readline's bell remain
    // untouched.
    auto const* style = rl_variable_value("bell-style");
    if (style == nullptr || std::strcmp(style, "none") == 0) {
        return;
    }
    auto const original_style =
        std::strcmp(style, "visible") == 0
        ? readline_history_recall_state::bell_style::visible
        : readline_history_recall_state::bell_style::audible;
    if (rl_variable_bind("bell-style", "none") == 0) {
        state.suppressed_bell_style = original_style;
    }
}

void remove_pristine_readline_history_entry(int position) {
    auto& state = readline_history_recall;
    auto const previous_history_length = history_length;
    auto const canonical_position = static_cast<std::size_t>(position);

    // Discard any temporary edit/undo chain Readline attached to this
    // canonical entry before it is detached from the history list.
    if (rl_undo_list != nullptr) {
        rl_revert_line(1, 0);
    }

    // Let Readline reveal its own next entry or saved live draft before
    // removing the selected entry from the underlying history list.
    rl_get_next_history(1, 4);
    auto const replacement_is_history =
        position + 1 < previous_history_length;

    auto* removed = remove_history(position);
    if (removed == nullptr) {
        rl_get_previous_history(1, 4);
        return;
    }
    auto* removed_undo = static_cast<UNDO_LIST*>(
        free_history_entry(removed));
    if (removed_undo != nullptr) {
        auto* active_undo = rl_undo_list;
        if (active_undo == removed_undo) {
            active_undo = nullptr;
        }
        rl_undo_list = removed_undo;
        rl_free_undo_list();
        rl_undo_list = active_undo;
    }

    if (state.persistence != nullptr) {
        remove_persistent_history_entry(
            *state.persistence,
            state.canonical_history,
            canonical_position);
    }
    state.canonical_history.erase(
        state.canonical_history.begin() + canonical_position);

    history_set_pos(
        replacement_is_history ? position : history_length);
    rl_point = rl_end;
    state.pristine_history_position =
        replacement_is_history &&
        readline_buffer_matches_history_position(position)
        ? std::optional{position}
        : std::nullopt;
}

void observe_readline_after_dispatch() {
    auto& state = readline_history_recall;
    auto const position = where_history();
    auto const line = current_readline_line();
    auto const mark_active = rl_mark_active_p() != 0;

    if (!state.have_snapshot) {
        state.pristine_history_position =
            readline_buffer_matches_history_position(position)
            ? std::optional{position}
            : std::nullopt;
        snapshot_readline_editor();
        state.observed_input_serial = state.input_serial;
        return;
    }

    if (position != state.history_position) {
        state.pristine_history_position =
            readline_buffer_matches_history_position(position)
            ? std::optional{position}
            : std::nullopt;
    } else if (state.pristine_history_position &&
               (position != *state.pristine_history_position ||
                line != state.line || rl_point != state.point ||
                rl_end != state.end || rl_mark != state.mark ||
                mark_active != state.mark_active)) {
        state.pristine_history_position.reset();
    }

    auto const new_input =
        state.input_serial != state.observed_input_serial;
    auto const native_forward_delete_ctrl_d =
        new_input && state.latest_input_character == 4 &&
        rl_last_func == rl_delete && rl_executing_key == 4 &&
        rl_key_sequence_length == 1 &&
        rl_executing_keyseq != nullptr &&
        static_cast<unsigned char>(rl_executing_keyseq[0]) == 4;
    if (native_forward_delete_ctrl_d &&
        state.pristine_history_position &&
        position == *state.pristine_history_position &&
        readline_buffer_matches_history_position(position)) {
        remove_pristine_readline_history_entry(position);
    }

    state.observed_input_serial = state.input_serial;
    snapshot_readline_editor();
}

int crepl_readline_getc(std::FILE* stream) {
    auto& state = readline_history_recall;
    restore_readline_bell_style();
    auto const character = state.underlying_getc(stream);
    state.latest_input_character = character;
    ++state.input_serial;
    suppress_pristine_history_delete_bell(character);
    return character;
}

void crepl_readline_redisplay() {
    if (rl_dispatching == 0) {
        restore_readline_bell_style();
        observe_readline_after_dispatch();
    }
    readline_history_recall.underlying_redisplay();
}

void prepare_readline_history_recall() {
    auto& state = readline_history_recall;
    restore_readline_bell_style();
    state.line.clear();
    state.observed_input_serial = state.input_serial;
    state.latest_input_character = EOF;
    state.history_position = where_history();
    state.point = 0;
    state.end = 0;
    state.mark = 0;
    state.mark_active = false;
    state.pristine_history_position.reset();
    state.have_snapshot = false;
}

void load_persistent_filenames(
    crepl_persistence_paths const& paths) noexcept {
    try {
        std::ifstream input(paths.settings);
        std::string header;
        std::string save_key;
        std::string save_filename;
        std::string load_key;
        std::string load_filename;
        if (!std::getline(input, header) || header != settings_header ||
            !(input >> save_key >> std::quoted(save_filename)) ||
            !(input >> load_key >> std::quoted(load_filename)) ||
            save_key != "save" || load_key != "load" ||
            save_filename.empty() || load_filename.empty()) {
            return;
        }
        input >> std::ws;
        if (!input.eof()) {
            return;
        }

        last_save_filename = std::move(save_filename);
        last_load_filename = std::move(load_filename);
    } catch (...) {
    }
}

void save_persistent_filenames(
    crepl_persistence_paths const& paths) noexcept {
    try {
        auto temporary = paths.settings;
#if defined(_WIN32)
        auto const process_id = _getpid();
#else
        auto const process_id = getpid();
#endif
        temporary += "." + std::to_string(process_id) + ".tmp";
        {
            std::ofstream output(
                temporary, std::ios::out | std::ios::trunc);
            output << settings_header << '\n'
                   << "save " << std::quoted(last_save_filename) << '\n'
                   << "load " << std::quoted(last_load_filename) << '\n';
            output.close();
            if (!output) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return;
            }
        }

        std::error_code ignored;
        std::filesystem::permissions(
            temporary,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace,
            ignored);

#if defined(_WIN32)
        auto const installed = MoveFileExW(
            temporary.c_str(), paths.settings.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, paths.settings, error);
        auto const installed = !error;
#endif
        if (!installed) {
            std::filesystem::remove(temporary, ignored);
        }
    } catch (...) {
    }
}

template<std::size_t Size>
[[nodiscard]] constexpr completion_candidates make_completion_candidates(
    std::array<std::string_view, Size> const& candidates) noexcept {
    return {candidates.data(), candidates.size()};
}

[[nodiscard]] completion_candidates command_completions_after(
    std::string_view prefix,
    std::string_view partial) noexcept {
    std::array<std::string_view, 5> words{};
    std::size_t word_count = 0;
    std::size_t position = 0;
    while (position < prefix.size()) {
        while (position < prefix.size() &&
               is_command_whitespace(prefix[position])) {
            ++position;
        }
        if (position == prefix.size()) {
            break;
        }
        if (word_count == words.size()) {
            return {};
        }

        auto const start = position;
        while (position < prefix.size() &&
               !is_command_whitespace(prefix[position])) {
            ++position;
        }
        words[word_count++] = prefix.substr(start, position - start);
    }

    if (word_count == 0) {
        return make_completion_candidates(command_completion_candidates);
    }
    if (word_count == 1) {
        if (words[0] == "basis" || words[0] == "key" ||
            words[0] == "single") {
            return make_completion_candidates(step_completion_candidates);
        }
        if (words[0] == "references") {
            return make_completion_candidates(
                definition_reference_completion_candidates);
        }
        if (words[0] == "step") {
            return make_completion_candidates(
                step_limit_completion_candidates);
        }
        if (words[0] == "colorize") {
            return make_completion_candidates(toggle_completion_candidates);
        }
        if (words[0] == "help") {
            return make_completion_candidates(help_completion_candidates);
        }
        if (words[0] == "abstract") {
            return partial.empty() || partial.starts_with('?')
                ? make_completion_candidates(
                    abstract_question_completion_candidates)
                : make_completion_candidates(
                    abstract_trace_completion_candidates);
        }
        if (words[0] == "compare") {
            return make_completion_candidates(
                abstract_question_completion_candidates);
        }
        if (words[0] == "define" || words[0] == "set") {
            return make_completion_candidates(
                definition_reference_completion_candidates);
        }
        if (words[0] == "depends") {
            return make_completion_candidates(
                depends_on_completion_candidates);
        }
        if (words[0] == "used") {
            return make_completion_candidates(
                used_by_completion_candidates);
        }
        if (words[0] == "dependson" ||
            words[0] == "depends-on") {
            return make_completion_candidates(
                dependency_all_completion_candidates);
        }
        if (words[0] == "usedby" || words[0] == "used-by") {
            return make_completion_candidates(
                uses_dependency_option_completion_candidates);
        }
        if (words[0] == "show") {
            return make_completion_candidates(show_completion_candidates);
        }
        if (words[0] == "find") {
            return make_completion_candidates(find_completion_candidates);
        }
        if (words[0] == "save") {
            save_filename_completion_candidates[0] =
                last_save_filename;
            return make_completion_candidates(
                save_filename_completion_candidates);
        }
        if (words[0] == "load") {
            load_filename_completion_candidates[0] =
                last_load_filename;
            return make_completion_candidates(
                load_filename_completion_candidates);
        }
        return {};
    }
    if (word_count == 2 && words[1] == "step" &&
        (words[0] == "basis" || words[0] == "key" ||
         words[0] == "single")) {
        return make_completion_candidates(toggle_completion_candidates);
    }
    if (word_count == 2 && words[0] == "step" &&
        words[1] == "limit") {
        return make_completion_candidates(
            step_limit_option_completion_candidates);
    }
    if (word_count == 2 && words[0] == "find" &&
        words[1] == "all") {
        return make_completion_candidates(
            find_after_all_completion_candidates);
    }
    if (word_count == 2 && words[0] == "depends" &&
        words[1] == "on") {
        return make_completion_candidates(
            dependency_all_completion_candidates);
    }
    if (word_count == 2 && words[0] == "used" &&
        words[1] == "by") {
        return make_completion_candidates(
            uses_dependency_option_completion_candidates);
    }
    auto const path_argument_position = [&]() -> std::optional<std::size_t> {
        if (word_count >= 2 &&
            (words[0] == "usedby" || words[0] == "used-by") &&
            words[1] == "path") {
            return 2;
        }
        if (word_count >= 3 && words[0] == "used" &&
            words[1] == "by" && words[2] == "path") {
            return 3;
        }
        return std::nullopt;
    }();
    if (path_argument_position) {
        auto const argument = *path_argument_position;
        if (word_count == argument) {
            return make_completion_candidates(
                dependency_path_between_completion_candidates);
        }
        if (word_count == argument + 1 &&
            words[argument] != "between") {
            return make_completion_candidates(
                dependency_path_and_completion_candidates);
        }
        if (word_count == argument + 2 &&
            words[argument] == "between") {
            return make_completion_candidates(
                dependency_path_and_completion_candidates);
        }
    }
    if (word_count == 2 && words[0] == "abstract" &&
        (words[1] == "steps" || words[1] == "ministeps")) {
        return make_completion_candidates(
            abstract_question_completion_candidates);
    }
    return {};
}

completion_candidates active_completion_candidates;

[[nodiscard]] char* duplicate_completion(
    std::string_view completion) noexcept {
    auto* result = static_cast<char*>(
        std::malloc(completion.size() + 1));
    if (result == nullptr) {
        return nullptr;
    }
    std::memcpy(result, completion.data(), completion.size());
    result[completion.size()] = '\0';
    return result;
}

[[nodiscard]] char* crepl_completion_generator(
    char const* text,
    int state) noexcept {
    static std::size_t candidate_index = 0;
    if (state == 0) {
        candidate_index = 0;
    }

    auto const partial = std::string_view(text == nullptr ? "" : text);
    while (candidate_index < active_completion_candidates.size) {
        auto const candidate =
            active_completion_candidates.data[candidate_index++];
        if (candidate.starts_with(partial)) {
            return duplicate_completion(candidate);
        }
    }
    return nullptr;
}

[[nodiscard]] char** crepl_attempted_completion(
    char const* text,
    int start,
    int) noexcept {
    rl_attempted_completion_over = 1;
    rl_completion_suppress_append = 0;
    if (rl_line_buffer == nullptr || start < 0) {
        return nullptr;
    }

    auto const partial = std::string_view(
        text == nullptr ? "" : text);
    active_completion_candidates = command_completions_after(
        std::string_view(
            rl_line_buffer, static_cast<std::size_t>(start)),
        partial);
    if (active_completion_candidates.size == 0) {
        return nullptr;
    }
    rl_completion_suppress_append =
        active_completion_candidates.size == 1 &&
        active_completion_candidates.data[0] == "?";
    return rl_completion_matches(text, crepl_completion_generator);
}

[[nodiscard]] bool is_exact_command(
    std::string_view source,
    std::string_view keyword) noexcept {
    std::size_t position = 0;
    while (position < source.size() &&
           is_command_whitespace(source[position])) {
        ++position;
    }

    if (!source.substr(position).starts_with(keyword)) {
        return false;
    }
    position += keyword.size();

    while (position < source.size() &&
           is_command_whitespace(source[position])) {
        ++position;
    }
    return position == source.size();
}

[[nodiscard]] bool begins_command(
    std::string_view source,
    std::string_view keyword) noexcept {
    std::size_t position = 0;
    while (position < source.size() &&
           is_command_whitespace(source[position])) {
        ++position;
    }

    if (!source.substr(position).starts_with(keyword)) {
        return false;
    }
    position += keyword.size();
    return position == source.size() ||
           is_command_whitespace(source[position]);
}

[[nodiscard]] bool is_show_all_command(
    std::string_view source) noexcept {
    std::size_t position = 0;
    auto skip_whitespace = [&] {
        while (position < source.size() &&
               is_command_whitespace(source[position])) {
            ++position;
        }
    };
    auto next_word = [&]() {
        skip_whitespace();
        auto const start = position;
        while (position < source.size() &&
               !is_command_whitespace(source[position])) {
            ++position;
        }
        return source.substr(start, position - start);
    };

    if (next_word() != "show" || next_word() != "all") {
        return false;
    }
    skip_whitespace();
    return position == source.size();
}

[[nodiscard]] std::optional<mode_command>
parse_mode_command(std::string_view source) {
    std::size_t position = 0;
    auto skip_whitespace = [&] {
        while (position < source.size() &&
               is_command_whitespace(source[position])) {
            ++position;
        }
    };
    auto next_word = [&]() {
        skip_whitespace();
        auto const start = position;
        while (position < source.size() &&
               !is_command_whitespace(source[position])) {
            ++position;
        }
        return std::pair{
            source.substr(start, position - start), start};
    };

    auto const first = next_word().first;
    auto const requested_kind =
        first == "single"
            ? std::optional{mode_command_kind::single}
            : first == "key"
                ? std::optional{mode_command_kind::key}
                : first == "basis"
                    ? std::optional{mode_command_kind::basis}
                    : first == "colorize"
                        ? std::optional{mode_command_kind::colorize}
                        : std::nullopt;
    if (!requested_kind) {
        return std::nullopt;
    }

    if (*requested_kind != mode_command_kind::colorize) {
        auto const second = next_word().first;
        if (second != "step") {
            return std::nullopt;
        }
    }

    skip_whitespace();
    if (position == source.size()) {
        return mode_command{*requested_kind, true};
    }

    auto const [option, option_position] = next_word();
    skip_whitespace();
    if (position != source.size()) {
        throw combdsl::parse_error(
            position, "unexpected input after stepping option");
    }
    if (option == "on") {
        return mode_command{*requested_kind, true};
    }
    if (option == "off") {
        return mode_command{*requested_kind, false};
    }
    throw combdsl::parse_error(
        option_position, "expected 'on' or 'off'");
}

[[nodiscard]] std::optional<help_detail>
parse_help_command(std::string_view source) {
    std::size_t position = 0;
    auto skip_whitespace = [&] {
        while (position < source.size() &&
               is_command_whitespace(source[position])) {
            ++position;
        }
    };
    auto next_word = [&]() {
        skip_whitespace();
        auto const start = position;
        while (position < source.size() &&
               !is_command_whitespace(source[position])) {
            ++position;
        }
        return std::pair{
            source.substr(start, position - start), start};
    };

    if (next_word().first != "help") {
        return std::nullopt;
    }

    skip_whitespace();
    if (position == source.size()) {
        return help_detail::brief;
    }

    auto const [option, option_position] = next_word();
    skip_whitespace();
    if (position != source.size()) {
        throw combdsl::parse_error(
            position, "unexpected input after help option");
    }
    if (option == "brief") {
        return help_detail::brief;
    }
    if (option == "full") {
        return help_detail::full;
    }
    throw combdsl::parse_error(
        option_position, "expected 'brief' or 'full'");
}

[[nodiscard]] std::optional<std::string>
parse_filename_command(
    std::string_view source,
    std::string_view command) {
    std::size_t position = 0;
    while (position < source.size() &&
           is_command_whitespace(source[position])) {
        ++position;
    }

    auto const command_start = position;
    while (position < source.size() &&
           !is_command_whitespace(source[position])) {
        ++position;
    }
    if (source.substr(command_start, position - command_start) !=
        command) {
        return std::nullopt;
    }

    while (position < source.size() &&
           is_command_whitespace(source[position])) {
        ++position;
    }
    auto const filename_start = position;
    auto filename_end = source.size();
    while (filename_end > filename_start &&
           is_command_whitespace(source[filename_end - 1])) {
        --filename_end;
    }
    if (filename_start == filename_end) {
        throw combdsl::parse_error(
            filename_start, "missing filename");
    }
    return std::string(
        source.substr(filename_start, filename_end - filename_start));
}

void write_wrapped_paragraph(
    std::ostream& output,
    std::string_view paragraph,
    std::size_t maximum_line_length = 80) {
    std::size_t position = 0;
    std::size_t line_length = 0;

    while (position < paragraph.size()) {
        while (position < paragraph.size() &&
               is_command_whitespace(paragraph[position])) {
            ++position;
        }
        if (position == paragraph.size()) {
            break;
        }

        auto const word_start = position;
        while (position < paragraph.size() &&
               !is_command_whitespace(paragraph[position])) {
            ++position;
        }
        auto const word =
            paragraph.substr(word_start, position - word_start);

        if (line_length != 0 &&
            line_length + 1 + word.size() > maximum_line_length) {
            output.put('\n');
            line_length = 0;
        }
        if (line_length != 0) {
            output.put(' ');
            ++line_length;
        }
        output.write(
            word.data(),
            static_cast<std::streamsize>(word.size()));
        line_length += word.size();
    }
    output.put('\n');
}

void print_help_brief(std::ostream& output) {
    output <<
        "Commands (items in brackets [] are optional):\n"
        "about                                 | display copyright and redistribution information\n"
        "abstract [steps | ministeps]          | compute combinators such that applying <xyz...> to them\n"
        "    ?<xyz...> = <expression>          |     reduces to <expression>\n"
        "basis step [on | off]                 | names are converted to their stored expressions as a step\n"
        "birds                                 | display the pre-defined bird combinators\n"
        "colorize [on | off]                   | add colors to arguments while stepping\n"
        "compare ?<xyz...> <left> = <right>    | compare applied normal forms within time limits\n"
        "define [captured | live] <name>       | compute and store combinators such that\n"
        "    [<xyz...>] = <expression>         |     <name> [<xyz...>] reduces to <expression>\n"
        "dependson  [all] <name>               | display direct and optional indirect definitions that\n"
        "depends-on [all] <name>               |     depend on <name>\n"
        "depends on [all] <name>               |\n"
        "exit                                  | end the program\n"
        "find [all] [<num> | among <birds...>] | search for shortest combinator strings such that applying\n"
        "    ?<xyz...> = <expression>          |     <xyz...> to them reduces to <expression>\n"
        "help [brief | full]                   | display help information\n"
        "inspect <expression>                  | display info about <expression> without evaluating it\n"
        "key step [on | off]                   | after each step, wait for a keypress to continue\n"
        "load <filename>                       | load a set-list journal from a file\n"
        "quit                                  | end the program\n"
        "references <captured | live>          | by default, capture name references at define time\n"
        "                                      |     or follow later changes to them\n"
        "remove <name>                         | remove a user-defined combinator name\n"
        "revisions <name>                      | display every retained revision of <name>\n"
        "save <filename>                       | save the set-list journal to a file\n"
        "set [captured | live] <name>          | store <expression> as <name> with arity <arity> or 0\n"
        "    = [arity] <expression>            |\n"
        "show <name[@<num>] | all>             | display one revision of <name> or the entire set list\n"
        "single step [on | off]                | display each step of the reduction without pause\n"
        "step limit <off | num>                | limit ordinary and Single Step evaluations\n"
        "usedby  [all] <name>                  | display direct and optional indirect definitions that\n"
        "used-by [all] <name>                  |     <name> uses\n"
        "used by [all] <name>                  |\n"
        "usedby  path [between] <name1>        | display the dependency path between <name1> and <name2>\n"
        "    [and] <name2>                     |\n"
        "used-by path [between] <name1>        |\n"
        "    [and] <name2>                     |\n"
        "used by path [between] <name1>        |\n"
        "    [and] <name2>                     |\n";
    output.flush();
}

void print_help_topic(
    std::ostream& output,
    std::string_view title,
    std::string_view text) {
    output << title << "\n";
    write_wrapped_paragraph(output, text);
    output.put('\n');
}

void print_help_full(std::ostream& output) {
    // Keep this text in sync with web/index.html's Help dialog.
    output << "Stepping Options\n\n";
    print_help_topic(
        output,
        "single step [on | off]",
        "Runs automatically and displays every reduction step until "
        "normal form is reached.");
    print_help_topic(
        output,
        "key step [on | off]",
        "Pauses after initial expression scanning. Press any ordinary "
        "key for exactly one reduction step. Pressing the letter Q or q "
        "exits the reduction immediately.");
    print_help_topic(
        output,
        "already in normal form",
        "When a submitted expression has no available reduction, ordinary "
        "evaluation, Single Step, and Key Step print \"No further "
        "reductions\".");
    print_help_topic(
        output,
        "basis step [on | off]",
        "Changes how \"single step\" and \"key step\" handle named bases "
        "defined by compound combinators. All the bird combinators (see the "
        "\"birds\" command) except S, K, I, and Y are named bases. Basis step "
        "\"off\" contracts a saturated named basis directly (for example, Mx "
        "becomes xx). Basis step \"on\" exposes its underlying definition as "
        "a separate reduction step (so Mx becomes SIIx). It may remain "
        "on when neither stepping mode is active; ordinary evaluation "
        "ignores it.");
    print_help_topic(
        output,
        "step limit <off | num>",
        "Pauses each subsequent ordinary or Single Step interactive "
        "evaluation after num reduction steps at a time. Key Step ignores "
        "the limit. With redirected input, evaluation stops at the limit. "
        "The limit is off at startup, and an explicit off or num is required. "
        "In an interactive session, reaching the limit "
        "pauses the evaluation. Press Enter to reset the count and continue "
        "the same evaluation, or press q or Q to cancel it. The number must "
        "be greater than zero. "
        "The setting is not written to saved set-list journals.");
    print_help_topic(
        output,
        "colorize [on | off]",
        "When \"single step\" or \"key step\" is active, it highlights the first, "
        "second, third, fourth, and fifth arguments of each reduction in "
        "red, tunic green, blue, dark orange, and Munsell purple, and "
        "carries those highlights into the reduced result. When \"basis "
        "step\" is on, a basis expansion instead highlights only the basis "
        "name before the step and its stored contents after the step, both "
        "in red; all arguments remain uncolored. After the final reduction "
        "step, the normal form is displayed without color at the left "
        "margin. Colorize may remain on when neither stepping mode is "
        "active; ordinary evaluation ignores it.");

    output << "Adding Combinators\n\n"
           << "set [captured | live] <name> = "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "In the first form, the expression always reduces immediately.");
    output << '\n'
           << "set [captured | live] <name> = <arity> "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "In the second form, arity is a number specifying the minimal "
        "number of arguments required for the expression to reduce.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "The parser accepts nonnegative decimal integers, such as 0 and 42. "
        "They are stored and displayed as integer values. A leading '+' or "
        "'-' sign and floating-point forms containing a decimal point or an "
        "e/E exponent are parse errors. Canonical output separates integers "
        "from adjacent non-parenthesized operands. In a set command, a "
        "nonnegative decimal integer after '=' is the arity only when "
        "whitespace and a following expression are present: 'set Num = 2 "
        "I' has arity 2, while 'set Num = 2' and 'set Num = 2I' begin "
        "integer bodies. Use '(2) I' to force a leading integer body before "
        "whitespace.");
    output << '\n'
           << "define [captured | live] <name> [<symbol_list>] = "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "The \"define\" form accepts zero or more symbols (lower case "
        "letters) after the name, infers the arity from their count, and "
        "computes a series of combinators that will reproduce the "
        "combinator_expression. With no symbols, it stores an arity-zero "
        "basis directly. An all-lowercase token immediately before '=' is "
        "assumed to be the basis name, as in \"define foo=x\". For a "
        "one-character name that does not begin with a lowercase ASCII "
        "letter, the space before symbols may be omitted. For example, if "
        "the Eagle bird wasn't already defined, it could be added by:");
    output << "define Exyzwv = xy(zwv)\n\n";
    write_wrapped_paragraph(
        output,
        "A basis name cannot begin with \\, \", ), (, or ?, and can't end with @");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "An equals sign may begin a basis name. For such a name, whitespace "
        "must separate the name from the assignment =; every later = before "
        "that whitespace remains part of the name. Thus 'set = = 3 C' "
        "defines the name '=', and 'set =bar = 3 C' defines the name "
        "'=bar', while 'set = 3 C' is a parse error because it has no second "
        "'='. In 'define = bar = rab', the name is '=' and 'bar' is the "
        "symbol list.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "The optional \"captured\" or \"live\" modifier on set and "
        "define overrides the reference mode for that command only, without "
        "changing the mode for later input. Captured references pin current "
        "revisions; live references follow later redefinitions. An explicit "
        "modifier is retained in the saved set list.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "User-defined combinator names created by \"set\" or \"define\" "
        "may be redefined. Every changed definition creates the next "
        "immutable name@N revision; equivalent repetitions do not. Revision "
        "numbers continue after removal and re-addition. The saved set list "
        "keeps every changed definition and removal in chronological order "
        "so all revisions remain replayable. Pre-defined bird combinator "
        "names are immutable and cannot be redefined. In formatted expressions "
        "and command results, the @N suffix is displayed only after a name "
        "has more than one retained revision. Until then, its sole first "
        "revision prints as bare name, including after removal; a removed "
        "singleton remains resolvable in "
        "ordinary expressions and restricted Find catalogs. Once revision 2 "
        "exists, stored captures, explicit revisions, inspectors, revision "
        "listings, and dependency paths expose exact revisions as name@N. A "
        "directly entered unqualified current name and a live reference remain "
        "unversioned. The replayable "
        "journal is source history, so it may preserve an explicitly entered "
        "name@1; show all and save expose that same journal.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "A redefinition is rejected when resolving its live references "
        "would make the resulting definition graph circular. A captured or "
        "version-qualified reference cannot create a cycle by itself, "
        "although a captured revision can contain live references. Recursive "
        "\"define\" remains allowed.");
    output << '\n'
           << "references <captured | live>\n";
    write_wrapped_paragraph(
        output,
        "Controls how unqualified user-defined names in subsequently parsed "
        "input are bound. A captured/live modifier on set or define overrides "
        "the mode for that command. References are captured initially. In "
        "captured mode, an unqualified current user name freezes its current "
        "immutable revision. Outside the replayable source journal, the sole "
        "first revision always prints as bare name, even when stored, "
        "inspected, explicitly entered as name@1, or "
        "retained after removal. Once the name has been redefined, captured "
        "references stored or inspected, explicit revisions, and stale plain "
        "expressions print their exact name@N revisions. An unqualified current "
        "name entered directly in a plain expression retains its bare spelling. "
        "In live mode, each unqualified reference prints as name and follows later "
        "redefinitions. Each changed set or define "
        "creates the next revision. Before the first saved "
        "set, define, or remove, only the last explicit \"references\" command is "
        "saved. If there is none, the saved set list begins with \"references "
        "captured\". Later \"references\" commands remain in chronological order.");
    output << '\n'
           << "show <name[@<num>] | all>\n";
    write_wrapped_paragraph(
        output,
        "The show command displays the arity and combinators stored for the "
        "current name, while \"show name@<num>\" displays that exact immutable "
        "revision. For example, \"show E\" for the Eagle bird would display "
        "\"arity:5 BDD\". The \"show all\" form displays the entire saved set "
        "list, or \"Nothing to show\" when it is empty. Because that list is "
        "replayable source history, it may preserve an explicitly entered "
        "name@1 token.");
    output << '\n'
           << "revisions <name>\n";
    write_wrapped_paragraph(
        output,
        "Displays every retained immutable revision of the unversioned name "
        "in ascending version order, including revisions retained after the "
        "current name is removed. A never-redefined user name begins its sole "
        "line with \"Name arity:A body\"; after a changed redefinition, every "
        "line for that name begins with \"Name@N arity:A body\". Each line "
        "ends with either \"[captured]\" or \"[live]\" to identify the "
        "effective reference mode used when that "
        "revision was parsed; the current revision also ends with "
        "\"[current]\". The "
        "latest revision of a removed name ends with \"[removed]\" instead "
        "of \"[current]\". A pre-defined name has one unversioned revision "
        "ending with \"[pre-defined] [current]\". The fundamental names S, "
        "K, I, and Y are unversioned and cannot be queried.");
    output << '\n'
           << "remove <name>\n";
    write_wrapped_paragraph(
        output,
        "Removes a user-defined combinator name without discarding its "
        "immutable revisions. A never-redefined removed singleton remains "
        "accepted by bare name in ordinary expressions and restricted Find "
        "catalogs, although show and remove still treat the current name as "
        "absent. Revisions of a redefined name use explicit name@N. Frozen "
        "references remain fixed. Live "
        "references retain the most recent target while the name is absent "
        "and follow the new current revision if the name is added again. "
        "The removal remains in the chronological saved set list. "
        "Pre-defined names cannot be removed.");

    output << "\nInspecting Expressions\n\n"
           << "inspect <expression>\n";
    write_wrapped_paragraph(
        output,
        "Describes the parsed expression without evaluating it or expanding "
        "named bases. The report displays its canonical spelling only when it "
        "differs from the submitted expression, then its sorted free symbols "
        "(or none) and direct named references in first-use order. Fundamental, "
        "pre-defined, captured, and live references are labeled. A captured "
        "reference is unversioned while its name has only one retained "
        "revision; after redefinition, it includes its exact immutable name@N. "
        "The final line identifies the minimal head prefix consumed by the "
        "next reduction, its head, and the evaluator-selected "
        "function/argument location, or reports normal form when no reduction "
        "is available. Trailing arguments not consumed by that reduction are "
        "omitted. The report contains no tree-size or depth statistics.");

    output << "\nComparing Expressions\n\n"
           << "compare ?<symbol_list> <left_expression> = <right_expression>\n";
    write_wrapped_paragraph(
        output,
        "Applies the listed lowercase symbols to both expressions, then "
        "normalizes each resulting expression with its own 0.5-second time "
        "limit. A question mark must immediately precede one or more symbols. "
        "If both sides finish with the same normal form, the command prints "
        "\"both reduce to: <normal-form>\". If both finish with different "
        "normal forms, it prints \"left reduces to: <left-normal-form>\" and "
        "then \"right reduces to: <right-normal-form>\" on the next line. If "
        "either side does not reach normal form within its own time limit, it "
        "prints \"inconclusive\". Compare ignores the stepping, colorize, and "
        "configured step-limit modes.");

    output << "\nInspecting Dependencies\n\n"
           << "dependson [all] <name>\n"
           << "depends-on [all] <name>\n"
           << "depends on [all] <name>\n";
    write_wrapped_paragraph(
        output,
        "The three forms are equivalent. Without all, they display the named "
        "bases whose definitions directly contain name. The direct line is "
        "printed as \"A is directly depended on by: B C\", or \"A is not "
        "directly depended on by anything\" when empty. With all, the search "
        "also follows dependencies transitively and appends \"A is indirectly "
        "depended on by: D E\" only when the indirect list is nonempty.");
    output << '\n'
           << "usedby [all] <name>\n"
           << "used-by [all] <name>\n"
           << "used by [all] <name>\n";
    write_wrapped_paragraph(
        output,
        "The three forms are equivalent. Without all, they display the named "
        "bases directly contained in name's definition. The direct line is "
        "printed as \"A directly uses: B C\", or \"A directly uses nothing\" "
        "when empty. With all, the search also follows dependencies "
        "transitively and appends \"A indirectly uses: D E\" only when the "
        "indirect list is nonempty.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "Both searches include pre-defined and current user-defined named "
        "bases, but exclude the fundamental names S, K, I, and Y.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "With all, captured and version-qualified references retain their "
        "exact revisions, while live references follow the current target "
        "or their retained last target while the name is removed.");
    output << '\n'
           << "usedby path [between] <name1> [and] <name2>\n"
           << "used-by path [between] <name1> [and] <name2>\n"
           << "used by path [between] <name1> [and] <name2>\n";
    write_wrapped_paragraph(
        output,
        "The three forms are equivalent. The words \"between\" and \"and\" "
        "are independently optional. The two names must be different, "
        "current, non-fundamental names. The command searches the actual "
        "stored-reference graph in "
        "both directions. It chooses the path with the fewest edges, breaking "
        "ties by the lexicographically smallest stored basis-name sequence "
        "(the displayed order for ordinary names), so the argument order "
        "does not affect the result. A found path begins with a heading and "
        "prints one indented line per edge. A user-defined node is unversioned "
        "while its name has only one retained revision; after redefinition, "
        "the node uses its exact name@N revision identity, as in:");
    output << "A uses B via:\n"
           << "  A@2 -> C@4  [live]\n\n";
    write_wrapped_paragraph(
        output,
        "Each edge is labeled "
        "\"[live]\", \"[captured]\", or \"[pre-defined]\". Pre-defined "
        "nodes remain unversioned. An explicitly "
        "written name@N reference is labeled \"[captured]\", just like an "
        "implicitly captured reference. A target whose name is no longer "
        "registered also receives \"[name removed]\". Live references follow "
        "their current or retained last targets. Otherwise the command prints "
        "\"A and B have no dependency path\", with those endpoint names in "
        "lexicographic order.");

    output << "\nAbstracting Expressions\n\n"
           << "abstract [steps | ministeps] ?<symbol_list> = "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "Abstracts the listed lowercase symbols from the expression, from "
        "right to left, without evaluating it. A question mark must "
        "immediately precede the symbols. The plain form displays only the "
        "result as \"?=<expression>\". The optional \"steps\" word also "
        "displays changed preprocessing, each takeout as \"takeout <symbol> "
        "from <before>: <after>\", and each optimizer substitution, ending "
        "with the same \"?=\" result line. For example, \"abstract steps "
        "?xy = y\" displays \"takeout y from y: I\", then \"takeout x from "
        "I: KI\". The \"ministeps\" form additionally displays every "
        "recursive takeout call in its position in the full expression as "
        "\"[takeout <symbol> from <sub-expression>]\", followed by the "
        "resolved full expression on a new line beginning \"= \". For "
        "example, \"abstract ministeps ?xy = y(xy)\" starts with \"takeout "
        "y from y(xy): O[takeout y from xy]\", then \"= Ox\", \"takeout x "
        "from Ox: O\", and \"?=O\". Abstract ignores the stepping and "
        "colorize modes.");

    output << "\nFinding Combinators\n\n"
           << "find [all] [<num> | among <bird>...] ?<symbol_list> = "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "Searches the pre-defined bird catalog for forms that reduce to "
        "combinator_expression when applied to symbol_list. A question mark "
        "must immediately precede one or more lowercase symbols. The optional "
        "num is a number from 1 through 4 and defaults to 3. Without \"all\", "
        "sizes are searched in order and the command stops after the first "
        "size with answers. With \"all\", every size through num is searched "
        "and every answer is displayed. For example, \"find 3\" stops after "
        "the one-bird answers for ?xy = x(yx), while \"find all 3\" also "
        "reports matching two- and three-bird forms. A four-bird search may "
        "take minutes. Each answer is printed as \"?=<match>\". Y is "
        "excluded from the search catalog. Find ignores the stepping and "
        "colorize modes. Matching uses bounded normalization, so the red "
        "response \"No match within search bounds\" does not prove that no "
        "equivalent expression exists.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "Instead of num, \"among\" selects a restricted catalog. It must "
        "be followed by one or more bird names before the question mark. "
        "Whitespace between names is optional, but whitespace before the "
        "question mark remains required. Within each contiguous group, an "
        "exact whole name or revision wins; otherwise the longest valid bird "
        "name or revision is taken from left to right. "
        "Whitespace can force a shorter boundary. S, K, I, Y, pre-defined "
        "birds, current user-defined names, and retained user revisions are "
        "accepted. Revisions of a redefined name use name@N; a removed "
        "singleton that was never redefined uses its bare name. Unlike the "
        "default catalog, an explicitly listed "
        "Y is allowed. A restricted search examines increasing composition "
        "sizes within one 10-second window. Native CREPL keeps sizes one and "
        "two serial, then reuses one grow-only worker pool for sizes three "
        "and above. Each worker owns a deterministic modulo shard; up to "
        "eight shards are activated, bounded by the machine's reported "
        "hardware threads and the candidate count, and indexed answers are "
        "merged in sequential-search order. Only fully completed sizes are "
        "reported, "
        "and results from a size still in progress when time expires are "
        "discarded. Without \"all\", it stops at the first completed size "
        "with answers. With \"all\", it retains answers from every size "
        "completed within the same window. The output is otherwise unchanged, "
        "including the bounded no-match response.");

    output << "\nCommand Entry\n\n";
    print_help_topic(
        output,
        "Tab completion",
        "Press Tab while entering a command to complete command words and "
        "supported options. Save and load each remember their own most "
        "recently successful filename across interactive sessions, initially "
        "\"set_list.cmb\"; Tab at either filename position restores it. "
        "Existing whitespace between words is preserved.");

    output << "Other Commands\n\n"
           << "load <filename>\n";
    write_wrapped_paragraph(
        output,
        "Loads a saved set-list journal from filename without evaluating "
        "its definitions. The filename is the rest of the command after "
        "surrounding whitespace is removed, so it may contain spaces. "
        "User-defined combinators are replaced silently. Parsing continues "
        "after an error, up to 15 errors, but if any error is found, the "
        "entire load is rolled back.");
    output << '\n'
           << "save <filename>\n";
    write_wrapped_paragraph(
        output,
        "Saves the replayable set-list journal to filename, replacing any "
        "existing file. This is replayable source history, so an explicitly "
        "entered name@1 may remain visible even though formatted expressions "
        "display that singleton as bare name. The filename is the rest of the "
        "command after "
        "surrounding whitespace is removed, so it may contain spaces. If "
        "the journal is empty, displays \"Nothing to save\" and leaves any "
        "existing file untouched.");
    output << '\n'
           << "birds\n";
    write_wrapped_paragraph(
        output,
        "Displays the list of pre-defined bird combinators, defined in the "
        "system by their capitalized first letter, and what they reduce to.");
    output << '\n'
           << "about\n";
    write_wrapped_paragraph(
        output,
        "Displays copyright and redistribution information.");
    output << '\n'
           << "help [brief | full]\n";
    write_wrapped_paragraph(
        output,
        "Displays help information.  The \"brief\" form gives a list of "
        "the commands with short explanations.");
    output << '\n'
           << "exit\n"
           << "quit\n";
    write_wrapped_paragraph(
        output,
        "Ends the program normally.");

    output.flush();
}

void print_about(std::ostream& output) {
    // Keep the shared license and source text in sync with
    // web/index.html's About dialog.
    constexpr std::string_view free_software =
        "This program is free software: you can redistribute it and/or "
        "modify it under the terms of the GNU Affero General Public "
        "License as published by the Free Software Foundation, either "
        "version 3 of the License, or (at your option) any later version.";
    constexpr std::string_view warranty =
        "This program is distributed in the hope that it will be useful, "
        "but WITHOUT ANY WARRANTY; without even the implied warranty of "
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
        "Affero General Public License for more details.";
    constexpr std::string_view links =
        "A copy of the GNU Affero General Public License, Version 3, is "
        "available at "
        "<https://www.gnu.org/licenses/agpl-3.0.html#license-text>. "
        "The source for this program is available at "
        "<https://github.com/dwgero/combDSL1>.";

    print_crepl_banner(output);
    output << '\n'
           << "Part of the C++ Combinator DSL\n"
           << "Copyright (C) 2026 David W. Gero\n\n";
    write_wrapped_paragraph(output, free_software);
    output.put('\n');
    write_wrapped_paragraph(output, warranty);
    output.put('\n');
    write_wrapped_paragraph(output, links);
    output.flush();
}

void print_birds(std::ostream& output) {
    // Keep these entries in sync with web/index.html's Bird Info table.
    struct bird_entry {
        std::string_view name;
        std::string_view reduction;
    };
    constexpr std::array bird_entries{
        bird_entry{"Albatross", "Axy = x(yx)"},
        bird_entry{"Bluebird", "Bxyz = x(yz)"},
        bird_entry{"Cardinal", "Cxyz = xzy"},
        bird_entry{"Cardinal star", "C*xyzw = xywz"},
        bird_entry{"Cardinal star star", "C**xyzwv = xyzvw"},
        bird_entry{"Dove", "Dxyzw = xy(zw)"},
        bird_entry{"Eagle", "Exyzwv = xy(zwv)"},
        bird_entry{"Finch", "Fxyz = zyx"},
        bird_entry{"Goldfinch", "Gxyzw = xw(yz)"},
        bird_entry{"Hummingbird", "Hxyz = xyzy"},
        bird_entry{"Identity bird", "Ix = x"},
        bird_entry{"Jay", "Jxyzw = xy(xwz)"},
        bird_entry{"Kestrel", "Kxy = x"},
        bird_entry{"Lark", "Lxy = x(yy)"},
        bird_entry{"Mockingbird", "Mx = xx"},
        bird_entry{"Nightingale", "Nxy = xyx"},
        bird_entry{"Owl", "Oxy = y(xy)"},
        bird_entry{"Queer bird", "Qxyz = y(xz)"},
        bird_entry{"Quixotic bird", "Q1xyz = x(zy)"},
        bird_entry{"Quirky bird", "Q3xyz = z(xy)"},
        bird_entry{"Robin", "Rxyz = yzx"},
        bird_entry{"Sage bird", "Yx = x(Yx)"},
        bird_entry{"Starling", "Sxyz = xz(yz)"},
        bird_entry{"Thrush", "Txy = yx"},
        bird_entry{"Turing bird", "Uxy = y(xxy)"},
        bird_entry{"Vireo", "Vxyz = zxy"},
        bird_entry{"Warbler", "Wxy = xyy"},
        bird_entry{"Warbler star", "W*xyz = xyzz"},
        bird_entry{"Warbler star star", "W**xyzw = xyzww"},
        bird_entry{"Converse warbler", "W1xy = yxx"},
        bird_entry{"Zazu", "Zxy = x(xy)"}};
    constexpr std::size_t maximum_line_length = 80;
    constexpr std::size_t column_gap = 3;

    struct column_layout {
        std::size_t column_count;
        std::size_t row_count;
        std::array<std::size_t, 3> name_widths{};
        std::array<std::size_t, 3> reduction_widths{};
        std::array<std::size_t, 3> widths{};
        std::size_t line_length = 0;
    };

    auto const make_layout =
        [&](std::size_t column_count) {
            column_layout result{
                column_count,
                (bird_entries.size() + column_count - 1) /
                    column_count};
            for (std::size_t column = 0;
                 column < column_count;
                 ++column) {
                for (std::size_t row = 0;
                     row < result.row_count;
                     ++row) {
                    auto const index =
                        row + column * result.row_count;
                    if (index >= bird_entries.size()) {
                        continue;
                    }
                    if (bird_entries[index].name.size() >
                        result.name_widths[column]) {
                        result.name_widths[column] =
                            bird_entries[index].name.size();
                    }
                    if (bird_entries[index].reduction.size() >
                        result.reduction_widths[column]) {
                        result.reduction_widths[column] =
                            bird_entries[index].reduction.size();
                    }
                }
                result.widths[column] =
                    result.name_widths[column] + 1 +
                    result.reduction_widths[column];
                result.line_length += result.widths[column];
            }
            result.line_length +=
                column_gap * (column_count - 1);
            return result;
        };

    auto layout = make_layout(3);
    if (layout.line_length > maximum_line_length) {
        layout = make_layout(2);
    }

    for (std::size_t row = 0; row < layout.row_count; ++row) {
        for (std::size_t column = 0;
             column < layout.column_count;
             ++column) {
            auto const index = row + column * layout.row_count;
            if (index >= bird_entries.size()) {
                break;
            }

            auto const& entry = bird_entries[index];
            output << entry.name;
            auto const name_padding =
                layout.name_widths[column] -
                entry.name.size() + 1;
            for (std::size_t space = 0;
                 space < name_padding;
                 ++space) {
                output.put(' ');
            }
            output << entry.reduction;
            auto const next_index =
                row + (column + 1) * layout.row_count;
            if (column + 1 < layout.column_count &&
                next_index < bird_entries.size()) {
                auto const padding =
                    layout.reduction_widths[column] -
                    entry.reduction.size() +
                    column_gap;
                for (std::size_t space = 0;
                     space < padding;
                     ++space) {
                    output.put(' ');
                }
            }
        }
        output.put('\n');
    }
    output.flush();
}

[[nodiscard]] bool save_set_list(
    std::string_view filename,
    std::ostream& output,
    std::ostream& error_output,
    bool color_errors) {
    auto const definitions = combdsl::set_list();
    if (definitions.empty()) {
        output << "Nothing to save\n";
        output.flush();
        return false;
    }

    std::ofstream file(
        std::string(filename),
        std::ios::binary | std::ios::trunc);
    if (!file) {
        auto const message =
            "Could not open " + std::string(filename) +
            " for writing";
        print_red_message_line(
            error_output, message, color_errors);
        return false;
    }

    file.write(
        definitions.data(),
        static_cast<std::streamsize>(definitions.size()));
    file.close();
    if (!file) {
        auto const message =
            "Could not write " + std::string(filename);
        print_red_message_line(
            error_output, message, color_errors);
        return false;
    }

    output << "Saved " << filename << '\n';
    output.flush();
    return true;
}

[[nodiscard]] bool load_set_list_file(
    std::string_view filename,
    std::ostream& output,
    std::ostream& error_output,
    bool color_errors) {
    std::ifstream file(std::string(filename), std::ios::binary);
    if (!file) {
        auto const message =
            "Could not open " + std::string(filename) +
            " for reading";
        print_red_message_line(
            error_output, message, color_errors);
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        auto const message =
            "Could not read " + std::string(filename);
        print_red_message_line(
            error_output, message, color_errors);
        return false;
    }

    auto const load_result =
        combdsl::web_detail::load_set_list(contents.str());
    if (!load_result.success) {
        auto const diagnostics =
            combdsl::web_detail::format_file_load_diagnostics(
                filename, load_result);
        if (!diagnostics.empty()) {
            print_red_message_line(
                error_output, diagnostics, color_errors);
        }
        return false;
    }

    output << "Loaded " << filename << '\n';
    output.flush();
    return true;
}

[[nodiscard]] bool same_expression(
    combdsl::quoted_expression const& left,
    combdsl::quoted_expression const& right) noexcept {
    return combdsl::detail::quoted_access::root(left) ==
           combdsl::detail::quoted_access::root(right);
}

void print_expression_line(
    std::ostream& output,
    combdsl::quoted_expression const& expression) {
    expression.print_to(output);
    combdsl::detail::print_layout(output, "\n");
    output.flush();
}

void print_no_further_reductions(std::ostream& output) {
    output << no_further_reductions_message << '\n';
    output.flush();
}

[[nodiscard]] bool has_next_reduction(
    combdsl::quoted_expression const& expression,
    bool basis_step,
    bool reduce_partial_k_argument = true) {
    return combdsl::detail::has_next_redex(
        expression,
        combdsl::detail::reduction_options{
            .basis_step = basis_step,
            .reduce_partial_k_argument = reduce_partial_k_argument,
        });
}

[[nodiscard]] combdsl::evaluation_outcome parse_and_crepl_eval(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    combdsl::evaluation_progress_callback const& progress_callback,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const& step_limit_callback,
    combdsl::evaluation_interrupt_callback const& interrupt_callback) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return combdsl::evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return combdsl::evaluation_outcome::completed;
    }
    if (!has_next_reduction(parsed.expression, basis_step, false)) {
        print_no_further_reductions(output);
        return combdsl::evaluation_outcome::completed;
    }
    return combdsl::eval_with_outcome(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        progress_callback,
        step_limit,
        step_limit_callback,
        interrupt_callback);
}

[[nodiscard]] combdsl::evaluation_outcome
parse_and_crepl_single_step(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const& step_limit_callback,
    combdsl::evaluation_interrupt_callback const& interrupt_callback) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return combdsl::evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return combdsl::evaluation_outcome::completed;
    }
    if (!has_next_reduction(parsed.expression, basis_step)) {
        print_no_further_reductions(output);
        return combdsl::evaluation_outcome::completed;
    }
    return combdsl::single_step_run_with_outcome(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        combdsl::evaluation_progress_callback{},
        step_limit,
        step_limit_callback,
        interrupt_callback);
}

void write_step_output(
    std::ostream& output,
    std::ostringstream const& step_output) {
    auto const text = step_output.str();
    output.write(
        text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
}

enum class keypress_action {
    step,
    resume,
    quit,
    interrupted,
    end
};

bool ignore_empty_line_after_key_quit = false;

#if defined(__unix__) || defined(__APPLE__)
volatile std::sig_atomic_t terminal_resize_pending = 0;

termios crepl_startup_terminal_attributes{};
bool crepl_startup_terminal_attributes_valid = false;

[[nodiscard]] bool read_terminal_attributes(
    termios& attributes) noexcept {
    while (::tcgetattr(STDIN_FILENO, &attributes) != 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_terminal_attributes(
    termios const& attributes) noexcept {
    while (::tcsetattr(
               STDIN_FILENO, TCSANOW, &attributes) != 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

void restore_crepl_terminal_at_exit() noexcept {
    if (!crepl_startup_terminal_attributes_valid) {
        return;
    }
    static_cast<void>(write_terminal_attributes(
        crepl_startup_terminal_attributes));
}

class scoped_crepl_terminal_restoration {
public:
    explicit scoped_crepl_terminal_restoration(bool enabled) noexcept {
        if (!enabled ||
            !read_terminal_attributes(
                crepl_startup_terminal_attributes)) {
            return;
        }
        crepl_startup_terminal_attributes_valid = true;
        active_ = true;
        static_cast<void>(std::atexit(
            restore_crepl_terminal_at_exit));
    }

    scoped_crepl_terminal_restoration(
        scoped_crepl_terminal_restoration const&) = delete;
    scoped_crepl_terminal_restoration& operator=(
        scoped_crepl_terminal_restoration const&) = delete;

    ~scoped_crepl_terminal_restoration() {
        if (active_) {
            restore_crepl_terminal_at_exit();
        }
    }

private:
    bool active_ = false;
};

void terminal_sigwinch_handler(int) noexcept {
    terminal_resize_pending = 1;
}

class scoped_terminal_sigwinch_handler {
public:
    explicit scoped_terminal_sigwinch_handler(bool enabled) noexcept {
        terminal_resize_pending = 0;
        if (!enabled) {
            return;
        }

        struct sigaction action {};
        action.sa_handler = terminal_sigwinch_handler;
        static_cast<void>(sigemptyset(&action.sa_mask));
        action.sa_flags = 0;
        installed_ =
            ::sigaction(SIGWINCH, &action, &previous_) == 0;
    }

    scoped_terminal_sigwinch_handler(
        scoped_terminal_sigwinch_handler const&) = delete;
    scoped_terminal_sigwinch_handler& operator=(
        scoped_terminal_sigwinch_handler const&) = delete;

    ~scoped_terminal_sigwinch_handler() {
        if (installed_) {
            static_cast<void>(
                ::sigaction(SIGWINCH, &previous_, nullptr));
        }
    }

private:
    struct sigaction previous_ {};
    bool installed_ = false;
};

void handle_pending_terminal_resize() noexcept {
    if (terminal_resize_pending != 0) {
        auto const was_redisplaying =
            RL_ISSTATE(RL_STATE_REDISPLAYING) != 0;
        if (!was_redisplaying) {
            RL_SETSTATE(RL_STATE_REDISPLAYING);
        }
        rl_resize_terminal();
        if (!was_redisplaying) {
            RL_UNSETSTATE(RL_STATE_REDISPLAYING);
        }
        terminal_resize_pending = 0;
    }
}

#endif

class terminal_keypress_reader {
public:
    terminal_keypress_reader() noexcept {
#if defined(__unix__) || defined(__APPLE__)
        if (!read_terminal_attributes(saved_attributes_)) {
            return;
        }

        auto keypress_attributes = saved_attributes_;
        keypress_attributes.c_lflag &=
            static_cast<tcflag_t>(~(ICANON | ECHO));
        keypress_attributes.c_cc[VMIN] = 0;
        keypress_attributes.c_cc[VTIME] = 1;
        if (!write_terminal_attributes(keypress_attributes)) {
            return;
        }
        restore_attributes_ = true;
#endif
    }

    terminal_keypress_reader(
        terminal_keypress_reader const&) = delete;
    terminal_keypress_reader& operator=(
        terminal_keypress_reader const&) = delete;

    ~terminal_keypress_reader() {
#if defined(__unix__) || defined(__APPLE__)
        if (!restore_attributes_) {
            return;
        }
        static_cast<void>(
            write_terminal_attributes(saved_attributes_));
#endif
    }

    [[nodiscard]] keypress_action read() noexcept {
#if defined(_WIN32)
        auto const key = ::_getch();
        if (key == EOF) {
            return keypress_action::end;
        }
        if (key == 0x03) {
            return keypress_action::interrupted;
        }
        if (key == 0 || key == 0xe0) {
            if (::_getch() == EOF) {
                return keypress_action::end;
            }
            return keypress_action::step;
        }
        if (key == 'q' || key == 'Q') {
            return keypress_action::quit;
        }
        if (key == '\r' || key == '\n') {
            return keypress_action::resume;
        }
        return keypress_action::step;
#elif defined(__unix__) || defined(__APPLE__)
        if (!restore_attributes_) {
            return keypress_action::end;
        }
        auto const first = read_byte();
        if (!first) {
            return keypress_action::end;
        }
        if (*first == 'q' || *first == 'Q') {
            return keypress_action::quit;
        }
        if (*first == '\r' || *first == '\n') {
            return keypress_action::resume;
        }

        if (*first == 0x1b) {
            consume_escape_sequence();
        } else {
            consume_utf8_continuations(*first);
        }
        return keypress_action::step;
#else
        char key = '\0';
        if (!std::cin.get(key)) {
            return keypress_action::end;
        }
        if (key == 'q' || key == 'Q') {
            return keypress_action::quit;
        }
        return key == '\r' || key == '\n'
                   ? keypress_action::resume
                   : keypress_action::step;
#endif
    }

private:
#if defined(__unix__) || defined(__APPLE__)
    [[nodiscard]] static std::optional<unsigned char>
    read_byte() noexcept {
        unsigned char value = 0;
        for (;;) {
            handle_pending_terminal_resize();
            if (combdsl::detail::evaluation_interrupt_pending()) {
                return std::nullopt;
            }
            auto const count =
                ::read(STDIN_FILENO, &value, sizeof value);
            if (count == 1) {
                return value;
            }
            if (count == 0) {
                pollfd descriptor{STDIN_FILENO, POLLIN, 0};
                auto const poll_result =
                    ::poll(&descriptor, 1, 0);
                if (poll_result == 1 &&
                    (descriptor.revents &
                     (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                    return std::nullopt;
                }
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
    }

    [[nodiscard]] static bool input_available(
        int timeout_milliseconds) noexcept {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        auto const result =
            ::poll(&descriptor, 1, timeout_milliseconds);
        return result == 1 &&
               (descriptor.revents & POLLIN) != 0;
    }

    static void consume_utf8_continuations(
        unsigned char first) noexcept {
        std::size_t remaining =
            first >= 0xc2 && first <= 0xdf
                ? 1
                : first >= 0xe0 && first <= 0xef
                    ? 2
                    : first >= 0xf0 && first <= 0xf4
                        ? 3
                        : 0;
        while (remaining != 0 && input_available(10)) {
            if (!read_byte()) {
                return;
            }
            --remaining;
        }
    }

    static void consume_escape_sequence() noexcept {
        if (!input_available(10)) {
            return;
        }

        auto const introducer = read_byte();
        if (!introducer) {
            return;
        }
        if (*introducer != '[' && *introducer != 'O') {
            consume_utf8_continuations(*introducer);
            return;
        }

        while (input_available(10)) {
            auto const next = read_byte();
            if (!next ||
                (*next >= 0x40 && *next <= 0x7e)) {
                return;
            }
        }
    }

    termios saved_attributes_{};
    bool restore_attributes_ = false;
#endif
};

[[nodiscard]] keypress_action read_terminal_keypress() noexcept {
    if (combdsl::detail::consume_evaluation_interrupt()) {
        return keypress_action::interrupted;
    }
    terminal_keypress_reader reader;
    auto const action = reader.read();
    return combdsl::detail::consume_evaluation_interrupt()
               ? keypress_action::interrupted
               : action;
}

[[nodiscard]] keypress_action terminal_keypress_action() noexcept {
    auto const action = read_terminal_keypress();
    if (action == keypress_action::quit) {
        ignore_empty_line_after_key_quit = true;
    }
    return action == keypress_action::resume
               ? keypress_action::step
               : action;
}

[[nodiscard]] bool wait_for_crepl_resume_key(
    terminal_keypress_reader& reader) noexcept {
    for (;;) {
        auto const action = reader.read();
        if (action == keypress_action::resume) {
            return true;
        }
        if (action == keypress_action::quit) {
            ignore_empty_line_after_key_quit = true;
            return false;
        }
        if (action == keypress_action::interrupted ||
            combdsl::detail::consume_evaluation_interrupt()) {
            continue;
        }
        if (action == keypress_action::end) {
            return false;
        }
    }
}

[[nodiscard]] bool prompt_for_crepl_resume_key(
    std::ostream& output,
    std::string_view message,
    bool use_terminal_color) {
    terminal_keypress_reader reader;
    write_red_message(output, message, use_terminal_color);
    output.write(
        crepl_resume_prompt_suffix.data(),
        static_cast<std::streamsize>(
            crepl_resume_prompt_suffix.size()));
    output.flush();
    return wait_for_crepl_resume_key(reader);
}

[[nodiscard]] combdsl::evaluation_step_limit_callback
make_step_limit_callback(
    bool enabled,
    std::ostream& output,
    bool use_terminal_color,
    std::function<void()> before_prompt) {
    if (!enabled) {
        return {};
    }

    return [
               &output,
               use_terminal_color,
               before_prompt = std::move(before_prompt)](
               std::size_t reductions) {
        if (before_prompt) {
            before_prompt();
        }
        auto const message = step_limit_message(reductions);
        return prompt_for_crepl_resume_key(
            output, message, use_terminal_color);
    };
}

[[nodiscard]] combdsl::evaluation_interrupt_callback
make_interrupt_callback(
    bool enabled,
    std::ostream& output,
    bool use_terminal_color,
    std::function<void()> before_prompt) {
    if (!enabled) {
        return {};
    }

    return [
               &output,
               use_terminal_color,
               before_prompt = std::move(before_prompt)] {
        if (before_prompt) {
            before_prompt();
        }
        return prompt_for_crepl_resume_key(
            output, "[interrupted]", use_terminal_color);
    };
}

[[nodiscard]] bool step_limit_exhausted(
    std::optional<std::size_t> step_limit,
    std::size_t reductions) noexcept {
    return step_limit && reductions >= *step_limit;
}

[[nodiscard]] combdsl::evaluation_outcome terminal_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const&
        step_limit_callback,
    combdsl::evaluation_interrupt_callback const&
        interrupt_callback) {
    if (!has_next_reduction(expression, basis_step)) {
        print_no_further_reductions(output);
        return combdsl::evaluation_outcome::completed;
    }
    combdsl::detail::scoped_evaluation_sigint_handler sigint_handler;
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    std::size_t reductions = 0;
    bool allow_one_reduction_after_zero_limit = false;
    for (;;) {
        if (combdsl::detail::consume_evaluation_interrupt() &&
            !(interrupt_callback
                  ? interrupt_callback()
                  : combdsl::detail::wait_for_evaluation_resume(
                        std::cin, output))) {
            return combdsl::evaluation_outcome::cancelled;
        }
        if (step_limit_exhausted(step_limit, reductions) &&
            !allow_one_reduction_after_zero_limit) {
            if (!combdsl::detail::has_next_redex(
                    expression,
                    combdsl::detail::reduction_options{
                        .basis_step = basis_step,
                    })) {
                return combdsl::evaluation_outcome::completed;
            }
            if (!step_limit_callback) {
                return combdsl::evaluation_outcome::step_limit_reached;
            }
            if (!step_limit_callback(reductions)) {
                return combdsl::evaluation_outcome::cancelled;
            }
            reductions = 0;
            allow_one_reduction_after_zero_limit =
                step_limit && *step_limit == 0;
            continue;
        }
        auto const action = terminal_keypress_action();
        if (action == keypress_action::interrupted) {
            auto const resume = interrupt_callback
                ? interrupt_callback()
                : combdsl::detail::wait_for_evaluation_resume(
                      std::cin, output);
            if (resume) {
                continue;
            }
            return combdsl::evaluation_outcome::cancelled;
        }
        if (action != keypress_action::step) {
            return combdsl::evaluation_outcome::cancelled;
        }

        auto next = combdsl::single_step(expression, basis_step);
        if (same_expression(next, expression)) {
            return combdsl::evaluation_outcome::completed;
        }

        expression = std::move(next);
        ++reductions;
        allow_one_reduction_after_zero_limit = false;
        print_expression_line(output, expression);

        if (!combdsl::detail::has_next_redex(
                expression,
                combdsl::detail::reduction_options{
                    .basis_step = basis_step,
                })) {
            return combdsl::evaluation_outcome::completed;
        }
    }
}

[[nodiscard]] combdsl::evaluation_outcome colorized_single_step_run(
    combdsl::quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const&
        step_limit_callback,
    combdsl::evaluation_interrupt_callback const&
        interrupt_callback) {
    if (!has_next_reduction(expression, basis_step)) {
        print_no_further_reductions(output);
        return combdsl::evaluation_outcome::completed;
    }
    combdsl::detail::scoped_evaluation_sigint_handler sigint_handler;
    bool reduced = false;
    std::size_t reductions = 0;
    bool allow_one_reduction_after_zero_limit = false;
    output.flush();

    for (;;) {
        if (!combdsl::detail::wait_after_single_step_run_interrupt(
                input, output, interrupt_callback)) {
            return combdsl::evaluation_outcome::cancelled;
        }

        if (step_limit_exhausted(step_limit, reductions) &&
            !allow_one_reduction_after_zero_limit) {
            auto const reducible = combdsl::detail::has_next_redex(
                expression,
                combdsl::detail::reduction_options{
                    .basis_step = basis_step,
                });
            if (reduced || reducible) {
                print_expression_line(output, expression);
            }
            if (!reducible) {
                return combdsl::evaluation_outcome::completed;
            }
            if (!step_limit_callback) {
                return combdsl::evaluation_outcome::step_limit_reached;
            }
            if (!step_limit_callback(reductions)) {
                return combdsl::evaluation_outcome::cancelled;
            }
            reductions = 0;
            allow_one_reduction_after_zero_limit =
                step_limit && *step_limit == 0;
            continue;
        }

        std::ostringstream step_output;
        step_output.imbue(output.getloc());
        auto next = combdsl::color_step_ansi(
            expression, step_output, basis_step);
        auto const no_reduction = same_expression(next, expression);

        if (!combdsl::detail::wait_after_single_step_run_interrupt(
                input, output, interrupt_callback)) {
            return combdsl::evaluation_outcome::cancelled;
        }

        if (no_reduction) {
            if (reduced) {
                print_expression_line(output, expression);
            }
            return combdsl::evaluation_outcome::completed;
        }
        write_step_output(output, step_output);
        expression = std::move(next);
        reduced = true;
        ++reductions;
        allow_one_reduction_after_zero_limit = false;
    }
}

[[nodiscard]] combdsl::evaluation_outcome colorized_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const&
        step_limit_callback,
    combdsl::evaluation_interrupt_callback const&
        interrupt_callback) {
    if (!has_next_reduction(expression, basis_step)) {
        print_no_further_reductions(output);
        return combdsl::evaluation_outcome::completed;
    }
    combdsl::detail::scoped_evaluation_sigint_handler sigint_handler;
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    std::size_t reductions = 0;
    bool allow_one_reduction_after_zero_limit = false;
    for (;;) {
        if (combdsl::detail::consume_evaluation_interrupt() &&
            !(interrupt_callback
                  ? interrupt_callback()
                  : combdsl::detail::wait_for_evaluation_resume(
                        std::cin, output))) {
            return combdsl::evaluation_outcome::cancelled;
        }
        if (step_limit_exhausted(step_limit, reductions) &&
            !allow_one_reduction_after_zero_limit) {
            if (!combdsl::detail::has_next_redex(
                    expression,
                    combdsl::detail::reduction_options{
                        .basis_step = basis_step,
                    })) {
                return combdsl::evaluation_outcome::completed;
            }
            if (reductions != 0) {
                print_expression_line(output, expression);
            }
            if (!step_limit_callback) {
                return combdsl::evaluation_outcome::step_limit_reached;
            }
            if (!step_limit_callback(reductions)) {
                return combdsl::evaluation_outcome::cancelled;
            }
            reductions = 0;
            allow_one_reduction_after_zero_limit =
                step_limit && *step_limit == 0;
            continue;
        }
        auto const action = terminal_keypress_action();
        if (action == keypress_action::interrupted) {
            auto const resume = interrupt_callback
                ? interrupt_callback()
                : combdsl::detail::wait_for_evaluation_resume(
                      std::cin, output);
            if (resume) {
                continue;
            }
            return combdsl::evaluation_outcome::cancelled;
        }
        if (action != keypress_action::step) {
            return combdsl::evaluation_outcome::cancelled;
        }

        std::ostringstream step_output;
        step_output.imbue(output.getloc());
        auto next = combdsl::color_step_ansi(
            expression, step_output, basis_step);
        if (same_expression(next, expression)) {
            return combdsl::evaluation_outcome::completed;
        }

        write_step_output(output, step_output);
        expression = std::move(next);
        ++reductions;
        allow_one_reduction_after_zero_limit = false;

        if (!combdsl::detail::has_next_redex(
                expression,
                combdsl::detail::reduction_options{
                    .basis_step = basis_step,
                })) {
            print_expression_line(output, expression);
            return combdsl::evaluation_outcome::completed;
        }
    }
}

[[nodiscard]] combdsl::evaluation_outcome parse_and_color_step_ansi(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    bool key_step,
    std::optional<std::size_t> step_limit,
    combdsl::evaluation_step_limit_callback const&
        step_limit_callback,
    combdsl::evaluation_interrupt_callback const&
        interrupt_callback) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return combdsl::evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return combdsl::evaluation_outcome::completed;
    }

    if (key_step) {
        return colorized_key_step_loop(
            std::move(parsed.expression),
            output,
            basis_step,
            std::nullopt,
            combdsl::evaluation_step_limit_callback{},
            interrupt_callback);
    }
    return colorized_single_step_run(
        std::move(parsed.expression),
        output,
        input,
        basis_step,
        step_limit,
        step_limit_callback,
        interrupt_callback);
}

[[nodiscard]] combdsl::evaluation_outcome parse_and_terminal_key_step(
    std::string_view source,
    std::ostream& output,
    bool basis_step,
    combdsl::evaluation_interrupt_callback const&
        interrupt_callback) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return combdsl::evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return combdsl::evaluation_outcome::completed;
    }
    return terminal_key_step_loop(
        std::move(parsed.expression),
        output,
        basis_step,
        std::nullopt,
        combdsl::evaluation_step_limit_callback{},
        interrupt_callback);
}

[[nodiscard]] bool standard_output_is_terminal() noexcept {
    return stream_is_terminal(stdout);
}

[[nodiscard]] bool standard_input_is_terminal() noexcept {
    return stream_is_terminal(stdin);
}

[[nodiscard]] bool standard_error_is_terminal() noexcept {
    return stream_is_terminal(stderr);
}

class progress_output_buffer final : public std::streambuf {
public:
    explicit progress_output_buffer(std::streambuf* destination)
        : destination_(destination) {}

    ~progress_output_buffer() override {
        clear_progress();
    }

    void update_progress(std::size_t reductions) {
        latest_reductions_ = reductions;
        if (reductions % 1000 == 0) {
            show_progress(reductions);
        }
    }

    void finalize_progress_line() {
        if (latest_reductions_ < 1000) {
            return;
        }

        clear_progress();
        auto const message = progress_message(latest_reductions_);
        destination_->sputn(
            message.data(),
            static_cast<std::streamsize>(message.size()));
        destination_->sputc('\n');
        destination_->pubsync();
    }

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(
                character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }

        clear_progress();
        return destination_->sputc(
            traits_type::to_char_type(character));
    }

    std::streamsize xsputn(
        char const* characters,
        std::streamsize count) override {
        if (count != 0) {
            clear_progress();
        }
        constexpr std::string_view interrupted = "Interrupted.";
        constexpr std::string_view styled_interrupt = "[interrupted]";
        if (count == static_cast<std::streamsize>(interrupted.size()) &&
            std::string_view(
                characters, static_cast<std::size_t>(count)) ==
                interrupted) {
            auto const rendered = fmt::format(
                fmt::fg(fmt::color::red), "{}", styled_interrupt);
            auto const written = destination_->sputn(
                rendered.data(),
                static_cast<std::streamsize>(rendered.size()));
            return written ==
                    static_cast<std::streamsize>(rendered.size())
                ? count
                : 0;
        }
        return destination_->sputn(characters, count);
    }

    int sync() override {
        return destination_->pubsync();
    }

private:
    [[nodiscard]] static std::string progress_message(
        std::size_t reductions) {
        return "[" + std::to_string(reductions) +
               " steps so far]";
    }

    void show_progress(std::size_t reductions) {
        auto const message = progress_message(reductions);
        destination_->sputc('\r');
        destination_->sputn(
            message.data(),
            static_cast<std::streamsize>(message.size()));
        for (auto length = message.size(); length < progress_width_;
             ++length) {
            destination_->sputc(' ');
        }
        if (message.size() > progress_width_) {
            progress_width_ = message.size();
        }
        destination_->pubsync();
        progress_visible_ = true;
    }

    void clear_progress() noexcept {
        if (!progress_visible_) {
            return;
        }

        destination_->sputc('\r');
        for (std::size_t length = 0; length < progress_width_;
             ++length) {
            destination_->sputc(' ');
        }
        destination_->sputc('\r');
        destination_->pubsync();
        progress_width_ = 0;
        progress_visible_ = false;
    }

    std::streambuf* destination_;
    std::size_t latest_reductions_ = 0;
    std::size_t progress_width_ = 0;
    bool progress_visible_ = false;
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 &&
        std::string_view(argv[1]) == "--version") {
        print_about(std::cout);
        return 0;
    }

    auto const interactive_output = standard_output_is_terminal();
    auto const interactive_input = standard_input_is_terminal();
    auto const interactive_error_output =
        standard_error_is_terminal();
#if defined(__unix__) || defined(__APPLE__)
    scoped_crepl_terminal_restoration terminal_restoration(
        interactive_input);
    scoped_terminal_sigwinch_handler sigwinch_handler(
        interactive_input);
#endif
    auto active_stepping_mode = stepping_mode::none;
    bool basis_step_mode = false;
    bool colorize_mode = false;
    std::optional<std::size_t> step_limit;
    if (interactive_output) {
        print_crepl_banner(std::cout);
        std::cout << '\n';
    }

    std::optional<crepl_persistence_paths> persistence;
    if (interactive_input) {
        rl_initialize();
        using_history();
        persistence = make_crepl_persistence_paths();
        if (persistence) {
            load_persistent_history(*persistence);
            load_persistent_filenames(*persistence);
        }
        capture_canonical_readline_history();
        readline_history_recall.persistence = persistence
            ? &*persistence
            : nullptr;
        readline_history_recall.underlying_getc = rl_getc_function;
        readline_history_recall.underlying_redisplay =
            rl_redisplay_function;
        rl_getc_function = crepl_readline_getc;
        rl_redisplay_function = crepl_readline_redisplay;
        rl_attempted_completion_function = crepl_attempted_completion;
    }
    std::string source;
    while (std::cin) {
#if defined(__unix__) || defined(__APPLE__)
        handle_pending_terminal_resize();
#endif
        if (interactive_input) {
            prepare_readline_history_recall();
            char *line = readline(interactive_output ? ">" : "");
            restore_readline_bell_style();
            if (line == nullptr) {
                break;
            }
            source = line;
            std::free(line);
        } else if (!std::getline(std::cin, source)) {
            break;
        }
        if (ignore_empty_line_after_key_quit) {
            ignore_empty_line_after_key_quit = false;
            if (source.empty()) {
                continue;
            }
        }
        if (is_exact_command(source, "quit") ||
            is_exact_command(source, "exit")) {
            break;
        }
        if (interactive_input && !source.empty() &&
            !last_history_entry_matches(source)) {
            add_history(source.c_str());
            remember_canonical_readline_history(source);
            if (persistence) {
                append_persistent_history(
                    *persistence,
                    readline_history_recall.canonical_history,
                    source);
            }
        }

        try {
            if (is_exact_command(source, "about")) {
                print_about(std::cout);
                continue;
            }
            if (is_exact_command(source, "birds")) {
                print_birds(std::cout);
                continue;
            }
            if (auto const filename =
                    parse_filename_command(source, "load")) {
                if (load_set_list_file(
                        *filename,
                        std::cout,
                        std::cerr,
                        interactive_error_output)) {
                    last_load_filename = *filename;
                    if (persistence) {
                        save_persistent_filenames(*persistence);
                    }
                }
                continue;
            }
            if (auto const filename =
                    parse_filename_command(source, "save")) {
                if (save_set_list(
                        *filename,
                        std::cout,
                        std::cerr,
                        interactive_error_output)) {
                    last_save_filename = *filename;
                    if (persistence) {
                        save_persistent_filenames(*persistence);
                    }
                }
                continue;
            }
            if (auto const detail = parse_help_command(source)) {
                if (*detail == help_detail::full) {
                    print_help_full(std::cout);
                } else {
                    print_help_brief(std::cout);
                }
                continue;
            }
            if (auto const command =
                    combdsl::parse_step_limit_command(source)) {
                step_limit = command->enabled
                    ? std::optional{command->limit}
                    : std::nullopt;
                continue;
            }
            if (auto const command =
                    parse_mode_command(source)) {
                if (command->kind == mode_command_kind::basis) {
                    basis_step_mode = command->enabled;
                    continue;
                }
                if (command->kind == mode_command_kind::colorize) {
                    colorize_mode = command->enabled;
                    continue;
                }
                if (command->kind == mode_command_kind::key &&
                    !interactive_input) {
                    continue;
                }

                auto const requested_mode =
                    command->kind == mode_command_kind::single
                        ? stepping_mode::single
                        : stepping_mode::key;
                if (command->enabled) {
                    active_stepping_mode = requested_mode;
                } else if (active_stepping_mode == requested_mode) {
                    active_stepping_mode = stepping_mode::none;
                }
                continue;
            }

            if (is_show_all_command(source) &&
                combdsl::set_list().empty()) {
                print_red_message_line(
                    std::cout,
                    "Nothing to show",
                    interactive_output);
                continue;
            }

            auto const escaped_source =
                combdsl::input_escape(source);
            if (begins_command(source, "abstract")) {
                auto const parsed =
                    combdsl::detail::parse_input(escaped_source);
                print_expression_line(
                    std::cout, parsed.expression);
                continue;
            }
            if (begins_command(source, "find")) {
                auto const parsed =
                    combdsl::detail::parse_input(escaped_source);
                if (parsed.is_find_no_match) {
                    print_red_message_line(
                        std::cout,
                        "No match within search bounds",
                        interactive_output);
                } else {
                    print_expression_line(
                        std::cout, parsed.expression);
                }
                continue;
            }
            if (!interactive_output) {
                auto const step_limit_pause =
                    make_step_limit_callback(
                        interactive_input,
                        std::cout,
                        false);
                auto const interrupt_pause =
                    make_interrupt_callback(
                        interactive_input,
                        std::cout,
                        false);
                auto outcome = combdsl::evaluation_outcome::completed;
                if (active_stepping_mode == stepping_mode::single) {
                    if (colorize_mode) {
                        outcome = parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            false,
                            step_limit,
                            step_limit_pause,
                            interrupt_pause);
                    } else {
                        outcome = parse_and_crepl_single_step(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            step_limit,
                            step_limit_pause,
                            interrupt_pause);
                    }
                } else if (
                    active_stepping_mode == stepping_mode::key) {
                    if (colorize_mode) {
                        outcome = parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            true,
                            step_limit,
                            step_limit_pause,
                            interrupt_pause);
                    } else {
                        outcome = parse_and_terminal_key_step(
                            escaped_source,
                            std::cout,
                            basis_step_mode,
                            interrupt_pause);
                    }
                } else {
                    outcome = parse_and_crepl_eval(
                        escaped_source,
                        std::cout,
                        std::cin,
                        false,
                        combdsl::evaluation_progress_callback{},
                        step_limit,
                        step_limit_pause,
                        interrupt_pause);
                }
                report_evaluation_outcome(
                    outcome,
                    std::cout,
                    false,
                    step_limit);
                continue;
            }

            progress_output_buffer output_buffer(std::cout.rdbuf());
            std::ostream evaluation_output(&output_buffer);
            auto const step_limit_pause =
                make_step_limit_callback(
                    interactive_input,
                    evaluation_output,
                    true,
                    [&output_buffer] {
                        output_buffer.finalize_progress_line();
                    });
            auto const interrupt_pause =
                make_interrupt_callback(
                    interactive_input,
                    evaluation_output,
                    true,
                    [&output_buffer] {
                        output_buffer.finalize_progress_line();
                    });
            if (active_stepping_mode == stepping_mode::single) {
                auto outcome = combdsl::evaluation_outcome::completed;
                if (colorize_mode) {
                    outcome = parse_and_color_step_ansi(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        false,
                        step_limit,
                        step_limit_pause,
                        interrupt_pause);
                } else {
                    outcome = parse_and_crepl_single_step(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        step_limit,
                        step_limit_pause,
                        interrupt_pause);
                }
                report_evaluation_outcome(
                    outcome,
                    evaluation_output,
                    true,
                    step_limit);
                continue;
            }
            if (active_stepping_mode == stepping_mode::key) {
                auto outcome = combdsl::evaluation_outcome::completed;
                if (colorize_mode) {
                    outcome = parse_and_color_step_ansi(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        true,
                        step_limit,
                        step_limit_pause,
                        interrupt_pause);
                } else {
                    outcome = parse_and_terminal_key_step(
                        escaped_source,
                        evaluation_output,
                        basis_step_mode,
                        interrupt_pause);
                }
                report_evaluation_outcome(
                    outcome,
                    evaluation_output,
                    true,
                    step_limit);
                continue;
            }

            combdsl::evaluation_progress_callback progress =
                [&output_buffer](std::size_t reductions) {
                output_buffer.update_progress(reductions);
            };
            auto const outcome = parse_and_crepl_eval(
                escaped_source,
                evaluation_output,
                std::cin,
                false,
                progress,
                step_limit,
                step_limit_pause,
                interrupt_pause);
            report_evaluation_outcome(
                outcome,
                evaluation_output,
                true,
                step_limit);
        } catch (combdsl::parse_error const& error) {
            print_red_message_line(
                std::cerr,
                error.what(),
                interactive_error_output);
        }
    }
}
