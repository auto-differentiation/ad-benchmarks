// W1 test 6: the Adept gradient equals the XAD gradient (rtol 1e-8).
//
// Contract (workloads/lmm/adept.hpp):
//   std::vector<double> lmm::adept_gradient(const lmm::Setup&,
//                                            const lmm::Inputs<double>&,
//                                            const std::vector<double>& z);
//   same kernel (lmm::price), layout as xad_gradient. Checked at two input
//   points.

#include "harness/check.hpp"
#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/adept.hpp"
#include "workloads/lmm/xad.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(LmmParity, AdeptMatchesXad)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    for (const lmm::Inputs<double>& in : {lmm_case::small_inputs(), lmm_case::shifted_inputs()}) {
        const std::vector<double> ref = lmm::xad_gradient(s, in, z);
        const std::vector<double> g = lmm::adept_gradient(s, in, z);
        const harness::CheckResult r = harness::check(g, ref, 1e-8);
        EXPECT_TRUE(r.ok) << "L0[0] = " << in.L0[0] << ": worst component " << r.worst_index
                          << ", rel error " << r.worst_rel_error;
    }
}

// The stack is built once and kept across calls, as the other tapes: building
// one on every call would time its setup, not Adept's gradient.
TEST(LmmAdept, StackOutlivesAGradientCall)
{
    const lmm::Setup s = lmm_case::small_setup(16);
    lmm::adept_gradient(s, lmm_case::small_inputs(), lmm_case::draws(s, 42));
    EXPECT_NE(adept::active_stack(), nullptr);
}
