// W1 test: the LMM kernel is arbitrage-free, checked from the model itself
// rather than against a reference that could share its mistakes.
//
// Under the spot measure the discounted bond prices are martingales:
//   E[disc * B_m] = P(0, T_{m+1}),   m = Nmat .. N-1,
// with disc = prod_{n<Nmat} 1 / (1 + delta L_n(T_n)) (the path discount of
// path_value), B_m = prod_{Nmat<=i<=m} 1 / (1 + delta L_i(T)) and
// P(0, T_k) = prod_{i<k} 1 / (1 + delta L0_i). After path_value, PathWork
// holds both: w.B[m] is B_m, and w.L[n] for n < Nmat is L_n(T_n), since rate n
// only moves in steps 0 .. n-1.
//
// Small case, 200000 paths, seed 42 (about 0.2 s). Measured with the same draws
// (Python port of path_value), relative error mean / P - 1 and its MC standard
// error:
//   drift as in Giles & Glasserman (delta * v * drift): m=8 -0.004 %, m=15
//     -0.010 %, SE 0.004 % .. 0.009 %, so within 1.0 SE (Euler bias is below
//     the noise);
//   drift without the delta (4x too large): m=8 -0.063 %, m=15 -0.309 %,
//     -15 .. -32 SE.
// Tolerance |mean / P - 1| < 0.1 % for every m: the correct model passes by
// about 10 SE, the 4x drift fails at m=15 by about 21 SE.

#include "tests/lmm/lmm_case.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

TEST(LmmMartingale, DiscountedBondsMatchInitialCurve)
{
    const lmm::Setup s = lmm_case::small_setup(200000);
    const std::vector<double> z = lmm_case::draws(s, 42);
    const lmm::Inputs<double> in = lmm_case::small_inputs();
    const int N = s.N;
    const int Nmat = s.Nmat;

    std::vector<double> P(N + 1, 1.0);  // P[k] = P(0, T_k)
    for (int i = 0; i < N; ++i) P[i + 1] = P[i] / (1.0 + s.delta * in.L0[i]);

    // x_m = disc * B_m / P(0, T_{m+1}) per path; sums for mean and SE.
    std::vector<double> sx(N, 0.0), sxx(N, 0.0);
    lmm::PathWork<double> w(N);
    for (std::size_t p = 0; p < s.paths; ++p) {
        lmm::path_value(s, in, z.data() + p * static_cast<std::size_t>(Nmat), w);
        double disc = 1.0;
        for (int n = 0; n < Nmat; ++n) disc /= 1.0 + s.delta * w.L[n];
        for (int m = Nmat; m < N; ++m) {
            const double x = disc * w.B[m] / P[m + 1];
            sx[m] += x;
            sxx[m] += x * x;
        }
    }

    const double n = static_cast<double>(s.paths);
    for (int m = Nmat; m < N; ++m) {
        const double mean = sx[m] / n;
        const double se = std::sqrt((sxx[m] / n - mean * mean) / n);
        EXPECT_NEAR(mean - 1.0, 0.0, 1e-3)
            << "m=" << m << ": E[disc * B_m] / P(0,T_" << m + 1 << ") - 1 = " << mean - 1.0
            << " (" << (mean - 1.0) / se << " SE, SE " << se << ")";
    }
}
