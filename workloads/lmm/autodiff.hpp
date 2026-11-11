// W1 wiring for autodiff: FORWARD mode (autodiff::dual), one pass of the
// lmm::price kernel per input, 2N - 1 passes in all.
//
// Reverse mode (autodiff::var) is not usable for this kernel: var builds an
// expression tree and derivatives() propagates the adjoint recursively along
// every path of it (a named var passes its weight through instead of
// accumulating it), so the cost grows with the number of paths through the
// DAG. Here each forward rate feeds the drift of every later rate at every
// step, so that number grows exponentially with N * Nmat, and the recursion
// depth grows with the number of Monte Carlo paths summed into the price.
// On small development cases (one Monte Carlo path, Nmat = N / 2) the cost
// of one derivatives() call grew about tenfold per N + 2, reaching seconds at
// N = 14, so the small case (N = 16, 4096 paths) would take hours. Forward
// mode is slow too (a few hundred times plain at the published size), but
// finishes.

#pragma once

#include "workloads/lmm/model.hpp"

#include <autodiff/forward/dual.hpp>

#include <cstddef>
#include <vector>

namespace lmm {

// Gradient layout: [dV/dL0[0..N-1], dV/dlambda[0..N-2]].
inline std::vector<double> autodiff_gradient(const Setup& s, const Inputs<double>& x,
                                             const std::vector<double>& z)
{
    using AD = autodiff::dual;
    Inputs<AD> in;
    in.L0.assign(x.L0.begin(), x.L0.end());
    in.lambda.assign(x.lambda.begin(), x.lambda.end());

    const std::size_t n = in.L0.size();
    std::vector<double> g(n + in.lambda.size());
    for (std::size_t k = 0; k < g.size(); ++k) {
        AD& seed = k < n ? in.L0[k] : in.lambda[k - n];
        seed.grad = 1.0;
        g[k] = price(s, in, z).grad;
        seed.grad = 0.0;
    }
    return g;
}

}  // namespace lmm
