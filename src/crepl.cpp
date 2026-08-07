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

#include <combdsl/combinators.hpp>
#include <combdsl/color_step_ansi.hpp>

#include <fmt/color.h>

#include "web/load_set_list.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include <readline/history.h>
#include <readline/readline.h>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view crepl_version = "2.5.1";

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

void report_evaluation_outcome(
    combdsl::evaluation_outcome outcome,
    std::ostream& output,
    bool use_terminal_color) {
    if (outcome == combdsl::evaluation_outcome::cancelled) {
        print_red_message_line(
            output, "[cancelled]", use_terminal_color);
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

constexpr std::array<std::string_view, 24> command_completion_candidates = {
    "about", "abstract", "basis", "birds", "colorize", "define",
    "depends", "depends-on", "dependson", "exit", "find", "help",
    "key", "load", "quit", "remove", "save", "set", "show",
    "single", "snapshot", "used", "used-by", "usedby"};
constexpr std::array<std::string_view, 1> step_completion_candidates = {
    "step"};
constexpr std::array<std::string_view, 2> toggle_completion_candidates = {
    "off", "on"};
constexpr std::array<std::string_view, 2> help_completion_candidates = {
    "brief", "full"};
constexpr std::array<std::string_view, 1>
    abstract_question_completion_candidates = {"?"};
constexpr std::array<std::string_view, 1>
    abstract_steps_completion_candidates = {"steps"};
constexpr std::array<std::string_view, 2>
    definition_reference_completion_candidates = {
        "captured", "live"};
constexpr std::array<std::string_view, 1>
    depends_on_completion_candidates = {"on"};
constexpr std::array<std::string_view, 1>
    used_by_completion_candidates = {"by"};
constexpr std::array<std::string_view, 1> show_completion_candidates = {
    "all"};
constexpr std::array<std::string_view, 5> find_completion_candidates = {
    "all", "1", "2", "3", "4"};
constexpr std::array<std::string_view, 4>
    find_after_all_completion_candidates = {"1", "2", "3", "4"};
std::string last_save_filename = "set_list.cmb";
std::array<std::string_view, 1> save_filename_completion_candidates;
std::string last_load_filename = "set_list.cmb";
std::array<std::string_view, 1> load_filename_completion_candidates;

constexpr int persistent_history_limit = 500;
constexpr std::string_view settings_header = "crepl-settings 1";

struct crepl_persistence_paths {
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
            (directory / "history").string(),
            directory / "settings"};
    } catch (...) {
        return std::nullopt;
    }
}

void load_persistent_history(
    crepl_persistence_paths const& paths) noexcept {
    stifle_history(persistent_history_limit);
    read_history(paths.history.c_str());
}

void append_persistent_history(
    crepl_persistence_paths const& paths) noexcept {
    auto result = append_history(1, paths.history.c_str());
    if (result != 0) {
        result = write_history(paths.history.c_str());
    }
    if (result == 0) {
        history_truncate_file(
            paths.history.c_str(), persistent_history_limit);
    }
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
    std::array<std::string_view, 3> words{};
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
        if (words[0] == "snapshot") {
            return make_completion_candidates(toggle_completion_candidates);
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
                    abstract_steps_completion_candidates);
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
    if (word_count == 2 && words[0] == "find" &&
        words[1] == "all") {
        return make_completion_candidates(
            find_after_all_completion_candidates);
    }
    if (word_count == 2 && words[0] == "abstract" &&
        words[1] == "steps") {
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
        "Commands:\n"
        "about                                 | display copyright and redistribution information\n"
        "abstract [steps] ?<symbols> = <expression> | display combinator abstraction\n"
        "basis step [on | off]                 | names are converted to their stored expressions as a step\n"
        "birds                                 | display the pre-defined bird combinators\n"
        "colorize [on | off]                   | add colors to arguments while stepping\n"
        "define [captured | live] <name> <xyz...> = <expression>\n"
        "                                      | compute and store combinators such that\n"
        "                                      | <name> <xyz...> reduces to <expression>\n"
        "dependson <name> | depends-on <name> | depends on <name>\n"
        "                                      | display named bases that directly contain <name>\n"
        "exit                                  | end the program\n"
        "find [all] [num] ?<symbols> = <expression> | find matching pre-defined bird forms\n"
        "help [brief | full]                   | display help information\n"
        "key step [on | off]                   | after each step, wait for a keypress to continue\n"
        "load <filename>                       | load a set-list journal from a file\n"
        "quit                                  | end the program\n"
        "remove <name>                         | remove a user-defined combinator name\n"
        "save <filename>                       | save the set-list journal to a file\n"
        "set [captured | live] <name> = [number] <expression>\n"
        "                                      | store <expression> as <name> with arity <number> or 0\n"
        "show <name | name@N | all>            | display one revision or the entire set list\n"
        "single step [on | off]                | display each step of the reduction without pause\n"
        "snapshot [on | off]                   | capture named definitions or follow later changes\n"
        "usedby <name> | used-by <name> | used by <name>\n"
        "                                      | display named bases directly contained in <name>\n";
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
    output << '\n'
           << "define [captured | live] <name> <symbol_list> = "
              "<combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "The \"define\" form requires one or more symbols (lower case "
        "letters) after the name, infers the arity from their count, and "
        "computes a series of combinators that will reproduce the "
        "combinator_expression. For a one-character name, the space "
        "before the symbols may be omitted. For example, to add the Eagle "
        "bird:");
    output << "define Exyzwv = xy(zwv)\n\n";
    write_wrapped_paragraph(
        output,
        "The optional \"captured\" or \"live\" modifier on set and "
        "define overrides snapshot mode for that command only, without "
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
        "names are immutable and cannot be redefined.");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "A redefinition is rejected when resolving its live references "
        "would make the resulting definition graph circular. A frozen or "
        "version-qualified reference cannot create a cycle by itself, "
        "although a frozen revision can contain live references. Recursive "
        "\"define\" remains allowed.");
    output << '\n'
           << "snapshot [on | off]\n";
    write_wrapped_paragraph(
        output,
        "Controls how unqualified user-defined names in subsequently parsed "
        "input are stored when set or define has no captured/live modifier. "
        "Snapshot mode is on initially, and the bare "
        "\"snapshot\" command also turns it on. When on, each reference "
        "captures the current immutable revision and prints as \"name@N\". "
        "When off, each reference remains live, prints as \"name\", and "
        "follows later redefinitions. Each changed \"set\" or \"define\" "
        "creates the next revision. An explicit \"name@N\" reference is "
        "always immutable regardless of the mode. Before the first saved "
        "set, define, or remove, only the last explicit snapshot command is "
        "saved. If there is none, the saved set list begins with \"snapshot "
        "on\". Later snapshot commands remain in chronological order.");
    output << '\n'
           << "show <name | name@N | all>\n";
    write_wrapped_paragraph(
        output,
        "The show command displays the arity and combinators stored for the "
        "current name, while \"show name@N\" displays that exact immutable "
        "revision. For example, \"show E\" for the Eagle bird would display "
        "\"arity:5 BDD\". The \"show all\" form displays the entire saved set "
        "list, or \"Nothing to show\" when it is empty.");
    output << '\n'
           << "remove <name>\n";
    write_wrapped_paragraph(
        output,
        "Removes a user-defined combinator name without discarding its "
        "immutable revisions. Frozen references remain fixed. Live "
        "references retain the most recent target while the name is absent "
        "and follow the new current revision if the name is added again. "
        "The removal remains in the chronological saved set list. "
        "Pre-defined names cannot be removed.");

    output << "\nInspecting Dependencies\n\n"
           << "dependson <name>\n"
           << "depends-on <name>\n"
           << "depends on <name>\n";
    write_wrapped_paragraph(
        output,
        "The three forms are equivalent. They display the named bases whose "
        "definitions directly contain name. Results are printed as \"A is "
        "depended on by: B C\" or \"A is not depended on by anything\".");
    output << '\n'
           << "usedby <name>\n"
           << "used-by <name>\n"
           << "used by <name>\n";
    write_wrapped_paragraph(
        output,
        "The three forms are equivalent. They display the named bases "
        "directly contained in name's definition. Results are printed as "
        "\"A uses: B C\" or \"A uses nothing\".");
    output.put('\n');
    write_wrapped_paragraph(
        output,
        "Both searches include pre-defined and current user-defined named "
        "bases, but exclude the fundamental names S, K, I, and Y.");

    output << "\nAbstracting Expressions\n\n"
           << "abstract [steps] ?<symbol_list> = <combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "Abstracts the listed lowercase symbols from the expression, from "
        "right to left, without evaluating it. A question mark must "
        "immediately precede the symbols. The plain form displays only the "
        "result as \"?=<expression>\". The optional \"steps\" word also "
        "displays changed preprocessing, each takeout, and each optimizer "
        "substitution, ending with the same \"?=\" result line. Abstract "
        "ignores the stepping and colorize modes.");

    output << "\nFinding Combinators\n\n"
           << "find [all] [num] ?<symbol_list> = <combinator_expression>\n";
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
        "existing file. The filename is the rest of the command after "
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
    quit,
    end
};

bool ignore_empty_line_after_key_quit = false;

#if defined(__unix__) || defined(__APPLE__)
volatile std::sig_atomic_t keypress_interrupted = 0;
volatile std::sig_atomic_t terminal_resize_pending = 0;

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

void keypress_sigint_handler(int) noexcept {
    keypress_interrupted = 1;
}

class scoped_keypress_sigint_handler {
public:
    scoped_keypress_sigint_handler() noexcept {
        keypress_interrupted = 0;
        struct sigaction action {};
        action.sa_handler = keypress_sigint_handler;
        static_cast<void>(sigemptyset(&action.sa_mask));
        action.sa_flags = 0;
        installed_ =
            ::sigaction(SIGINT, &action, &previous_) == 0;
    }

    scoped_keypress_sigint_handler(
        scoped_keypress_sigint_handler const&) = delete;
    scoped_keypress_sigint_handler& operator=(
        scoped_keypress_sigint_handler const&) = delete;

    ~scoped_keypress_sigint_handler() {
        if (installed_) {
            static_cast<void>(
                ::sigaction(SIGINT, &previous_, nullptr));
        }
    }

    [[nodiscard]] bool installed() const noexcept {
        return installed_;
    }

    [[nodiscard]] bool interrupted() const noexcept {
        return keypress_interrupted != 0;
    }

private:
    struct sigaction previous_ {};
    bool installed_ = false;
};
#endif

class terminal_keypress_reader {
public:
    terminal_keypress_reader() noexcept {
#if defined(__unix__) || defined(__APPLE__)
        while (::tcgetattr(
                   STDIN_FILENO, &saved_attributes_) != 0) {
            if (errno != EINTR) {
                return;
            }
        }

        auto keypress_attributes = saved_attributes_;
        keypress_attributes.c_lflag &=
            static_cast<tcflag_t>(~(ICANON | ECHO));
        keypress_attributes.c_cc[VMIN] = 0;
        keypress_attributes.c_cc[VTIME] = 1;
        while (::tcsetattr(
                   STDIN_FILENO,
                   TCSANOW,
                   &keypress_attributes) != 0) {
            if (errno != EINTR) {
                return;
            }
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
        while (::tcsetattr(
                   STDIN_FILENO,
                   TCSANOW,
                   &saved_attributes_) != 0) {
            if (errno != EINTR) {
                break;
            }
        }
#endif
    }

    [[nodiscard]] keypress_action read() noexcept {
#if defined(_WIN32)
        auto const key = ::_getch();
        if (key == EOF) {
            return keypress_action::end;
        }
        if (key == 0x03) {
            return keypress_action::end;
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
        return key == 'q' || key == 'Q'
                   ? keypress_action::quit
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
            if (keypress_interrupted != 0) {
                return std::nullopt;
            }
            auto const count =
                ::read(STDIN_FILENO, &value, sizeof value);
            if (count == 1) {
                return value;
            }
            if (count == 0 || errno == EINTR) {
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
#if defined(__unix__) || defined(__APPLE__)
    scoped_keypress_sigint_handler sigint_handler;
    if (!sigint_handler.installed()) {
        return keypress_action::end;
    }
    terminal_keypress_reader reader;
    if (sigint_handler.interrupted()) {
        return keypress_action::end;
    }
    auto const action = reader.read();
    return sigint_handler.interrupted()
               ? keypress_action::end
               : action;
#else
    terminal_keypress_reader reader;
    return reader.read();
#endif
}

[[nodiscard]] bool terminal_key_requests_step() noexcept {
    auto const action = read_terminal_keypress();
    if (action == keypress_action::quit) {
        ignore_empty_line_after_key_quit = true;
    }
    return action == keypress_action::step;
}

[[nodiscard]] combdsl::evaluation_outcome terminal_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step) {
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    for (;;) {
        if (!terminal_key_requests_step()) {
            return combdsl::evaluation_outcome::cancelled;
        }

        auto next = combdsl::single_step(expression, basis_step);
        if (same_expression(next, expression)) {
            return combdsl::evaluation_outcome::completed;
        }

        expression = std::move(next);
        print_expression_line(output, expression);

        auto following = combdsl::single_step(expression, basis_step);
        if (same_expression(following, expression)) {
            return combdsl::evaluation_outcome::completed;
        }
    }
}

[[nodiscard]] combdsl::evaluation_outcome colorized_single_step_run(
    combdsl::quoted_expression expression,
    std::ostream& output,
    std::istream& input,
    bool basis_step) {
    combdsl::detail::scoped_evaluation_sigint_handler sigint_handler;
    bool reduced = false;
    output.flush();

    for (;;) {
        if (!combdsl::detail::wait_after_single_step_run_interrupt(
                input, output)) {
            return combdsl::evaluation_outcome::cancelled;
        }

        std::ostringstream step_output;
        step_output.imbue(output.getloc());
        auto next = combdsl::color_step_ansi(
            expression, step_output, basis_step);
        auto const no_reduction = same_expression(next, expression);

        if (!combdsl::detail::wait_after_single_step_run_interrupt(
                input, output)) {
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
    }
}

[[nodiscard]] combdsl::evaluation_outcome colorized_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step) {
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    for (;;) {
        if (!terminal_key_requests_step()) {
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

        auto following = combdsl::single_step(expression, basis_step);
        if (same_expression(following, expression)) {
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
    bool key_step) {
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
            basis_step);
    }
    return colorized_single_step_run(
        std::move(parsed.expression),
        output,
        input,
        basis_step);
}

[[nodiscard]] combdsl::evaluation_outcome parse_and_terminal_key_step(
    std::string_view source,
    std::ostream& output,
    bool basis_step) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return combdsl::evaluation_outcome::completed;
    }
    if (parsed.is_definition) {
        return combdsl::evaluation_outcome::completed;
    }
    return terminal_key_step_loop(
        std::move(parsed.expression), output, basis_step);
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

    void show_progress(std::size_t reductions) {
        auto const message = std::to_string(reductions) + " steps";
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
        return destination_->sputn(characters, count);
    }

    int sync() override {
        return destination_->pubsync();
    }

private:
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
    scoped_terminal_sigwinch_handler sigwinch_handler(
        interactive_input);
#endif
    auto active_stepping_mode = stepping_mode::none;
    bool basis_step_mode = false;
    bool colorize_mode = false;
    if (interactive_output) {
        print_crepl_banner(std::cout);
        std::cout << '\n';
    }

    std::optional<crepl_persistence_paths> persistence;
    if (interactive_input) {
        using_history();
        persistence = make_crepl_persistence_paths();
        if (persistence) {
            load_persistent_history(*persistence);
            load_persistent_filenames(*persistence);
        }
        rl_attempted_completion_function = crepl_attempted_completion;
    }
    std::string source;
    while (std::cin) {
#if defined(__unix__) || defined(__APPLE__)
        handle_pending_terminal_resize();
#endif
        if (interactive_input) {
            char *line = readline(interactive_output ? ">" : "");
            if (line == nullptr) {
                break;
            }
            source = line;
            std::free(line);
            if (!source.empty()) {
                add_history(source.c_str());
                if (persistence) {
                    append_persistent_history(*persistence);
                }
            }
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
                auto outcome = combdsl::evaluation_outcome::completed;
                if (active_stepping_mode == stepping_mode::single) {
                    if (colorize_mode) {
                        outcome = parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            false);
                    } else {
                        outcome = combdsl::parse_and_step_with_outcome(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode);
                    }
                } else if (
                    active_stepping_mode == stepping_mode::key) {
                    if (colorize_mode) {
                        outcome = parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            true);
                    } else {
                        outcome = parse_and_terminal_key_step(
                            escaped_source,
                            std::cout,
                            basis_step_mode);
                    }
                } else {
                    outcome = combdsl::parse_eval_with_outcome(
                        escaped_source,
                        std::cout,
                        std::cin,
                        false,
                        combdsl::evaluation_progress_callback{});
                }
                report_evaluation_outcome(
                    outcome, std::cout, false);
                continue;
            }

            progress_output_buffer output_buffer(std::cout.rdbuf());
            std::ostream evaluation_output(&output_buffer);
            if (active_stepping_mode == stepping_mode::single) {
                auto outcome = combdsl::evaluation_outcome::completed;
                if (colorize_mode) {
                    outcome = parse_and_color_step_ansi(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        false);
                } else {
                    outcome = combdsl::parse_and_step_with_outcome(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode);
                }
                report_evaluation_outcome(
                    outcome, evaluation_output, true);
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
                        true);
                } else {
                    outcome = parse_and_terminal_key_step(
                        escaped_source,
                        evaluation_output,
                        basis_step_mode);
                }
                report_evaluation_outcome(
                    outcome, evaluation_output, true);
                continue;
            }

            combdsl::evaluation_progress_callback progress =
                [&output_buffer](std::size_t reductions) {
                if (reductions % 1000 == 0) {
                    output_buffer.show_progress(reductions);
                }
            };
            auto const outcome = combdsl::parse_eval_with_outcome(
                escaped_source,
                evaluation_output,
                std::cin,
                false,
                progress);
            report_evaluation_outcome(
                outcome, evaluation_output, true);
        } catch (combdsl::parse_error const& error) {
            print_red_message_line(
                std::cerr,
                error.what(),
                interactive_error_output);
        }
    }
}
