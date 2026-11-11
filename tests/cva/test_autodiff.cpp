// W3: autodiff gradient == XAD gradient (rtol 1e-8).
//
// Contract checked here (namespace cva, workloads/cva/autodiff.hpp):
//   std::vector<double> gradient_autodiff(const Inputs<double>&, const Config&,
//                       const std::vector<double>& z, double* value = nullptr);
//     - d CVA / d inputs in the flat layout: autodiff::var (reverse) with
//       autodiff::derivatives, or forward-mode dual passes (one per input) if
//       reverse is unsuitable at the timing size (deep expression graphs);
//       the file says which and why. *value (if given) receives the CVA.

#include "tests/cva/portfolio.hpp"
#include "workloads/cva/autodiff.hpp"
#include "workloads/cva/model.hpp"
#include "workloads/cva/xad.hpp"

#include "harness/check.hpp"
#include "harness/rng.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(CvaParity, AutodiffMatchesXad)
{
    const cva_test::Case s = cva_test::portfolio_setup(300);
    harness::Rng rng(23);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));

    double v_lib = 0.0, v_xad = 0.0;
    const std::vector<double> g_lib = cva::gradient_autodiff(s.inputs, s.config, z, &v_lib);
    const std::vector<double> g_xad = cva::gradient_xad(s.inputs, s.config, z, &v_xad);
    EXPECT_NEAR(v_lib, v_xad, 1e-12 * v_xad);
    const harness::CheckResult r = harness::check(g_lib, g_xad, 1e-8);
    EXPECT_TRUE(r.ok) << "worst input " << r.worst_index << ": autodiff " << g_lib[r.worst_index] << " xad "
                      << g_xad[r.worst_index] << " rel " << r.worst_rel_error;
}
