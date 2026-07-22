/*
 * C++ combinator DSL
 * Copyright (C) 2026  David W. Gero
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <combdsl/combinators.hpp>

#include <cstdio>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

[[nodiscard]] bool standard_output_is_terminal() noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::isatty(::fileno(stdout)) != 0;
#else
    return false;
#endif
}

} // namespace

int main() {
    auto const interactive_output = standard_output_is_terminal();
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
            combdsl::parse_eval(combdsl::input_escape(source));
        } catch (combdsl::parse_error const& error) {
            std::cerr << error.what() << '\n';
        }
    }
}
