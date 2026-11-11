// W4 wiring for Adept-2: residual Jacobian from a recorded stack.

#pragma once

#include "workloads/sabr/model.hpp"

#include <adept.h>

#include <cstddef>
#include <vector>

namespace sabr {

// Mode: forward, Stack::jacobian_forward over the recording (Adept's own
// multi-pass forward sweeps); the residuals are re-recorded on every call.
class AdeptJacobian {
public:
    using AD = adept::adouble;

    explicit AdeptJacobian(const Market& m) : m_(m), terms_(quote_terms(m)) {}

    // Row-major n_quotes x n_params Jacobian at x, J[i*n + j] = dr_i/dx_j.
    std::vector<double> operator()(const std::vector<double>& x)
    {
        const std::size_t n = x.size();
        stack_.activate();
        std::vector<AD> xa(x.begin(), x.end());
        stack_.new_recording();
        const std::vector<AD> r = residuals(m_, terms_, xa);
        stack_.independent(xa.data(), n);
        stack_.dependent(r.data(), r.size());
        std::vector<double> J(r.size() * n);
        // Element (i, j) at i * dep_offset + j * indep_offset: row-major.
        stack_.jacobian_forward(J.data(), static_cast<adept::Index>(n), 1);
        return J;
    }

private:
    Market m_;
    std::vector<QuoteTerms> terms_;  // parameter-independent, computed once
    adept::Stack stack_{false};  // activated on use
};

}  // namespace sabr
