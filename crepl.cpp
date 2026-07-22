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
