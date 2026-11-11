// W4 test 9: one timed unit = one Jacobian, and LM iterations are dependent:
// iteration k+1 evaluates the Jacobian at the parameters accepted at k.
// (The previous suite timed 4 dependent optimiser iterations as one batched
// unit, which no real calibration can do.)
//
// Checked with an instrumented Jacobian provider wrapped around XadJacobian:
//   - the provider is called exactly once per LM iteration (calls == iterations),
//     one parameter vector per call;
//   - call 0 sees x0; every later call sees a new vector (never one seen
//     before) whose cost is strictly lower than at the previous call, i.e. the
//     vector LM accepted after the previous Jacobian;
//   - the returned x is the last accepted vector: its cost is at most the
//     cost at the last call.

#include "workloads/sabr/lm.hpp"
#include "workloads/sabr/model.hpp"
#include "workloads/sabr/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {

double cost(const sabr::Market& m, const std::vector<double>& x)
{
    double c = 0.0;
    for (double r : sabr::residuals(m, x)) c += r * r;
    return 0.5 * c;
}

struct RecordingJacobian {
    sabr::XadJacobian inner;
    std::vector<std::vector<double>>* seen;
    std::vector<double> operator()(const std::vector<double>& x)
    {
        seen->push_back(x);
        return inner(x);
    }
};

}  // namespace

TEST(SabrLm, EachIterationEvaluatesOneJacobianAtTheUpdatedParameters)
{
    const sabr::Market m = sabr::standard_market();
    std::vector<double> x0 = sabr::standard_params();
    for (std::size_t e = 0; e < x0.size() / 3; ++e) {
        x0[3 * e] *= 1.3;
        x0[3 * e + 1] = 0.5 * x0[3 * e + 1] - 0.1;
        x0[3 * e + 2] *= 0.7;
    }

    std::vector<std::vector<double>> seen;
    RecordingJacobian jac{sabr::XadJacobian(m), &seen};
    const sabr::LmResult res = sabr::levenberg_marquardt(m, x0, jac);

    ASSERT_TRUE(res.converged);
    ASSERT_GE(seen.size(), 3u);
    EXPECT_EQ(seen.size(), res.iterations);
    EXPECT_EQ(seen.front(), x0);
    for (std::size_t k = 1; k < seen.size(); ++k) {
        for (std::size_t j = 0; j < k; ++j)
            EXPECT_NE(seen[k], seen[j]) << "call " << k << " repeats call " << j;
        EXPECT_LT(cost(m, seen[k]), cost(m, seen[k - 1])) << "call " << k;
    }
    EXPECT_LE(res.cost, cost(m, seen.back()));
    EXPECT_DOUBLE_EQ(res.cost, cost(m, res.x));
}
