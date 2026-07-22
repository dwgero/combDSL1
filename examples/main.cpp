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
