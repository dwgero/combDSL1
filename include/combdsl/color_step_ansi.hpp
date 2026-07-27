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

#pragma once

#include <fmt/color.h>

#include <combdsl/combinators.hpp>

#include <array>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace combdsl {

namespace detail {

[[nodiscard]] inline fmt::rgb
terminal_background_color(argument_color color) noexcept {
    switch (color) {
    case argument_color::red:
        return fmt::rgb(fmt::color::red);
    case argument_color::green:
        return fmt::rgb(0x00, 0xcc, 0x00);
    case argument_color::blue:
        return fmt::rgb(fmt::color::blue);
    case argument_color::dark_orange:
        return fmt::rgb(fmt::color::dark_orange);
    case argument_color::munsell_purple:
        return fmt::rgb(0xcc, 0x00, 0xff);
    }
    return fmt::rgb();
}

[[nodiscard]] inline std::string
make_terminal_argument_color_prefix(argument_color color) {
    // The public styled formatter resets after a complete value. A color-step
    // argument must instead print directly through the caller's stream so its
    // lexical-spacing state is preserved, so use color.h's escape generator
    // for the opening foreground and background sequences.
    auto const foreground =
        fmt::detail::make_foreground_color<char>(
            fmt::color::white);
    auto const background =
        fmt::detail::make_background_color<char>(
            terminal_background_color(color));

    std::string result(foreground.begin(), foreground.end());
    result.append(background.begin(), background.end());
    return result;
}

[[nodiscard]] inline std::string_view
terminal_argument_color_prefix(argument_color color) {
    static std::array<std::string, 5> const prefixes = {
        make_terminal_argument_color_prefix(argument_color::red),
        make_terminal_argument_color_prefix(argument_color::green),
        make_terminal_argument_color_prefix(argument_color::blue),
        make_terminal_argument_color_prefix(argument_color::dark_orange),
        make_terminal_argument_color_prefix(argument_color::munsell_purple),
    };
    return prefixes[static_cast<std::size_t>(color)];
}

class terminal_argument_color_renderer final
    : public argument_color_renderer {
public:
    void begin_argument_color(
        std::ostream& output,
        argument_color color) override {
        colors_.push_back(color);
        write_prefix(output, color);
    }

    void end_argument_color(std::ostream& output) override {
        if (colors_.empty()) {
            throw std::logic_error(
                "terminal argument color stack is empty");
        }

        colors_.pop_back();
        output.write(reset.data(),
                     static_cast<std::streamsize>(reset.size()));
        if (!colors_.empty()) {
            write_prefix(output, colors_.back());
        }
    }

private:
    static constexpr std::string_view reset = "\x1b[0m";

    static void write_prefix(
        std::ostream& output,
        argument_color color) {
        auto const prefix = terminal_argument_color_prefix(color);
        output.write(prefix.data(),
                     static_cast<std::streamsize>(prefix.size()));
    }

    std::vector<argument_color> colors_;
};

inline void print_quoted_terminal(
    std::ostream& output,
    quoted_expression const& expression) {
    if (!output.good()) {
        return;
    }

    auto const context_index = argument_color_renderer_index();
    if (output.pword(context_index) != nullptr) {
        expression.print_to(output);
        return;
    }

    terminal_argument_color_renderer renderer;
    output.pword(context_index) = &renderer;
    try {
        expression.print_to(output);
    } catch (...) {
        output.pword(context_index) = nullptr;
        throw;
    }
    output.pword(context_index) = nullptr;
}

} // namespace detail

[[nodiscard]] inline quoted_expression color_step_ansi(
    quoted_expression expression,
    std::ostream& output = std::cout,
    bool basis_step = false) {
    detail::reduction_trace trace;
    auto reduced = detail::reduce_next_redex(
        expression,
        detail::reduction_options{.basis_step = basis_step},
        &trace);
    auto const& before =
        trace.before ? *trace.before : expression;
    auto const& after =
        trace.after ? *trace.after
                    : (reduced ? *reduced : expression);

    detail::print_layout(output, "  ");
    detail::print_quoted_terminal(output, before);
    detail::print_layout(output, "\n");
    output.flush();

    detail::print_layout(output, "->");
    detail::print_quoted_terminal(output, after);
    detail::print_layout(output, "\n");
    output.flush();

    if (reduced) {
        return detail::strip_argument_colors(after);
    }
    return expression;
}

[[nodiscard]] inline quoted_expression color_step_ansi(
    quoted_expression expression,
    bool basis_step) {
    return color_step_ansi(
        std::move(expression), std::cout, basis_step);
}

} // namespace combdsl
