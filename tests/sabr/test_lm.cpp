// W4 test 2: Levenberg-Marquardt with the XAD residual Jacobian recovers known
// SABR parameters from synthetic noise-free quotes, to 1e-6.
//
// Contract checked here:
//   workloads/sabr/model.hpp
//     struct sabr::Market {                 // fixed data, nothing calibrated
//       double beta;                        // same for every expiry
//       std::vector<double> T, F;           // per expiry: time, forward
//       std::vector<std::size_t> expiry;    // per quote: index into T, F
//       std::vector<double> K, quote;       // per quote: strike, market vol
//     };
//     sabr::Market sabr::standard_market();       // 5 expiries x 20 strikes =
//         // 100 quotes, quote[i] = hagan_vol at standard_params() (no noise)
//     std::vector<double> sabr::standard_params(); // 15 = {alpha, rho, nu} per
//         // expiry, expiry-major: x[3e] = alpha_e, x[3e+1] = rho_e, x[3e+2] = nu_e
//     template <class Real> std::vector<Real>
//       sabr::residuals(const Market&, const std::vector<Real>& x);
//         // r[i] = hagan_vol(F[e], K[i], T[e], alpha_e, beta, rho_e, nu_e) - quote[i],
//         // e = expiry[i]; the one kernel used by plain double and every AD column
//   workloads/sabr/xad.hpp
//     sabr::XadJacobian jac(market);   // set up once
//     std::vector<double> J = jac(x);  // row-major n_quotes x n_params, J[i*n+j] = dr_i/dx_j
//   workloads/sabr/lm.hpp
//     struct sabr::LmOptions { std::size_t max_iterations = 100; ... };
//     struct sabr::LmResult { std::vector<double> x; std::size_t iterations; bool converged; double cost; };
//     template <class Jacobian> sabr::LmResult
//       sabr::levenberg_marquardt(const Market&, std::vector<double> x0, Jacobian&& jac,
//                                 const LmOptions& = {});
//         // jac: any callable x -> row-major Jacobian as above; residuals in
//         // plain double via sabr::residuals<double>; cost = 0.5 * |r|^2.

#include "workloads/sabr/lm.hpp"
#include "workloads/sabr/model.hpp"
#include "workloads/sabr/xad.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

TEST(SabrLm, RecoversKnownParametersWithXadJacobian)
{
    const sabr::Market m = sabr::standard_market();
    const std::vector<double> truth = sabr::standard_params();
    ASSERT_EQ(m.T.size(), 5u);
    ASSERT_EQ(m.F.size(), 5u);
    ASSERT_EQ(m.K.size(), 100u);
    ASSERT_EQ(m.quote.size(), 100u);
    ASSERT_EQ(m.expiry.size(), 100u);
    ASSERT_EQ(truth.size(), 15u);

    // Start away from the truth on every parameter, inside the domain.
    std::vector<double> x0 = truth;
    for (std::size_t e = 0; e < 5; ++e) {
        x0[3 * e] *= 1.3;                   // alpha
        x0[3 * e + 1] = 0.5 * x0[3 * e + 1] - 0.1;  // rho
        x0[3 * e + 2] *= 0.7;               // nu
    }

    sabr::XadJacobian jac(m);
    const sabr::LmResult res = sabr::levenberg_marquardt(m, x0, jac);

    EXPECT_TRUE(res.converged) << "after " << res.iterations << " iterations, cost " << res.cost;
    EXPECT_GT(res.iterations, 1u);
    EXPECT_LT(res.iterations, sabr::LmOptions{}.max_iterations);
    EXPECT_LT(res.cost, 1e-20);
    ASSERT_EQ(res.x.size(), truth.size());
    for (std::size_t j = 0; j < truth.size(); ++j)
        EXPECT_NEAR(res.x[j], truth[j], 1e-6) << "parameter " << j;
}
