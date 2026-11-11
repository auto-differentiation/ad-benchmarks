// H5: check(grad, ref, rtol) passes and fails correctly, and reports the
// worst component.
//
// Contract checked here:
//   harness::CheckResult r = harness::check(grad, ref, rtol, atol = 0);
//   - r.ok is true iff grad.size() == ref.size() and, for every i,
//       |grad[i] - ref[i]| <= atol + rtol * |ref[i]|
//     (so a NaN in grad fails: the comparison is false);
//   - rel error of component i = |grad[i] - ref[i]| / max(|ref[i]|, DBL_MIN);
//   - r.worst_index is the component with the largest rel error (a NaN
//     component counts as worst; ties go to the lowest index), and
//     r.worst_rel_error is its rel error;
//   - a size mismatch gives r.ok == false and must not read out of bounds.

#include "harness/check.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

TEST(Check, EqualVectorsPass)
{
    const std::vector<double> ref = {1.0, -2.5, 3e-4, 0.0};
    const harness::CheckResult r = harness::check(ref, ref, 1e-8);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.worst_rel_error, 0.0);
}

TEST(Check, SmallDifferenceWithinRtolPasses)
{
    const std::vector<double> ref = {1.0, 2.0, 3.0};
    const std::vector<double> grad = {1.0, 2.0 * (1.0 + 1e-10), 3.0};
    const harness::CheckResult r = harness::check(grad, ref, 1e-8);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.worst_index, 1u);
    EXPECT_NEAR(r.worst_rel_error, 1e-10, 1e-14);
}

TEST(Check, OneComponentOffFailsAndIsNamed)
{
    const std::vector<double> ref = {1.0, 2.0, 3.0};
    const std::vector<double> grad = {1.0, 2.0 * (1.0 + 1e-6), 3.0};
    const harness::CheckResult r = harness::check(grad, ref, 1e-8);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.worst_index, 1u);
    EXPECT_NEAR(r.worst_rel_error, 1e-6, 1e-12);
}

TEST(Check, SeveralOffNamesWorstByRelativeError)
{
    // Rel errors: 1e-3, 1e-2, 2e-2, 0. Component 1 has the largest absolute
    // error (1.0) but component 2 has the largest relative error.
    const std::vector<double> ref = {1.0, 100.0, 1e-3, 5.0};
    const std::vector<double> grad = {1.001, 101.0, 1.02e-3, 5.0};
    const harness::CheckResult r = harness::check(grad, ref, 1e-8);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.worst_index, 2u);
    EXPECT_NEAR(r.worst_rel_error, 2e-2, 1e-12);
}

TEST(Check, AtolAllowsSmallErrorAtZeroReference)
{
    const std::vector<double> ref = {0.0, 1.0};
    const std::vector<double> grad = {1e-12, 1.0};
    EXPECT_TRUE(harness::check(grad, ref, 1e-8, 1e-10).ok);
    EXPECT_FALSE(harness::check(grad, ref, 1e-8).ok);  // atol defaults to 0
}

TEST(Check, SizeMismatchFails)
{
    const std::vector<double> ref = {1.0, 2.0, 3.0};
    const std::vector<double> shorter = {1.0, 2.0};
    const std::vector<double> longer = {1.0, 2.0, 3.0, 4.0};
    EXPECT_FALSE(harness::check(shorter, ref, 1e-8).ok);
    EXPECT_FALSE(harness::check(longer, ref, 1e-8).ok);
}

TEST(Check, NanInGradFailsAndIsNamed)
{
    const std::vector<double> ref = {1.0, 2.0, 3.0};
    const std::vector<double> grad = {1.0 + 1e-3, 2.0, std::numeric_limits<double>::quiet_NaN()};
    const harness::CheckResult r = harness::check(grad, ref, 1e-2);  // component 0 is within rtol
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.worst_index, 2u);
}
