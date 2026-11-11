// W4 tests 4-8: shared check that a library's residual Jacobian equals the
// XAD one (rtol 1e-8).
//
// Every library column is a class constructed once from the market,
//   sabr::<Lib>Jacobian jac(market);  std::vector<double> J = jac(x);
// returning the row-major n_quotes x n_params Jacobian, like XadJacobian.
// The same object is evaluated at two different points in turn, so a column
// that records (or generates) once must pick up the new x each call.

#pragma once

#include "harness/check.hpp"
#include "workloads/sabr/model.hpp"
#include "workloads/sabr/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace sabr_test {

// Two non-trivial points: the calibrated parameters moved in opposite ways.
inline std::vector<std::vector<double>> parity_points()
{
    std::vector<double> a = sabr::standard_params(), b = a;
    for (std::size_t e = 0; e < a.size() / 3; ++e) {
        a[3 * e] *= 1.1;
        a[3 * e + 1] += 0.05;
        a[3 * e + 2] *= 0.9;
        b[3 * e] *= 0.85;
        b[3 * e + 1] -= 0.1;
        b[3 * e + 2] *= 1.2;
    }
    return {a, b, a};
}

template <class Jacobian>
void expect_matches_xad(const char* name)
{
    const sabr::Market m = sabr::standard_market();
    sabr::XadJacobian xad_jac(m);
    Jacobian jac(m);
    for (const std::vector<double>& x : parity_points()) {
        const std::vector<double> ref = xad_jac(x);
        const std::vector<double> J = jac(x);
        ASSERT_EQ(J.size(), ref.size()) << name;
        const std::size_t n = x.size();
        const harness::CheckResult r = harness::check(J, ref, 1e-8);
        EXPECT_TRUE(r.ok) << name << ": worst entry (quote " << r.worst_index / n
                          << ", parameter " << r.worst_index % n << "), rel error "
                          << r.worst_rel_error << " at alpha_0 = " << x[0];
    }
}

}  // namespace sabr_test
