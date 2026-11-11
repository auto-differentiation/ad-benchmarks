// W4 test 1: the Hagan et al. (2002) lognormal SABR implied-vol approximation
// matches independent reference vols in plain double: ATM, wings, near ATM and
// exactly at F = K.
//
// Contract checked here (workloads/sabr/model.hpp):
//   template <class Real>
//   Real sabr::hagan_vol(double F, double K, double T,
//                        const Real& alpha, double beta, const Real& rho, const Real& nu);
//   - Black (lognormal) implied vol of Hagan et al., "Managing Smile Risk",
//     Wilmott Magazine (2002), eq. (2.17a); at F = K it is eq. (2.18), the
//     limit of (2.17a), so K == F gives a finite value (no 0/0);
//   - written once as a template on the number type (alpha, rho, nu are the
//     calibrated parameters; F, K, T and beta are fixed data);
//   - accurate near the F = K limit as well: |ln(F/K)| ~ 1e-7 must not lose
//     digits to cancellation in z / x(z).
//
// Reference values: tests/sabr/hagan_reference.py, an independent
// implementation of the same equations in 50-digit decimal arithmetic.

#include "workloads/sabr/model.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace {

struct Case {
    double F, K, T, alpha, beta, rho, nu, vol;
};

// Output of tests/sabr/hagan_reference.py.
const Case kCases[] = {
    {0.04, 0.01, 2.0, 0.04, 0.5, -0.3, 0.5, 0.4742904595608637},
    {0.04, 0.02, 2.0, 0.04, 0.5, -0.3, 0.5, 0.33417287696524656},
    {0.04, 0.03, 2.0, 0.04, 0.5, -0.3, 0.5, 0.25356503791145407},
    {0.04, 0.04, 2.0, 0.04, 0.5, -0.3, 0.5, 0.205875},
    {0.04, 0.05, 2.0, 0.04, 0.5, -0.3, 0.5, 0.18858187573217158},
    {0.04, 0.08, 2.0, 0.04, 0.5, -0.3, 0.5, 0.21124202236910947},
    {0.04, 0.12, 2.0, 0.04, 0.5, -0.3, 0.5, 0.24652408375565257},
    {0.04, 0.040000004000000006, 2.0, 0.04, 0.5, -0.3, 0.5, 0.2058749871619817},
    {0.04, 0.039999996, 2.0, 0.04, 0.5, -0.3, 0.5, 0.20587501283802337},
    {100.0, 50.0, 1.0, 0.25, 1.0, 0.4, 0.8, 0.3161811036966367},
    {100.0, 80.0, 1.0, 0.25, 1.0, 0.4, 0.8, 0.24912578564042995},
    {100.0, 100.0, 1.0, 0.25, 1.0, 0.4, 0.8, 0.26513333333333333},
    {100.0, 120.0, 1.0, 0.25, 1.0, 0.4, 0.8, 0.30424981065733514},
    {100.0, 200.0, 1.0, 0.25, 1.0, 0.4, 0.8, 0.43612499166666346},
    {0.03, 0.005, 5.0, 0.006, 0.0, 0.1, 0.3, 0.5216094397812995},
    {0.03, 0.03, 5.0, 0.006, 0.0, 0.1, 0.3, 0.20905416666666668},
    {0.03, 0.06, 5.0, 0.006, 0.0, 0.1, 0.3, 0.18663226473636937},
};

}  // namespace

TEST(SabrHagan, MatchesIndependentReferenceVols)
{
    // A few ulps of pow/log/sqrt; a plain evaluation of z / x(z) loses
    // about 6.5e-11 at the two near-ATM strikes and gives 0/0 at F = K.
    const double rtol = 1e-12;
    for (const Case& c : kCases) {
        const double v = sabr::hagan_vol<double>(c.F, c.K, c.T, c.alpha, c.beta, c.rho, c.nu);
        EXPECT_TRUE(std::isfinite(v)) << "F=" << c.F << " K=" << c.K;
        EXPECT_NEAR(v, c.vol, rtol * c.vol)
            << "F=" << c.F << " K=" << c.K << " T=" << c.T << " beta=" << c.beta
            << " rel error " << std::fabs(v / c.vol - 1.0);
    }
}
