#include <combdsl/combinators.hpp>

namespace {

const auto external_basis = combdsl::basis("Extern", 1, combdsl::I);

} // namespace

void ensure_external_basis_registered() {
    static_cast<void>(external_basis);
}
