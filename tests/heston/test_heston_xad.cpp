// XAD gradient of the Heston book (workloads/heston/xad.hpp).

#include "tests/heston/heston_case.hpp"
#include "workloads/heston/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

// Every input moves the price: a zero component means the input was
// disconnected from the tape (for example converted to double inside the
// kernel) or never used.
TEST(HestonXad, EveryInputIsLive)
{
    const heston_case::Case c = heston_case::small();
    const std::vector<double> g = heston::xad_gradient(c.x, c.book, c.draws, c.dt);
    ASSERT_EQ(g.size(), heston_case::input_names.size());
    for (std::size_t i = 0; i < g.size(); ++i)
        EXPECT_NE(g[i], 0.0) << "d price / d " << heston_case::input_names[i] << " is zero";
}
