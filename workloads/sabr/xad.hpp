// W4 wiring for XAD: residual Jacobian by forward (tangent) mode.

#pragma once

#include "workloads/sabr/model.hpp"

#include <XAD/XAD.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sabr {

// Mode: forward, N tangent directions per pass (xad::fwd<double, N>), so
// ceil(n_params / N) passes of the plain kernel; no tape. Forward mode is
// the natural choice for this Jacobian (15 parameters, 100 residuals).
template <std::size_t N>
class XadFwdJacobian {
public:
    using mode = xad::fwd<double, N>;
    using AD = typename mode::active_type;

    explicit XadFwdJacobian(const Market& m) : m_(m), terms_(quote_terms(m)) {}

    // Row-major n_quotes x n_params Jacobian at x, J[i*n + j] = dr_i/dx_j.
    std::vector<double> operator()(const std::vector<double>& x) const
    {
        const std::size_t n = x.size();
        const std::size_t nq = m_.K.size();
        std::vector<double> J(nq * n);
        std::vector<AD> xa(n);
        for (std::size_t j0 = 0; j0 < n; j0 += N) {
            const std::size_t nd = std::min(N, n - j0);
            for (std::size_t j = 0; j < n; ++j) {
                xa[j] = x[j];
                for (std::size_t k = 0; k < N; ++k) tangent(xa[j], k) = (j == j0 + k) ? 1.0 : 0.0;
            }
            const std::vector<AD> r = residuals(m_, terms_, xa);
            for (std::size_t i = 0; i < nq; ++i)
                for (std::size_t k = 0; k < nd; ++k) J[i * n + j0 + k] = tangent(r[i], k);
        }
        return J;
    }

private:
    template <class A>
    static auto& tangent(A& a, std::size_t k)
    {
        if constexpr (N == 1) {
            (void)k;
            return xad::derivative(a);
        } else {
            return xad::derivative(a)[k];
        }
    }

    Market m_;
    std::vector<QuoteTerms> terms_;  // parameter-independent, computed once
};

// The XAD column: one pass with 16 directions (15 used for the standard
// market). In development runs at the standard market on one core it was as
// fast as 8 directions in 2 passes, and two to five times faster than 15
// directions (no clean SIMD width) or scalar forward mode in 15 passes.
using XadJacobian = XadFwdJacobian<16>;

}  // namespace sabr
