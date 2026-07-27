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

#include <cstdio>
#include <cstddef>
#include <iostream>
#include <streambuf>
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
            auto const escaped_source =
                combdsl::input_escape(source);
            if (!interactive_output) {
                combdsl::parse_eval(escaped_source);
                continue;
            }

            progress_output_buffer output_buffer(std::cout.rdbuf());
            std::ostream evaluation_output(&output_buffer);
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
