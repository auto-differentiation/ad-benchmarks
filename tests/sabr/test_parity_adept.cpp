// W4 test 6: Adept residual Jacobian == XAD (rtol 1e-8).
//
// Contract (workloads/sabr/adept.hpp): sabr::AdeptJacobian jac(market);
// jac(x) returns the row-major n_quotes x n_params Jacobian. Mode: forward,
// like every column (record, then stack.jacobian_forward()). See parity.hpp.

#include "tests/sabr/parity.hpp"
#include "workloads/sabr/adept.hpp"

TEST(SabrParity, AdeptMatchesXad) { sabr_test::expect_matches_xad<sabr::AdeptJacobian>("adept"); }
