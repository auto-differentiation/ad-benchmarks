// H1: with a fixed seed, Rng gives the same normals on every call.
// The normals are pre-generated into a vector, so drawing them is never
// part of a timed region: kernels only read from the returned buffer.

#include "harness/rng.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

TEST(Rng, SameSeedGivesSameNormals)
{
    const std::uint64_t seed = 42;
    const std::size_t n = 1000;

    harness::Rng a(seed);
    harness::Rng b(seed);
    const std::vector<double> za = a.normals(n);
    const std::vector<double> zb = b.normals(n);

    ASSERT_EQ(za.size(), n);
    ASSERT_EQ(zb.size(), n);
    EXPECT_EQ(za, zb);  // bit-for-bit identical
}

TEST(Rng, DifferentSeedsGiveDifferentNormals)
{
    harness::Rng a(1);
    harness::Rng b(2);
    EXPECT_NE(a.normals(100), b.normals(100));
}

TEST(Rng, NormalsHaveZeroMeanUnitVariance)
{
    const std::size_t n = 100000;
    harness::Rng rng(12345);
    const std::vector<double> z = rng.normals(n);
    ASSERT_EQ(z.size(), n);

    double sum = 0.0;
    for (double x : z) sum += x;
    const double mean = sum / static_cast<double>(n);

    double ss = 0.0;
    for (double x : z) ss += (x - mean) * (x - mean);
    const double var = ss / static_cast<double>(n - 1);

    // Standard error of the mean ~ 0.003, of the variance ~ 0.0045.
    EXPECT_NEAR(mean, 0.0, 0.02);
    EXPECT_NEAR(var, 1.0, 0.03);
    for (double x : z) ASSERT_TRUE(std::isfinite(x));
}
