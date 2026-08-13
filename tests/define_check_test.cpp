/*
 * C++ Combinator DSL
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

#include <array>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using combdsl::quoted_expression;

[[nodiscard]] std::string expression_string(
    quoted_expression const& expression) {
    std::ostringstream output;
    expression.print_to(output);
    return std::move(output).str();
}

[[nodiscard]] combdsl::detail::quoted_basis_node_base const&
as_basis(quoted_expression const& expression) {
    auto const& root =
        combdsl::detail::quoted_access::root(expression);
    if (root->kind() !=
        combdsl::detail::quoted_node_kind::basis) {
        throw std::logic_error("expected a quoted basis");
    }
    return static_cast<
        combdsl::detail::quoted_basis_node_base const&>(*root);
}

[[nodiscard]] std::string check_name(
    std::string_view basis_name) {
    std::string result = "Check";
    for (auto character : basis_name) {
        if (std::isalnum(
                static_cast<unsigned char>(character))) {
            result.push_back(character);
        } else if (character == '*') {
            result += "star";
        }
    }
    return result;
}

} // namespace

int main() {
    using namespace combdsl;

    std::array const birds{
        quote(A),
        quote(B),
        quote(C),
        quote(C_star),
        quote(C_star_star),
        quote(D),
        quote(E),
        quote(F),
        quote(G),
        quote(H),
        quote(J),
        quote(L),
        quote(M),
        quote(N),
        quote(O),
        quote(Q),
        quote(Q1),
        quote(Q3),
        quote(R),
        quote(T),
        quote(U),
        quote(V),
        quote(W),
        quote(W_star),
        quote(W_star_star),
        quote(W1),
        quote(Z),
    };
    std::array const arguments{
        quoted_atomic{x},
        quoted_atomic{y},
        quoted_atomic{z},
        quoted_atomic{w},
        quoted_atomic{v},
    };
    constexpr std::string_view symbol_names = "xyzwv";

    std::size_t failures = 0;
    for (auto const& bird : birds) {
        auto const& bird_basis = as_basis(bird);
        if (bird_basis.arity() > arguments.size()) {
            std::cerr << "FAILED: " << bird_basis.name()
                      << " has unsupported arity "
                      << bird_basis.arity() << '\n';
            ++failures;
            continue;
        }

        auto semantic_expression = bird;
        for (std::size_t index = 0;
             index < bird_basis.arity();
             ++index) {
            semantic_expression =
                semantic_expression(
                    arguments[index].expression());
        }

        auto normalized =
            detail::normalize_for_combinator_match(
                std::move(semantic_expression));
        if (!normalized) {
            std::cerr << "FAILED: " << bird_basis.name()
                      << " did not normalize\n";
            ++failures;
            continue;
        }

        auto command =
            std::string("define ") +
            check_name(bird_basis.name()) + " " +
            std::string(symbol_names.substr(
                0, bird_basis.arity())) +
            " = " + expression_string(*normalized);
        auto const defined = parse(command);
        auto const defined_body = as_basis(defined).body();

        if (detail::same_parser_definition_expression(
                defined_body, bird)) {
            continue;
        }

        std::cerr << "FAILED: " << bird_basis.name();
        if (detail::same_parser_definition_expression(
                defined_body, bird_basis.body())) {
            std::cerr << " matched only its stored expression";
        } else {
            std::cerr << " produced "
                      << expression_string(defined_body);
        }
        std::cerr << '\n';
        ++failures;
    }

    auto const zero_symbol_definition = parse("define foo=x");
    auto const& zero_symbol_basis = as_basis(zero_symbol_definition);
    if (zero_symbol_basis.name() != "foo" ||
        zero_symbol_basis.arity() != 0 ||
        expression_string(zero_symbol_basis.body()) != "x") {
        std::cerr << "FAILED: define foo=x produced name "
                  << zero_symbol_basis.name() << ", arity "
                  << zero_symbol_basis.arity() << ", body "
                  << expression_string(zero_symbol_basis.body())
                  << '\n';
        ++failures;
    }
    auto const shown_zero_symbol_definition = parse("show foo");
    if (expression_string(shown_zero_symbol_definition) !=
        "arity:0 x") {
        std::cerr << "FAILED: show foo produced "
                  << expression_string(shown_zero_symbol_definition)
                  << '\n';
        ++failures;
    }

    std::cout << birds.size() + 2
              << " named basis define check(s) run, "
              << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
