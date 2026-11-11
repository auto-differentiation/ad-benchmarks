// W4 test 4: CppAD residual Jacobian == XAD (rtol 1e-8).
//
// Contract (workloads/sabr/cppad.hpp): sabr::CppadJacobian jac(market) records
// the residuals once (CppAD::ADFun, optimize()); jac(x) evaluates the
// recorded function's Jacobian at x without re-taping. See parity.hpp.

#include "tests/sabr/parity.hpp"
#include "workloads/sabr/cppad.hpp"

TEST(SabrParity, CppadMatchesXad) { sabr_test::expect_matches_xad<sabr::CppadJacobian>("cppad"); }

// Repeated sweeps reuse CppAD's pool instead of going back to the system.
TEST(SabrCppadPool, SweepLeavesFreedMemoryInThePool)
{
    sabr::CppadJacobian jac(sabr::standard_market());
    jac(sabr::standard_params());
    EXPECT_GT(CppAD::thread_alloc::available(0), 0u);
}
