// W1 test 7: the Autodiff gradient equals the XAD gradient (rtol 1e-8).
//
// Contract (workloads/lmm/autodiff.hpp):
//   std::vector<double> lmm::autodiff_gradient(const lmm::Setup&,
//                                            const lmm::Inputs<double>&,
//                                            const std::vector<double>& z);
//   same kernel (lmm::price), layout as xad_gradient. Checked at two input
//   points.

#include "harness/check.hpp"
#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/autodiff.hpp"
#include "workloads/lmm/xad.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(LmmParity, AutodiffMatchesXad)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    for (const lmm::Inputs<double>& in : {lmm_case::small_inputs(), lmm_case::shifted_inputs()}) {
        const std::vector<double> ref = lmm::xad_gradient(s, in, z);
        const std::vector<double> g = lmm::autodiff_gradient(s, in, z);
        const harness::CheckResult r = harness::check(g, ref, 1e-8);
        EXPECT_TRUE(r.ok) << "L0[0] = " << in.L0[0] << ": worst component " << r.worst_index
                          << ", rel error " << r.worst_rel_error;
    }
}
