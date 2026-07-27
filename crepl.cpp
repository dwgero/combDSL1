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

int main() {
    auto const interactive_output = standard_output_is_terminal();
    auto const interactive_input = standard_input_is_terminal();
    auto active_stepping_mode = stepping_mode::none;
    bool basis_step_mode = false;
    bool colorize_mode = false;
    if (interactive_output) {
        std::cout << "Combinator Read-Eval-Print\n";
    }

    std::string source;
    while (std::cin) {
        if (interactive_output) {
            std::cout << "crep> " << std::flush;
        }

        if (!std::getline(std::cin, source) ||
            source == "q" || source == "Q") {
            break;
        }

        try {
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
