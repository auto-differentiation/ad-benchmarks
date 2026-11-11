// W4 wiring for autodiff: residual Jacobian by forward mode with dual numbers.

#pragma once

#include "workloads/sabr/model.hpp"

#include <autodiff/forward/dual.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace autodiff {
namespace detail {

// autodiff 1.1.2 has no log1p. First-order dual version, found by ADL from
// the kernel: value log1p(u), derivative u' / (1 + u).
inline Dual<double, double> log1p(const Dual<double, double>& u)
{
    Dual<double, double> r;
    r.val = std::log1p(u.val);
    r.grad = u.grad / (1.0 + u.val);
    return r;
}

}  // namespace detail
}  // namespace autodiff

namespace sabr {

// Mode: forward, autodiff::dual, one pass of the plain kernel per parameter
// with that parameter seeded (15 passes for the standard market).
class AutodiffJacobian {
public:
    using AD = autodiff::dual;

    explicit AutodiffJacobian(const Market& m) : m_(m), terms_(quote_terms(m)) {}

    // Row-major n_quotes x n_params Jacobian at x, J[i*n + j] = dr_i/dx_j.
    std::vector<double> operator()(const std::vector<double>& x) const
    {
        const std::size_t n = x.size();
        const std::size_t nq = m_.K.size();
        std::vector<double> J(nq * n);
        std::vector<AD> xa(x.begin(), x.end());
        for (std::size_t j = 0; j < n; ++j) {
            xa[j].grad = 1.0;
            const std::vector<AD> r = residuals(m_, terms_, xa);
            xa[j].grad = 0.0;
            for (std::size_t i = 0; i < nq; ++i) J[i * n + j] = r[i].grad;
        }
        return J;
    }

private:
    Market m_;
    std::vector<QuoteTerms> terms_;  // parameter-independent, computed once
};

}  // namespace sabr
