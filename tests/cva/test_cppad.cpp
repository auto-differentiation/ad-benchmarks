// W3: CppAD gradient == XAD gradient (rtol 1e-8).
//
// Contract checked here (namespace cva, workloads/cva/cppad.hpp):
//   class CppadCva {
//   public:
//       // Records the kernel once: the flat inputs are the independent
//       // variables, the shocks z are dynamic parameters; the tape is optimized.
//       CppadCva(const Inputs<double>& in, const Config& c, const std::vector<double>& z);
//       // Gradient (flat layout) and, if asked, CVA at new inputs and shocks of
//       // the same sizes, from the recorded tape: new_dynamic(z), Forward(0, x),
//       // Reverse(1, w). Never re-records.
//       std::vector<double> gradient(const Inputs<double>& in, const std::vector<double>& z,
//                                    double* value = nullptr);
//   };
//   The positive part of the portfolio value must be recorded as a
//   conditional expression, so the tape stays right where paths change sign.

#include "tests/cva/portfolio.hpp"
#include "workloads/cva/cppad.hpp"
#include "workloads/cva/model.hpp"
#include "workloads/cva/xad.hpp"

#include "harness/check.hpp"
#include "harness/rng.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

// Recorded at one point (inputs a, shocks z1), evaluated at a clearly different
// one (inputs b: curve, vols, a correlation, a fixed rate, hazards and LGD all
// moved; shocks z2), where portfolio values change sign on some paths: a
// frozen branch or re-recording per call would show up here.
TEST(CvaParity, CppadMatchesXad)
{
    const cva_test::Case a = cva_test::portfolio_setup(300);
    cva_test::Case b = a;
    for (double& r : b.inputs.zero_rates) r += 0.004;
    for (double& v : b.inputs.vols) v *= 1.3;
    b.inputs.fixed_rates[1] -= 0.003;
    b.inputs.correlations[0] = -0.4;
    for (double& h : b.inputs.hazard_rates) h *= 1.5;
    b.inputs.lgd = 0.45;

    harness::Rng rng(19);
    const std::vector<double> z1 = rng.normals(cva::n_normals(a.config));
    const std::vector<double> z2 = rng.normals(z1.size());

    cva::CppadCva cppad(a.inputs, a.config, z1);
    for (const auto& [in, z] : {std::pair{a.inputs, z1}, std::pair{b.inputs, z2}}) {
        double v_cppad = 0.0, v_xad = 0.0;
        const std::vector<double> g_cppad = cppad.gradient(in, z, &v_cppad);
        const std::vector<double> g_xad = cva::gradient_xad(in, a.config, z, &v_xad);
        EXPECT_NEAR(v_cppad, v_xad, 1e-12 * v_xad);
        const harness::CheckResult r = harness::check(g_cppad, g_xad, 1e-8);
        EXPECT_TRUE(r.ok) << "worst input " << r.worst_index << ": cppad " << g_cppad[r.worst_index]
                          << " xad " << g_xad[r.worst_index] << " rel " << r.worst_rel_error;
    }
}

// Repeated sweeps reuse CppAD's pool instead of going back to the system.
TEST(CvaCppad, SweepLeavesFreedMemoryInThePool)
{
    const cva_test::Case s = cva_test::portfolio_setup(10);
    harness::Rng rng(19);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));
    cva::CppadCva cppad(s.inputs, s.config, z);
    cppad.gradient(s.inputs, z);
    EXPECT_GT(CppAD::thread_alloc::available(0), 0u);
}
