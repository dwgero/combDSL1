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

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstddef>
#include <iostream>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr std::string_view crepl_version = "1.11.0";

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
        "basis step [on | off]                 | names are converted to their stored expressions as a step\n"
        "birds                                 | display the pre-defined bird combinators\n"
        "colorize [on | off]                   | add colors to arguments while stepping\n"
        "define <name> <xyz...> = <expression> | compute and store a series of combinators such that\n"
        "                                      | <name> <xyz...> reduces to <expression>\n"
        "exit                                  | end the program\n"
        "help [brief | full]                   | display help information\n"
        "key step [on | off]                   | after each step, wait for a keypress to continue\n"
        "quit                                  | end the program\n"
        "set <name> = [number] <expression>    | store <expression> as <name> with arity <number> or 0\n"
        "show <name>                           | display the arity and stored expression of <name>\n"
        "single step [on | off]                | display each step of the reduction without pause\n";
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
           << "set <name> = <combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "In the first form, the expression always reduces immediately.");
    output << '\n'
           << "set <name> = <arity> <combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "In the second form, arity is a number specifying the minimal "
        "number of arguments required for the expression to reduce.");
    output << '\n'
           << "define <name> <symbol_list> = <combinator_expression>\n";
    write_wrapped_paragraph(
        output,
        "The \"define\" form requires one or more symbols (lower case "
        "letters) after the name, infers the arity from their count, and "
        "computes a series of combinators that will reproduce the "
        "combinator_expression. For a one-character name, the space "
        "before the symbols may be omitted. For example, to add the "
        "Eagle bird:");
    output << "define Exyzwv = xy(zwv)\n\n";
    write_wrapped_paragraph(
        output,
        "User-defined combinator names created by \"set\" or \"define\" "
        "may be redefined. Pre-defined bird combinator names are immutable "
        "and cannot be redefined.");
    output << '\n'
           << "show <name>\n";
    write_wrapped_paragraph(
        output,
        "The show command displays the arity and combinators stored for "
        "name. For example, \"show E\" for the Eagle bird would display "
        "\"arity:5 BDD\".");

    output << "\nOther Commands\n\n"
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
    // Keep this text in sync with web/index.html's About dialog.
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
        "<https://github.com/dwgero/combDSL1>. "
        "My hidden email is <airings-pinker.3e@icloud.com>";

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
        bird_entry{"Dove", "Dxyzw = xy(zw)"},
        bird_entry{"Identity", "Ix = x"},
        bird_entry{"Kestrel", "Kxy = x"},
        bird_entry{"Lark", "Lxy = x(yy)"},
        bird_entry{"Mockingbird", "Mx = xx"},
        bird_entry{"Nightingale", "Nxy = xyx"},
        bird_entry{"Owl", "Oxy = y(xy)"},
        bird_entry{"Peacock", "Pxyz = y(xz)"},
        bird_entry{"Robin", "Rxyz = yzx"},
        bird_entry{"Sage", "Yx = x(Yx)"},
        bird_entry{"Starling", "Sxyz = xz(yz)"},
        bird_entry{"Thrush", "Txy = yx"},
        bird_entry{"Vireo", "Vxyz = zxy"},
        bird_entry{"Warbler", "Wxy = xyy"},
        bird_entry{"Zazu", "Zxy = x(xy)"}};
    constexpr std::size_t maximum_line_length = 80;
    constexpr std::size_t column_gap = 2;

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

void terminal_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step) {
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    for (;;) {
        if (!terminal_key_requests_step()) {
            return;
        }

        auto next = combdsl::single_step(expression, basis_step);
        if (same_expression(next, expression)) {
            return;
        }

        expression = std::move(next);
        print_expression_line(output, expression);

        auto following = combdsl::single_step(expression, basis_step);
        if (same_expression(following, expression)) {
            return;
        }
    }
}

void colorized_single_step_run(
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
            return;
        }

        std::ostringstream step_output;
        step_output.imbue(output.getloc());
        auto next = combdsl::color_step_ansi(
            expression, step_output, basis_step);
        auto const no_reduction = same_expression(next, expression);

        if (!combdsl::detail::wait_after_single_step_run_interrupt(
                input, output)) {
            return;
        }

        if (no_reduction) {
            if (reduced) {
                print_expression_line(output, expression);
            }
            return;
        }

        write_step_output(output, step_output);
        expression = std::move(next);
        reduced = true;
    }
}

void colorized_key_step_loop(
    combdsl::quoted_expression expression,
    std::ostream& output,
    bool basis_step) {
    combdsl::detail::print_layout(
        output,
        "Press any key for one reduction step; press q or Q to quit.\n");
    print_expression_line(output, expression);

    for (;;) {
        if (!terminal_key_requests_step()) {
            return;
        }

        std::ostringstream step_output;
        step_output.imbue(output.getloc());
        auto next = combdsl::color_step_ansi(
            expression, step_output, basis_step);
        if (same_expression(next, expression)) {
            return;
        }

        write_step_output(output, step_output);
        expression = std::move(next);

        auto following = combdsl::single_step(expression, basis_step);
        if (same_expression(following, expression)) {
            print_expression_line(output, expression);
            return;
        }
    }
}

void parse_and_color_step_ansi(
    std::string_view source,
    std::ostream& output,
    std::istream& input,
    bool basis_step,
    bool key_step) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return;
    }

    if (key_step) {
        colorized_key_step_loop(
            std::move(parsed.expression),
            output,
            basis_step);
        return;
    }
    colorized_single_step_run(
        std::move(parsed.expression),
        output,
        input,
        basis_step);
}

void parse_and_terminal_key_step(
    std::string_view source,
    std::ostream& output,
    bool basis_step) {
    auto parsed = combdsl::detail::parse_input(source);
    if (parsed.is_definition) {
        return;
    }
    if (parsed.is_display_only) {
        print_expression_line(output, parsed.expression);
        return;
    }
    terminal_key_step_loop(
        std::move(parsed.expression), output, basis_step);
}

[[nodiscard]] bool standard_output_is_terminal() noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(::fileno(stdout)) != 0;
#else
    return false;
#endif
}

[[nodiscard]] bool standard_input_is_terminal() noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdin)) != 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(::fileno(stdin)) != 0;
#else
    return false;
#endif
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
    auto active_stepping_mode = stepping_mode::none;
    bool basis_step_mode = false;
    bool colorize_mode = false;
    if (interactive_output) {
        print_crepl_banner(std::cout);
        std::cout << '\n';
    }

    std::string source;
    while (std::cin) {
        if (interactive_output) {
            std::cout << '>' << std::flush;
        }

        if (!std::getline(std::cin, source)) {
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

            auto const escaped_source =
                combdsl::input_escape(source);
            if (!interactive_output) {
                if (active_stepping_mode == stepping_mode::single) {
                    if (colorize_mode) {
                        parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            false);
                    } else {
                        combdsl::parse_and_step(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode);
                    }
                } else if (
                    active_stepping_mode == stepping_mode::key) {
                    if (colorize_mode) {
                        parse_and_color_step_ansi(
                            escaped_source,
                            std::cout,
                            std::cin,
                            basis_step_mode,
                            true);
                    } else {
                        parse_and_terminal_key_step(
                            escaped_source,
                            std::cout,
                            basis_step_mode);
                    }
                } else {
                    combdsl::parse_eval(escaped_source);
                }
                continue;
            }

            progress_output_buffer output_buffer(std::cout.rdbuf());
            std::ostream evaluation_output(&output_buffer);
            if (active_stepping_mode == stepping_mode::single) {
                if (colorize_mode) {
                    parse_and_color_step_ansi(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        false);
                } else {
                    combdsl::parse_and_step(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode);
                }
                continue;
            }
            if (active_stepping_mode == stepping_mode::key) {
                if (colorize_mode) {
                    parse_and_color_step_ansi(
                        escaped_source,
                        evaluation_output,
                        std::cin,
                        basis_step_mode,
                        true);
                } else {
                    parse_and_terminal_key_step(
                        escaped_source,
                        evaluation_output,
                        basis_step_mode);
                }
                continue;
            }

            combdsl::evaluation_progress_callback progress =
                [&output_buffer](std::size_t reductions) {
                if (reductions % 1000 == 0) {
                    output_buffer.show_progress(reductions);
                }
            };
            combdsl::parse_eval(
                escaped_source,
                evaluation_output,
                std::cin,
                false,
                progress);
        } catch (combdsl::parse_error const& error) {
            std::cerr << error.what() << '\n';
        }
    }
}
