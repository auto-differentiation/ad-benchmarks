// W4 test 3: the XAD residual Jacobian equals the central finite-difference
// Jacobian (rtol 1e-3) at a non-trivial point, away from the calibrated
// parameters (non-zero residuals).
//
// Contract checked here (workloads/sabr/fd.hpp):
//   sabr::FdJacobian jac(market, h);   // h: absolute step, same for every parameter
//   std::vector<double> J = jac(x);    // row-major n_quotes x n_params, like XadJacobian;
//       // column j = (residuals<double>(x + h e_j) - residuals<double>(x - h e_j)) / (2h)
// and the block structure: residual i depends only on the three parameters of
// its own expiry, so every other entry is exactly zero, and every entry in the
// block is non-zero at this point.

#include "harness/check.hpp"
#include "workloads/sabr/fd.hpp"
#include "workloads/sabr/model.hpp"
#include "workloads/sabr/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

TEST(SabrJacobian, XadMatchesCentralFd)
{
    const sabr::Market m = sabr::standard_market();
    std::vector<double> x = sabr::standard_params();
    const std::size_t n = x.size();
    const std::size_t nq = m.K.size();
    for (std::size_t e = 0; e < n / 3; ++e) {
        x[3 * e] *= 1.1;         // alpha
        x[3 * e + 1] += 0.05;    // rho
        x[3 * e + 2] *= 0.9;     // nu
    }

    sabr::XadJacobian xad_jac(m);
    sabr::FdJacobian fd_jac(m, 1e-6);
    const std::vector<double> J = xad_jac(x);
    const std::vector<double> Jfd = fd_jac(x);
    ASSERT_EQ(J.size(), nq * n);
    ASSERT_EQ(Jfd.size(), nq * n);

    for (std::size_t i = 0; i < nq; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            const bool in_block = j / 3 == m.expiry[i];
            if (in_block)
                EXPECT_NE(J[i * n + j], 0.0) << "quote " << i << ", parameter " << j;
            else
                EXPECT_EQ(J[i * n + j], 0.0) << "quote " << i << ", parameter " << j;
        }

    const harness::CheckResult r = harness::check(J, Jfd, 1e-3);
    EXPECT_TRUE(r.ok) << "worst entry (quote " << r.worst_index / n << ", parameter "
                      << r.worst_index % n << "), rel error " << r.worst_rel_error;
}
