// W1 test 3: the XAD gradient matches the central finite-difference gradient
// of the plain double price (small case, same pre-generated draws).
//
// FD: harness::fd_gradient over x = [L0[0..N-1], lambda[0..N-2]] with one
// absolute step h = 1e-6 for every component. The payoff has kinks
// (max(., 0)), but with 4096 paths no path sits within h of one, so the
// pathwise derivative and FD agree to FD accuracy. rtol 1e-3 (brief).

#include "harness/check.hpp"
#include "harness/fd.hpp"
#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {

lmm::Inputs<double> unflatten(const std::vector<double>& x, int N)
{
    lmm::Inputs<double> in;
    in.L0.assign(x.begin(), x.begin() + N);
    in.lambda.assign(x.begin() + N, x.end());
    return in;
}

}  // namespace

TEST(LmmFd, XadGradientMatchesCentralFd)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    const lmm::Inputs<double> in = lmm_case::small_inputs();

    std::vector<double> x = in.L0;
    x.insert(x.end(), in.lambda.begin(), in.lambda.end());
    const auto f = [&](const std::vector<double>& xs) {
        return lmm::price<double>(s, unflatten(xs, s.N), z);
    };
    const std::vector<double> fd = harness::fd_gradient(f, x, 1e-6);
    const std::vector<double> g = lmm::xad_gradient(s, in, z);

    const harness::CheckResult r = harness::check(g, fd, 1e-3);
    EXPECT_TRUE(r.ok) << "worst component " << r.worst_index << ": xad "
                      << (r.worst_index < g.size() ? g[r.worst_index] : 0.0) << " fd "
                      << fd[r.worst_index] << ", rel error " << r.worst_rel_error;
}
