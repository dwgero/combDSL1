#include <combdsl/combinators.hpp>

int main() {
    using namespace combdsl;

    const auto x = symbol('x');
    single_step_loop(quote(S)(K)(I)(x));
}
