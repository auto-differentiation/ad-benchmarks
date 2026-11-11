// W1 wiring for Adept-2: adept::Stack + adouble, pathwise. Adept has no
// partial adjoint sweep or reset to a tape position, so each Monte Carlo path
// of lmm::path_value is its own recording (new_recording keeps the input
// variables), swept with compute_adjoint, and the input adjoints are summed
// by hand. The stack never holds more than one path. Gradient = sum / paths.

#pragma once

#include "workloads/lmm/model.hpp"

#include <adept.h>

#include <cstddef>
#include <vector>

namespace lmm {

// Gradient layout: [dV/dL0[0..N-1], dV/dlambda[0..N-2]].
inline std::vector<double> adept_gradient(const Setup& s, const Inputs<double>& x,
                                          const std::vector<double>& z)
{
    using AD = adept::adouble;
    static adept::Stack stack;  // kept across calls, as the other tapes

    Inputs<AD> in;
    in.L0.assign(x.L0.begin(), x.L0.end());
    in.lambda.assign(x.lambda.begin(), x.lambda.end());
    const std::size_t n = in.L0.size();
    std::vector<double> g(n + in.lambda.size(), 0.0);
    const std::size_t nmat = static_cast<std::size_t>(s.Nmat);
    PathWork<AD> w(s.N);
    for (std::size_t p = 0; p < s.paths; ++p) {
        w.release();
        stack.new_recording();
        w.acquire(s.N);
        AD v = path_value(s, in, z.data() + p * nmat, w);
        v.set_gradient(1.0);
        stack.compute_adjoint();
        for (std::size_t i = 0; i < n; ++i) g[i] += in.L0[i].get_gradient();
        for (std::size_t k = 0; k < in.lambda.size(); ++k) g[n + k] += in.lambda[k].get_gradient();
    }
    for (double& x : g) x /= static_cast<double>(s.paths);
    return g;
}

}  // namespace lmm
