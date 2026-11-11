// W3 gradient tests: every input is live and every AD column agrees.
//
// Contract checked here (namespace cva, workloads/cva/):
//   model.hpp:
//     std::size_t n_inputs(const Config&);
//     std::vector<double> to_vector(const Inputs<double>&);
//     template <class Real> Inputs<Real> from_vector(const std::vector<Real>&, const Config&);
//       - flat layout: zero_rates, vols, correlations, hazard_rates, lgd, fixed_rates;
//         from_vector(to_vector(in), cfg) == in.
//   xad.hpp:
//     std::vector<double> gradient_xad(const Inputs<double>&, const Config&,
//                                      const std::vector<double>& z, double* value = nullptr);
//       - d cva / d inputs in the flat layout, by XAD adjoint mode (xad::adj<double>);
//         *value (if given) receives the CVA computed on the tape; one-shot form of
//     class XadGradient { XadGradient(const Config&, const Plan&, const std::vector<double>& z);
//                         std::vector<double> operator()(const Inputs<double>&, double* value = nullptr); };
//       - owns one tape, active for the object's lifetime and reused by every call;
//         no tape stays active after the object is gone.
//   fd.hpp:
//     std::vector<double> gradient_fd(const Inputs<double>&, const Config&,
//                                     const std::vector<double>& z, double h, double* value = nullptr);
//       - central bump-and-reprice of cva::cva<double> on the same shocks z, step h
//         (harness::fd_gradient over the flat layout); *value = the unbumped CVA.

#include "tests/cva/portfolio.hpp"
#include "workloads/cva/fd.hpp"
#include "workloads/cva/model.hpp"
#include "workloads/cva/xad.hpp"

#include "harness/check.hpp"
#include "harness/fd.hpp"
#include "harness/rng.hpp"

#include <XAD/XAD.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

// The old model had 9 of 40 inputs with a zero derivative. Every input here
// (curve pillars, vols, correlations through the Cholesky, hazard rates, LGD,
// fixed rates) must move the CVA.
TEST(CvaGradient, EveryInputIsLive)
{
    const cva_test::Case s = cva_test::portfolio_setup(500);
    harness::Rng rng(11);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));

    const std::vector<double> flat = cva::to_vector(s.inputs);
    ASSERT_EQ(flat.size(), cva::n_inputs(s.config));
    ASSERT_EQ(flat.size(), 8u + 3u + 3u + 3u + 1u + 4u);
    ASSERT_EQ(cva::to_vector(cva::from_vector(flat, s.config)), flat);

    const std::vector<double> g = cva::gradient_xad(s.inputs, s.config, z);
    ASSERT_EQ(g.size(), flat.size());
    for (std::size_t i = 0; i < g.size(); ++i) EXPECT_NE(g[i], 0.0) << "input " << i << " is dead";
}

// The old suite ran FD and AD on different seeds. The FD column reprices the
// same kernel on the same pre-generated shocks: its unbumped CVA is the AD
// primal bit for bit, and its gradient is exactly harness::fd_gradient of
// cva::cva on those shocks.
TEST(CvaGradient, FdUsesTheSameShocksAsAd)
{
    const cva_test::Case s = cva_test::portfolio_setup(200);
    harness::Rng rng(13);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));
    const double h = 1e-6;

    double value_xad = 0.0, value_fd = 0.0;
    cva::gradient_xad(s.inputs, s.config, z, &value_xad);
    const std::vector<double> g_fd = cva::gradient_fd(s.inputs, s.config, z, h, &value_fd);
    EXPECT_EQ(value_fd, value_xad);
    EXPECT_EQ(value_fd, cva::cva(s.inputs, s.config, z));

    const auto price = [&](const std::vector<double>& x) {
        return cva::cva(cva::from_vector(x, s.config), s.config, z);
    };
    const std::vector<double> reference = harness::fd_gradient(price, cva::to_vector(s.inputs), h);
    ASSERT_EQ(g_fd.size(), reference.size());
    for (std::size_t i = 0; i < g_fd.size(); ++i) EXPECT_EQ(g_fd[i], reference[i]) << "input " << i;

    // Other shocks give another answer, so the match above is not vacuous.
    harness::Rng other(14);
    const std::vector<double> z2 = other.normals(z.size());
    double value_other = 0.0;
    cva::gradient_fd(s.inputs, s.config, z2, h, &value_other);
    EXPECT_NE(value_other, value_fd);
}

// XAD against central finite differences on the same shocks, small size.
// Step h = 1e-7: the positive part max(V, 0) has a kink, and a bump that moves
// some path's portfolio value across zero breaks the O(h^2) FD error. Measured
// on this case (worst component, relative): h = 1e-5: 2.5e-1, h = 1e-6: 3.7e-4
// (10y zero rate), h = 1e-7: 5e-8, h = 1e-8: 9e-7 (round-off); with 2000 paths
// the h = 1e-6 error falls to 4.4e-5. So 1e-7 keeps clear of both kinks and
// round-off here.
TEST(CvaGradient, XadMatchesCentralFd)
{
    const cva_test::Case s = cva_test::portfolio_setup(200);
    harness::Rng rng(17);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));

    const std::vector<double> g_xad = cva::gradient_xad(s.inputs, s.config, z);
    const std::vector<double> g_fd = cva::gradient_fd(s.inputs, s.config, z, 1e-7);
    const harness::CheckResult r = harness::check(g_xad, g_fd, 1e-3);
    EXPECT_TRUE(r.ok) << "worst input " << r.worst_index << ": xad " << g_xad[r.worst_index] << " fd "
                      << g_fd[r.worst_index] << " rel " << r.worst_rel_error;
}

// A new tape per call costs fresh tape memory every gradient. As heston::XadGradient,
// the CVA gradient object owns one tape and reuses it: the active tape is the
// same one after each call while the object lives, and none is left active once
// it is gone (bench_cva runs other columns, with their own tapes, in the same
// process). Reuse leaves the results unchanged: both calls give the one-shot
// value and gradient bit for bit.
TEST(CvaGradient, XadGradientObjectReusesItsOwnTape)
{
    using tape_type = xad::adj<double>::tape_type;
    const cva_test::Case s = cva_test::portfolio_setup(50);
    harness::Rng rng(19);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));
    const cva::Plan pl = cva::make_plan(s.config);

    ASSERT_EQ(tape_type::getActive(), nullptr);
    double value_once = 0.0;
    const std::vector<double> g_once = cva::gradient_xad(s.inputs, s.config, pl, z, &value_once);

    double value_first = 0.0, value_second = 0.0;
    std::vector<double> g_first, g_second;
    {
        cva::XadGradient gradient(s.config, pl, z);
        g_first = gradient(s.inputs, &value_first);
        const tape_type* const tape_first = tape_type::getActive();
        g_second = gradient(s.inputs, &value_second);

        ASSERT_NE(tape_first, nullptr) << "no tape is active after the call: it was built for that call only";
        EXPECT_EQ(tape_type::getActive(), tape_first);
    }
    EXPECT_EQ(tape_type::getActive(), nullptr) << "a tape is still active after the gradient object is gone";

    EXPECT_EQ(value_first, value_once);
    EXPECT_EQ(value_second, value_once);
    EXPECT_EQ(g_first, g_once);
    EXPECT_EQ(g_second, g_once);
}
