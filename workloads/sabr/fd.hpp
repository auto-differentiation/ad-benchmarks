// W4 reference: central finite-difference residual Jacobian.

#pragma once

#include "workloads/sabr/model.hpp"

#include <cstddef>
#include <vector>

namespace sabr {

class FdJacobian {
public:
    // h: absolute step, the same for every parameter.
    FdJacobian(const Market& m, double h) : m_(m), terms_(quote_terms(m)), h_(h) {}

    // Row-major n_quotes x n_params, like XadJacobian; column j is
    // (residuals(x + h e_j) - residuals(x - h e_j)) / (2h).
    std::vector<double> operator()(const std::vector<double>& x) const
    {
        const std::size_t n = x.size();
        const std::size_t nq = m_.K.size();
        std::vector<double> J(nq * n);
        std::vector<double> xs = x;
        for (std::size_t j = 0; j < n; ++j) {
            xs[j] = x[j] + h_;
            const std::vector<double> up = residuals(m_, terms_, xs);
            xs[j] = x[j] - h_;
            const std::vector<double> down = residuals(m_, terms_, xs);
            xs[j] = x[j];
            for (std::size_t i = 0; i < nq; ++i) J[i * n + j] = (up[i] - down[i]) / (2.0 * h_);
        }
        return J;
    }

private:
    Market m_;
    std::vector<QuoteTerms> terms_;  // parameter-independent, computed once
    double h_;
};

}  // namespace sabr
