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

#include <iostream>
#include <utility>

int main() {
    using namespace combdsl;

    constexpr auto add = [](int left) {
        return [left](int right) { return left + right; };
    };

    // S(add)(I)(x) = add(x)(I(x)) = x + x
    auto twice = S(add)(I);

    // S(K(square))(increment)(x) = square(increment(x))
    constexpr auto square = [](int value) { return value * value; };
    constexpr auto increment = [](int value) { return value + 1; };
    auto square_after_increment = S(K(square))(increment);

    auto factorial_generator = [](auto self) {
        return [self = std::move(self)](auto value) -> unsigned long long {
            return value < 2 ? 1 : value * force(self)(value - 1);
        };
    };
    auto factorial = Y(factorial_generator);

    std::cout << "I(42)                         = " << I(42) << '\n';
    std::cout << "K(42)(0)                      = " << K(42)(0) << '\n';
    std::cout << "S(K)(K)(42)                   = " << S(K)(K)(42) << '\n';
    std::cout << "S(add)(I)(21)                 = " << twice(21) << '\n';
    std::cout << "S(K(square))(increment)(4)    = "
              << square_after_increment(4) << '\n';
    std::cout << "Y(factorial_generator)(10)    = " << factorial(10) << '\n';
}
