// W1 test 1: the plain double price of the LMM swaption portfolio matches an
// independent reference.
//
// Reference: tests/reference/lmm_reference.py (pure Python, written from the
// Giles & Glasserman model description, regenerates harness::Rng draws
// exactly). Its drift is delta * v * drift (con1 = delta * lambda in Giles &
// Glasserman); an earlier version dropped the delta, as the kernel did, so the
// two agreed on a wrong price (tests/lmm/test_lmm_martingale.cpp catches that
// from the model itself). Output of the corrected script:
//   first normals: 1.2938204232729367 0.7049882664208599
//   small case: N=16 Nmat=8 seed=42 paths=4096 price=11.550569978279944
// Same draws and the same operations in the same order, so only libm
// rounding can differ: rtol 1e-12.

#include "tests/lmm/lmm_case.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

TEST(LmmPrice, DrawsAreTheReferenceDraws)
{
    const std::vector<double> z = lmm_case::draws(lmm_case::small_setup());
    EXPECT_DOUBLE_EQ(z[0], 1.2938204232729367);
    EXPECT_DOUBLE_EQ(z[1], 0.7049882664208599);
}

TEST(LmmPrice, SmallCaseMatchesPythonReference)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    const double v = lmm::price<double>(s, lmm_case::small_inputs(), z);
    const double ref = 11.550569978279944;
    EXPECT_NEAR(v, ref, 1e-12 * ref);
}
