// Gradient parity of the Heston book: XAD against central FD, and every AD
// library against XAD, all on the same inputs and pre-generated draws.

#include "tests/heston/heston_case.hpp"
#include "workloads/heston/adept.hpp"
#include "workloads/heston/autodiff.hpp"
#include "workloads/heston/cppad.hpp"
#include "workloads/heston/fd.hpp"
#include "workloads/heston/xad.hpp"
#include "harness/check.hpp"

#include <gtest/gtest.h>

#include <vector>

// XAD gradient == central FD on the same draws (rtol 1e-3, small size).
//
// On fixed draws the price is only piecewise smooth in the inputs: it has
// kinks where a path's payoff crosses its strike and where full truncation
// switches max(v, 0) (about 0.1% of the steps of this case are truncated,
// 123 of 100000). A central difference across a kink is off by O(1) on that
// path, so h has to be small enough that hardly any path crosses one between
// x - h and x + h. Measured on this case (worst component xi): h = 1e-3 gives
// 3e-2 relative error, h = 1e-4 gives 6e-3, h = 1e-5 gives 3e-5 and h = 1e-6
// gives 3e-7, i.e. the FD converges to the AD value. h = 1e-5 (one absolute
// step for all inputs, also fine for s0 = 100) leaves a 30x margin under
// rtol 1e-3 and stays far from round-off.
TEST(HestonCheck, XadMatchesCentralFd)
{
    const heston_case::Case c = heston_case::small();
    const std::vector<double> g = heston::xad_gradient(c.x, c.book, c.draws, c.dt);
    const std::vector<double> fd = heston::fd_gradient(c.x, c.book, c.draws, c.dt, /*h=*/1e-5);
    const harness::CheckResult r = harness::check(g, fd, /*rtol=*/1e-3);
    EXPECT_TRUE(r.ok) << "worst d/d" << heston_case::input_names[r.worst_index] << ": xad "
                      << g[r.worst_index] << ", fd " << fd[r.worst_index] << ", rel "
                      << r.worst_rel_error;
}

namespace {

// Library gradient == XAD gradient on the same case, rtol 1e-8.
void expect_matches_xad(const char* library, const std::vector<double>& g)
{
    const heston_case::Case c = heston_case::small();
    const std::vector<double> ref = heston::xad_gradient(c.x, c.book, c.draws, c.dt);
    const harness::CheckResult r = harness::check(g, ref, /*rtol=*/1e-8);
    ASSERT_EQ(g.size(), ref.size()) << library;
    EXPECT_TRUE(r.ok) << library << " worst d/d" << heston_case::input_names[r.worst_index] << ": "
                      << g[r.worst_index] << ", xad " << ref[r.worst_index] << ", rel "
                      << r.worst_rel_error;
}

}  // namespace

// One CppAD tape, recorded at small().x, gives XAD's gradient both at the
// recording point and at shifted_x(): the tape must stay valid for all inputs
// (kinks recorded as conditional expressions, not frozen branches), so the
// timed re-evaluation is the real gradient and not a replay of one point.
TEST(HestonCheck, CppadMatchesXadAtTwoPointsFromOneTape)
{
    const heston_case::Case c = heston_case::small();
    heston::CppadGradient cppad(c.x, c.book, c.draws, c.dt);
    for (const std::vector<double>& x : {heston_case::shifted_x(), c.x}) {
        const std::vector<double> ref = heston::xad_gradient(x, c.book, c.draws, c.dt);
        const std::vector<double> g = cppad(x);
        ASSERT_EQ(g.size(), ref.size());
        const harness::CheckResult r = harness::check(g, ref, /*rtol=*/1e-8);
        EXPECT_TRUE(r.ok) << "s0 = " << x[0] << ": worst d/d" << heston_case::input_names[r.worst_index]
                          << ": cppad " << g[r.worst_index] << ", xad " << ref[r.worst_index]
                          << ", rel " << r.worst_rel_error;
    }
}

TEST(HestonCheck, AdeptMatchesXad)
{
    const heston_case::Case c = heston_case::small();
    expect_matches_xad("adept", heston::adept_gradient(c.x, c.book, c.draws, c.dt));
}

TEST(HestonCheck, AutodiffMatchesXad)
{
    const heston_case::Case c = heston_case::small();
    expect_matches_xad("autodiff", heston::autodiff_gradient(c.x, c.book, c.draws, c.dt));
}

// Repeated sweeps reuse CppAD's pool instead of going back to the system.
TEST(HestonCppad, SweepLeavesFreedMemoryInThePool)
{
    const heston_case::Case c = heston_case::small();
    heston::CppadGradient cppad(c.x, c.book, c.draws, c.dt);
    cppad(c.x);
    EXPECT_GT(CppAD::thread_alloc::available(0), 0u);
}
