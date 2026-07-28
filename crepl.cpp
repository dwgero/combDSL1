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
#include <combdsl/color_step_ansi.hpp>

#include <array>
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
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

constexpr std::string_view crepl_version = "1.7.5";

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
    std::istream& input,
    std::ostream& output,
    bool basis_step) {
    combdsl::detail::print_layout(
        output,
        "Press Enter for one reduction step; type q then Enter to quit.\n");
    print_expression_line(output, expression);

    std::string command;
    while (std::getline(input, command)) {
        if (command == "q" || command == "Q") {
            return;
        }
        if (!command.empty()) {
            continue;
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
            input,
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

        if (!std::getline(std::cin, source) ||
            source == "q" ||
            source == "Q" ||
            is_exact_command(source, "quit") ||
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
                        combdsl::parse_and_key_step(
                            escaped_source,
                            std::cout,
                            std::cin,
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
                    combdsl::parse_and_key_step(
                        escaped_source,
                        evaluation_output,
                        std::cin,
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
