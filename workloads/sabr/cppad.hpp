// W4 wiring for CppAD: residuals recorded once into an optimized ADFun.

#pragma once

#include "workloads/cppad_hold_memory.hpp"
#include "workloads/sabr/model.hpp"

#include <cppad/cppad.hpp>

#include <cstddef>
#include <vector>

namespace sabr {

class CppadJacobian {
public:
    using AD = CppAD::AD<double>;

    // Records the residuals once, at a generic in-domain point (only the
    // operation sequence matters: the kernel has no branch on the
    // parameters), then optimizes the tape. The 3 parameters per expiry are
    // the independent variables.
    explicit CppadJacobian(const Market& m)
    {
        std::vector<AD> x(3 * m.T.size());
        for (std::size_t e = 0; e < m.T.size(); ++e) {
            x[3 * e] = 0.05;      // alpha
            x[3 * e + 1] = -0.2;  // rho
            x[3 * e + 2] = 0.5;   // nu
        }
        CppAD::Independent(x);
        std::vector<AD> r = residuals(m, quote_terms(m), x);
        f_.Dependent(x, r);
        f_.optimize();

        // Identity seed for the n first-order directions, xq[j*n + l] = (j == l).
        const std::size_t n = x.size();
        seed_.assign(n * n, 0.0);
        for (std::size_t j = 0; j < n; ++j) seed_[j * n + j] = 1.0;
    }

    // Row-major n_quotes x n_params Jacobian at x from the recorded function.
    // Mode: forward, one zero-order sweep at x, then one first-order sweep
    // with all n directions at once (Forward(1, n, seed)); its result
    // yq[i*n + l] = dr_i/dx_l is already row-major.
    std::vector<double> operator()(const std::vector<double>& x)
    {
        f_.Forward(0, x);
        return f_.Forward(1, seed_.size() / x.size(), seed_);
    }

private:
    CppAD::ADFun<double> f_;
    std::vector<double> seed_;
};

}  // namespace sabr
