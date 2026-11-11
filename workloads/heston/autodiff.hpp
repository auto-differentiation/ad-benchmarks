// autodiff wiring of the Heston book: forward mode (autodiff::dual), one
// pass of the kernel per input.
//
// Why not reverse mode (autodiff::var): its derivatives() walks the
// expression graph recursively and visits a node once per path from the
// output to it, without accumulating adjoints at shared nodes. In this kernel
// the variance v_t feeds v_{t+1} through several terms (drift, the
// sqrt(v+) diffusion, and the log S update), so the number of paths back to
// v0 grows exponentially in the number of steps (roughly 2^100 per path at
// 100 steps). Forward mode costs 7 kernel passes and is exact.

#pragma once

#include "workloads/heston/model.hpp"

#include <autodiff/forward/dual.hpp>

#include <cstddef>
#include <vector>

namespace heston {

// d price / d x for x = {s0, v0, kappa, theta, xi, rho, r} (Params order).
inline std::vector<double> autodiff_gradient(const std::vector<double>& x, const std::vector<Option>& book,
                                             const Draws& d, double dt)
{
    using AD = autodiff::dual;

    std::vector<double> g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        Params<AD> p{x[0], x[1], x[2], x[3], x[4], x[5], x[6]};
        AD* inputs[] = {&p.s0, &p.v0, &p.kappa, &p.theta, &p.xi, &p.rho, &p.r};
        inputs[i]->grad = 1.0;
        const AD y = price(p, book, d, dt);
        g[i] = y.grad;
    }
    return g;
}

}  // namespace heston
