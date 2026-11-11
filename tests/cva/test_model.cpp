// W3 model tests: CVA on a swap portfolio, correlated multi-factor Gaussian
// short-rate model (workloads/cva/model.hpp).
//
// Contract checked here (namespace cva):
//   struct Config {                       // not differentiated
//       std::vector<double> mean_reversions;   // a_i > 0, one per factor
//       std::vector<double> dates;             // exposure dates t_1 < ... < t_K, t_0 = 0
//       std::size_t n_paths;
//       ...                                    // curve pillars, swaps, hazard pillars: later tests
//   };
//   template <class Real> struct Inputs {  // differentiated
//       std::vector<Real> vols;                // sigma_i, one per factor
//       std::vector<Real> correlations;        // strict lower triangle of rho, row by row:
//                                              // (1,0), (2,0), (2,1), ...
//       ...                                    // curve, hazards, LGD, fixed rates: later tests
//   };
//   std::size_t n_normals(const Config&);  // n_paths * K * n_factors
//   std::vector<Real> simulate_factors(const Inputs<Real>&, const Config&,
//                                      const std::vector<double>& z);
//     - z: pre-generated N(0,1) draws, z[(p * K + k) * F + i] is the shock of
//       path p, step k (t_k -> t_{k+1}), factor i;
//     - returns x[(p * (K + 1) + k) * F + i], the factor x_i(t_k) of path p,
//       k = 0..K, with x(t_0) = 0;
//     - each factor is an Ornstein-Uhlenbeck process stepped exactly:
//       x_i(t_{k+1}) = e^{-a_i dt} x_i(t_k) + (deterministic drift)
//                      + sigma_i sqrt((1 - e^{-2 a_i dt}) / (2 a_i)) * (correlated shock)_i,
//       with dt = t_{k+1} - t_k. The drift may depend on the measure the model
//       simulates under, but not on z.
//
//   Config also has (test 2):
//       std::vector<double> curve_times;       // pillars of the initial zero curve
//       std::vector<Swap> swaps;               // Swap{notional, start, maturity, period, payer}:
//                                              // fixed and float legs with the same period,
//                                              // accrual = period, single curve, payer pays fixed
//   Inputs also has (test 2):
//       std::vector<Real> zero_rates;          // continuously compounded, at curve_times
//       std::vector<Real> fixed_rates;         // one per swap
//   std::vector<Real> expected_exposure(const Inputs<Real>&, const Config&, const std::vector<double>& z);
//     - EE+(t_k), k = 1..K, of the portfolio in t_k money: E^Q[D(0, t_k) V(t_k)^+] / P(0, t_k),
//       the Monte Carlo average over n_paths paths.
//
//   Config also has (test 3):
//       std::vector<double> hazard_times;      // right ends of the hazard intervals
//   Inputs also has (test 3):
//       std::vector<Real> hazard_rates;        // hazard_rates[j] on (hazard_times[j-1], hazard_times[j]],
//                                              // hazard_times[-1] = 0, the last extended flat
//       Real lgd;
//   Real cva(const Inputs<Real>&, const Config&, const std::vector<double>& z);
//     - LGD * sum_k P(0, t_k) EE+(t_k) PD(t_{k-1}, t_k), PD(u, v) = S(u) - S(v), S(t) = exp(-int_0^t h).

#include "workloads/cva/model.hpp"

#include "harness/rng.hpp"
#include "tests/cva/portfolio.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

// Three uncorrelated factors on an uneven grid, two paths.
cva_test::Case uncorrelated_setup()
{
    cva_test::Case s;
    s.config.mean_reversions = {0.05, 0.3, 1.2};
    s.config.dates = {0.25, 0.5, 1.0, 2.0};
    s.config.n_paths = 2;
    s.inputs.vols = {0.010, 0.008, 0.006};
    s.inputs.correlations = {0.0, 0.0, 0.0};
    return s;
}

}  // namespace

// The old model reused the same shocks at every step. Here the stochastic part
// of each step must be the step's own pre-generated normal, scaled by the exact
// OU standard deviation. Running once with z and once with all-zero shocks
// removes any deterministic drift: y = x(z) - x(0) obeys
//   y_i(t_{k+1}) - e^{-a_i dt} y_i(t_k) = sigma_i s_i(dt) z[(p K + k) F + i].
TEST(CvaModel, FreshShocksEveryStep)
{
    const cva_test::Case s = uncorrelated_setup();
    const std::size_t F = 3, K = s.config.dates.size(), P = s.config.n_paths;
    ASSERT_EQ(cva::n_normals(s.config), P * K * F);

    // Distinct, easily recognised shocks: z = 0.1 * (index + 1), sign alternating.
    std::vector<double> z(P * K * F);
    for (std::size_t n = 0; n < z.size(); ++n) z[n] = (n % 2 ? -0.1 : 0.1) * double(n + 1);
    const std::vector<double> zero(z.size(), 0.0);

    const std::vector<double> x = cva::simulate_factors(s.inputs, s.config, z);
    const std::vector<double> x0 = cva::simulate_factors(s.inputs, s.config, zero);
    ASSERT_EQ(x.size(), P * (K + 1) * F);
    ASSERT_EQ(x0.size(), x.size());

    for (std::size_t p = 0; p < P; ++p) {
        std::vector<double> prev_increment(F, 0.0);
        for (std::size_t k = 0; k < K; ++k) {
            const double t0 = k == 0 ? 0.0 : s.config.dates[k - 1];
            const double dt = s.config.dates[k] - t0;
            for (std::size_t i = 0; i < F; ++i) {
                const double a = s.config.mean_reversions[i];
                const double sd = s.inputs.vols[i] * std::sqrt((1.0 - std::exp(-2.0 * a * dt)) / (2.0 * a));
                const auto at = [&](std::size_t kk) { return (p * (K + 1) + kk) * F + i; };
                const double y0 = x[at(k)] - x0[at(k)];
                const double y1 = x[at(k + 1)] - x0[at(k + 1)];
                const double increment = y1 - std::exp(-a * dt) * y0;
                const double expected = sd * z[(p * K + k) * F + i];
                EXPECT_NEAR(increment, expected, 1e-14 + 1e-12 * std::fabs(expected))
                    << "path " << p << " step " << k << " factor " << i;
                if (k > 0)
                    EXPECT_NE(increment, prev_increment[i])
                        << "path " << p << " step " << k << " factor " << i << " reuses the previous shock";
                prev_increment[i] = increment;
            }
        }
        for (std::size_t i = 0; i < F; ++i)
            EXPECT_EQ(x[(p * (K + 1)) * F + i], 0.0) << "x(t_0) must be 0, path " << p << " factor " << i;
    }
}

namespace {

double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

// Zero-coupon bond put in the F-factor Gaussian model (G2++ generalised,
// Brigo & Mercurio, Interest Rate Models, sec. 4.2): option expiry t on
// P(t, T) with strike X, priced at time 0.
double zcb_put(double p0t, double p0T, double t, double T, double X, const std::vector<double>& a,
               const std::vector<double>& sigma, const std::vector<std::vector<double>>& rho)
{
    const std::size_t F = a.size();
    double var = 0.0;
    for (std::size_t i = 0; i < F; ++i)
        for (std::size_t j = 0; j < F; ++j) {
            const double Bi = (1.0 - std::exp(-a[i] * (T - t))) / a[i];
            const double Bj = (1.0 - std::exp(-a[j] * (T - t))) / a[j];
            var += rho[i][j] * sigma[i] * sigma[j] * Bi * Bj * (1.0 - std::exp(-(a[i] + a[j]) * t)) / (a[i] + a[j]);
        }
    const double s = std::sqrt(var);
    const double h = std::log(p0T / (X * p0t)) / s + 0.5 * s;
    return X * p0t * norm_cdf(-h + s) - p0T * norm_cdf(-h);
}

}  // namespace

// The old model discounted the exposure twice. EE+(t_k) is the expected
// positive exposure in t_k money, E^Q[D(0, t_k) V(t_k)^+] / P(0, t_k), so that
// CVA = LGD * sum_k P(0, t_k) EE+(t_k) PD_k discounts exactly once.
// One payer swap starting at S = 4 with one annual period to T = 5: at its
// start V(S) = N (1 - c P(S, T)), c = 1 + K, so
//   P(0, S) EE+(S) = N c ZBP(0, S, T, 1 / c)   (closed form, correlated factors).
// Double discounting would be off by P(0, 4) ~ 0.89; MC error here ~0.5%.
TEST(CvaModel, ExpectedExposureDiscountsFromT)
{
    cva::Config config;
    config.mean_reversions = {0.05, 0.3, 1.0};
    for (int q = 1; q <= 16; ++q) config.dates.push_back(0.25 * q);
    config.n_paths = 100000;
    config.curve_times = {0.5, 1.0, 2.0, 4.0, 5.0, 7.0};
    cva::Swap swap;
    swap.notional = 100.0;
    swap.start = 4.0;
    swap.maturity = 5.0;
    swap.period = 1.0;
    swap.payer = true;
    config.swaps = {swap};

    cva::Inputs<double> inputs;
    inputs.zero_rates = {0.020, 0.022, 0.025, 0.029, 0.030, 0.031};
    inputs.vols = {0.010, 0.008, 0.006};
    inputs.correlations = {-0.6, 0.3, -0.2};
    const double S = 4.0, T = 5.0;
    const double p0S = std::exp(-0.029 * S), p0T = std::exp(-0.030 * T);
    const double strike = p0S / p0T - 1.0;  // at the money forward
    inputs.fixed_rates = {strike};

    harness::Rng rng(20261028);
    const std::vector<double> z = rng.normals(cva::n_normals(config));
    const std::vector<double> ee = cva::expected_exposure(inputs, config, z);
    ASSERT_EQ(ee.size(), config.dates.size());

    const std::vector<std::vector<double>> rho = {{1.0, -0.6, 0.3}, {-0.6, 1.0, -0.2}, {0.3, -0.2, 1.0}};
    const double c = 1.0 + strike;
    const double reference = swap.notional * c *
                             zcb_put(p0S, p0T, S, T, 1.0 / c, config.mean_reversions, inputs.vols, rho) / p0S;
    const double ee_start = ee.back();  // t_16 = 4 = S
    EXPECT_NEAR(ee_start, reference, 0.02 * reference)
        << "ratio " << ee_start / reference << " (double discounting gives ~" << p0S << ")";
}

// The old model left LGD out. CVA = LGD * sum_k P(0, t_k) EE+(t_k) PD(t_{k-1}, t_k)
// with PD(u, v) = exp(-int_0^u h) - exp(-int_0^v h) for the piecewise-constant
// hazard rate h (hazard_rates[j] on (hazard_times[j-1], hazard_times[j]], the
// last one extended flat). So CVA is LGD times a number that does not depend
// on LGD, and LGD = 0 gives CVA = 0 exactly.
TEST(CvaModel, CvaIsLinearInLgd)
{
    cva_test::Case s = cva_test::portfolio_setup(2000);
    harness::Rng rng(7);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));

    // The defining sum, rebuilt from EE+ (dates sit on curve and hazard pillars).
    const std::vector<double> ee = cva::expected_exposure(s.inputs, s.config, z);
    ASSERT_EQ(ee.size(), s.config.dates.size());
    const auto cum_hazard = [&](double t) {  // pillars 1, 3, 6 and t on the grid
        const double h0 = s.inputs.hazard_rates[0], h1 = s.inputs.hazard_rates[1], h2 = s.inputs.hazard_rates[2];
        return h0 * std::min(t, 1.0) + h1 * std::max(0.0, std::min(t, 3.0) - 1.0) + h2 * std::max(0.0, t - 3.0);
    };
    double sum = 0.0;
    for (std::size_t k = 0; k < ee.size(); ++k) {
        const double t = s.config.dates[k], u = k == 0 ? 0.0 : s.config.dates[k - 1];
        const double df = std::exp(-s.inputs.zero_rates[k] * t);  // pillar k is t_k
        const double pd = std::exp(-cum_hazard(u)) - std::exp(-cum_hazard(t));
        sum += df * ee[k] * pd;
    }
    ASSERT_GT(sum, 0.0);

    for (double lgd : {0.6, 0.4, 1.0}) {
        s.inputs.lgd = lgd;
        const double value = cva::cva(s.inputs, s.config, z);
        EXPECT_NEAR(value, lgd * sum, 1e-12 * sum) << "LGD " << lgd;
    }
    s.inputs.lgd = 0.0;
    EXPECT_EQ(cva::cva(s.inputs, s.config, z), 0.0);
}
