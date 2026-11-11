// W4 guard for the CppAD column: the tape is recorded once (in the
// constructor) and reused while LM moves the parameters, so any branch of the
// kernel that depends on the parameters would be frozen at the recording
// point. The tape must give XAD's Jacobian (rtol 1e-8) at points far from any
// plausible recording point, including:
//   - rho of the opposite sign and a large nu (large |z|),
//   - rho near -1 and a tiny nu (|z| near 0 on every strike),
// on a market that also has quotes exactly at F = K (the data branch, eq. 2.18).

#include "harness/check.hpp"
#include "workloads/sabr/cppad.hpp"
#include "workloads/sabr/model.hpp"
#include "workloads/sabr/xad.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {

// standard_market() plus one quote at K = F per expiry.
sabr::Market market_with_atm_quotes()
{
    sabr::Market m = sabr::standard_market();
    const std::vector<double> truth = sabr::standard_params();
    for (std::size_t e = 0; e < m.T.size(); ++e) {
        m.expiry.push_back(e);
        m.K.push_back(m.F[e]);
        m.quote.push_back(0.0);
    }
    const std::vector<double> r = sabr::residuals(m, truth);
    for (std::size_t i = m.quote.size() - m.T.size(); i < m.quote.size(); ++i) m.quote[i] = r[i];
    return m;
}

}  // namespace

TEST(SabrCppad, RecordedTapeHoldsFarFromTheRecordingPoint)
{
    const sabr::Market m = market_with_atm_quotes();
    const std::vector<double> truth = sabr::standard_params();
    const std::size_t n = truth.size();

    std::vector<double> big_z = truth, small_z = truth;
    for (std::size_t e = 0; e < n / 3; ++e) {
        big_z[3 * e] *= 0.5;
        big_z[3 * e + 1] = 0.6;
        big_z[3 * e + 2] *= 2.5;
        small_z[3 * e] *= 2.0;
        small_z[3 * e + 1] = -0.95;
        small_z[3 * e + 2] = 1e-4;
    }

    sabr::CppadJacobian cppad_jac(m);
    sabr::XadJacobian xad_jac(m);
    for (const std::vector<double>& x : {big_z, small_z, truth, big_z}) {
        const std::vector<double> J = cppad_jac(x);
        const std::vector<double> ref = xad_jac(x);
        ASSERT_EQ(J.size(), m.K.size() * n);
        const harness::CheckResult r = harness::check(J, ref, 1e-8);
        EXPECT_TRUE(r.ok) << "worst entry (quote " << r.worst_index / n << ", parameter "
                          << r.worst_index % n << "), rel error " << r.worst_rel_error
                          << " at rho_0 = " << x[1] << ", nu_0 = " << x[2];
    }
}
