// W1 test 2: every input is live. The XAD gradient of the small-case price
// has no zero component, so no L0[i] or lambda[k] is dropped by the kernel.
//
// Contract (workloads/lmm/xad.hpp):
//   std::vector<double> lmm::xad_gradient(const lmm::Setup&,
//                                         const lmm::Inputs<double>&,
//                                         const std::vector<double>& z);
//   layout [dV/dL0[0..N-1], dV/dlambda[0..N-2]], size 2N - 1; one-shot form of
//   class lmm::XadGradient { XadGradient(const lmm::Setup&, const std::vector<double>& z);
//                            std::vector<double> operator()(const lmm::Inputs<double>&); };
//     owns one tape, active for the object's lifetime and reused by every call;
//     no tape stays active after the object is gone.
//
// Also here: the gradient object reuses its own tape and gives the one-shot result.

#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/xad.hpp"

#include <XAD/XAD.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

TEST(LmmLive, XadGradientHasNoZeroComponent)
{
    const lmm::Setup s = lmm_case::small_setup(4096);
    const std::vector<double> z = lmm_case::draws(s, 42);
    const std::vector<double> g = lmm::xad_gradient(s, lmm_case::small_inputs(), z);
    ASSERT_EQ(g.size(), static_cast<std::size_t>(2 * s.N - 1));
    for (std::size_t i = 0; i < g.size(); ++i) {
        EXPECT_NE(g[i], 0.0) << (i < static_cast<std::size_t>(s.N) ? "dV/dL0[" : "dV/dlambda[")
                             << (i < static_cast<std::size_t>(s.N) ? i : i - s.N) << "] is zero";
    }
}

// A new tape per call costs fresh tape memory every gradient. As heston and
// cva::XadGradient, the LMM gradient object owns one tape and reuses it: the
// active tape is the same one after each call while the object lives, and
// none is left active once it is gone (the bench runs other columns, with
// their own tapes, in the same process). Both calls give the one-shot
// gradient bit for bit.
TEST(LmmXad, GradientObjectReusesItsOwnTape)
{
    using tape_type = xad::adj<double>::tape_type;
    const lmm::Setup s = lmm_case::small_setup(512);
    const std::vector<double> z = lmm_case::draws(s, 42);
    const lmm::Inputs<double> in = lmm_case::small_inputs();

    ASSERT_EQ(tape_type::getActive(), nullptr);
    const std::vector<double> g_once = lmm::xad_gradient(s, in, z);

    std::vector<double> g_first, g_second;
    {
        lmm::XadGradient gradient(s, z);
        g_first = gradient(in);
        const tape_type* const tape_first = tape_type::getActive();
        g_second = gradient(in);

        ASSERT_NE(tape_first, nullptr) << "no tape is active after the call: it was built for that call only";
        EXPECT_EQ(tape_type::getActive(), tape_first);
    }
    EXPECT_EQ(tape_type::getActive(), nullptr) << "a tape is still active after the gradient object is gone";

    EXPECT_EQ(g_first, g_once);
    EXPECT_EQ(g_second, g_once);
}
