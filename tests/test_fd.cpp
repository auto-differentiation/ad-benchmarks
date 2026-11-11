// H6: the central FD gradient of x^2 + sin y is within 1e-6.
//
// Contract checked here:
//   std::vector<double> g = harness::fd_gradient(f, x, h);
//   - f: any callable taking const std::vector<double>& and returning double;
//   - h is an absolute step, the same for every component (no default);
//   - central differences: g[i] = (f(x + h e_i) - f(x - h e_i)) / (2h);
//   - f is called exactly 2n times for n = x.size(), and x is not modified.

#include "harness/check.hpp"
#include "harness/fd.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

double f_xy(const std::vector<double>& v) { return v[0] * v[0] + std::sin(v[1]); }

std::vector<double> analytic(const std::vector<double>& v) { return {2.0 * v[0], std::cos(v[1])}; }

}  // namespace

TEST(Fd, CentralGradientOfXSquaredPlusSinY)
{
    const double h = 1e-5;
    for (const std::vector<double>& x :
         {std::vector<double>{1.5, 0.3}, std::vector<double>{-2.0, 2.0}}) {
        const std::vector<double> g = harness::fd_gradient(f_xy, x, h);
        const harness::CheckResult r = harness::check(g, analytic(x), 1e-6);
        EXPECT_TRUE(r.ok) << "x = (" << x[0] << ", " << x[1] << "), worst component "
                          << r.worst_index << ", rel error " << r.worst_rel_error;
    }
}

TEST(Fd, CallsFTwicePerInputAndLeavesXUnchanged)
{
    const std::vector<double> x = {0.7, -1.2, 3.0};
    const std::vector<double> x_before = x;
    std::size_t calls = 0;
    auto f = [&](const std::vector<double>& v) {
        ++calls;
        return v[0] * v[1] + v[2];
    };

    const std::vector<double> g = harness::fd_gradient(f, x, 1e-6);

    EXPECT_EQ(calls, 2 * x.size());
    EXPECT_EQ(x, x_before);
    ASSERT_EQ(g.size(), x.size());
    // d/dx0 = x1, d/dx1 = x0, d/dx2 = 1
    EXPECT_TRUE(harness::check(g, {-1.2, 0.7, 1.0}, 1e-6).ok);
}

TEST(Fd, UsesTheGivenStepSymmetrically)
{
    // For f = v^3 the central difference is exactly 3v^2 + h^2, so a large
    // step shows which h was used and that the scheme is central.
    const std::vector<double> x = {2.0};
    auto f = [](const std::vector<double>& v) { return v[0] * v[0] * v[0]; };
    const std::vector<double> g = harness::fd_gradient(f, x, 0.5);
    ASSERT_EQ(g.size(), 1u);
    EXPECT_NEAR(g[0], 12.25, 1e-12);
}
