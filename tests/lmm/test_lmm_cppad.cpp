// W1 test 4: the CppAD gradient equals the XAD gradient (rtol 1e-8).
//
// Contract (workloads/lmm/cppad.hpp):
//   lmm::CppadGradient g(setup, z);  // records lmm::price once (tape over
//                                     // the small-case structure), optimize()
//   std::vector<double> g(inputs);    // gradient at inputs from that one tape
//                                     // (no re-taping), layout as xad_gradient
// The same object is evaluated at two input points; the second moves many
// paths across the max(., 0) kinks, so a tape that froze the branches taken
// at record time fails here.

#include "harness/check.hpp"
#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/cppad.hpp"
#include "workloads/lmm/xad.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(LmmParity, CppadMatchesXadAtTwoPointsFromOneTape)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    lmm::CppadGradient cppad(s, z);
    for (const lmm::Inputs<double>& in : {lmm_case::small_inputs(), lmm_case::shifted_inputs()}) {
        const std::vector<double> ref = lmm::xad_gradient(s, in, z);
        const std::vector<double> g = cppad(in);
        const harness::CheckResult r = harness::check(g, ref, 1e-8);
        EXPECT_TRUE(r.ok) << "L0[0] = " << in.L0[0] << ": worst component " << r.worst_index
                          << ", rel error " << r.worst_rel_error;
    }
}

// Repeated sweeps reuse CppAD's pool instead of going back to the system.
TEST(LmmCppad, SweepLeavesFreedMemoryInThePool)
{
    const lmm::Setup s = lmm_case::small_setup(16);
    lmm::CppadGradient cppad(s, lmm_case::draws(s, 42));
    cppad(lmm_case::small_inputs());
    EXPECT_GT(CppAD::thread_alloc::available(0), 0u);
}
