// Heston model checks on the plain double kernel (workloads/heston/model.hpp).

#include "workloads/heston/model.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

double norm_cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

double black_scholes_call(double s, double k, double t, double r, double sigma)
{
    const double sd = sigma * std::sqrt(t);
    const double d1 = (std::log(s / k) + (r + 0.5 * sigma * sigma) * t) / sd;
    const double d2 = d1 - sd;
    return s * norm_cdf(d1) - k * std::exp(-r * t) * norm_cdf(d2);
}

}  // namespace

// With vol of vol ~ 0 and v0 = theta the variance stays at theta on every
// path, so Heston is Black-Scholes with sigma = sqrt(theta). The full-
// truncation scheme evolves log S, which is exact for constant variance, so
// the only difference left is Monte Carlo noise. The standard error is taken
// from 20 independent batches (seeds 1..20), each priced by the kernel.
TEST(HestonModel, VanishingVolOfVolMatchesBlackScholes)
{
    const heston::Params<double> p{/*s0=*/100.0, /*v0=*/0.04, /*kappa=*/1.5, /*theta=*/0.04,
                                   /*xi=*/1e-8, /*rho=*/-0.7, /*r=*/0.03};
    const std::vector<heston::Option> book{{heston::Kind::Call, /*strike=*/100.0, /*maturity=*/1.0}};
    const double dt = 0.01;
    const std::size_t steps = 100;
    const std::size_t batches = 20, paths = 2500;

    std::vector<double> means;
    for (std::size_t b = 0; b < batches; ++b) {
        const heston::Draws d = heston::make_draws(paths, steps, /*seed=*/b + 1);
        means.push_back(heston::price(p, book, d, dt));
    }
    double mean = 0.0;
    for (double m : means) mean += m;
    mean /= batches;
    double var = 0.0;
    for (double m : means) var += (m - mean) * (m - mean);
    var /= batches - 1;
    const double se = std::sqrt(var / batches);

    const double bs = black_scholes_call(100.0, 100.0, 1.0, 0.03, std::sqrt(0.04));
    ASSERT_GT(se, 0.0);
    ASSERT_LT(se, 0.1);  // 50k paths: about 14.6 / sqrt(50000) = 0.065
    EXPECT_NEAR(mean, bs, 3.0 * se) << "MC " << mean << " +- " << se << ", BS " << bs;
}
