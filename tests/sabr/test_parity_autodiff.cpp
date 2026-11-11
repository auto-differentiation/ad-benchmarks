// W4 test 7: autodiff residual Jacobian == XAD (rtol 1e-8).
//
// Contract (workloads/sabr/autodiff.hpp): sabr::AutodiffJacobian jac(market);
// jac(x) returns the row-major n_quotes x n_params Jacobian. Mode: forward,
// autodiff::dual, one seeded pass per parameter (15 passes): the natural mode
// for a 100 x 15 Jacobian. See parity.hpp.

#include "tests/sabr/parity.hpp"
#include "workloads/sabr/autodiff.hpp"

TEST(SabrParity, AutodiffMatchesXad) { sabr_test::expect_matches_xad<sabr::AutodiffJacobian>("autodiff"); }
