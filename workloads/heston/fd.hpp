// Central finite-difference gradient of the Heston book: the reference for the
// AD columns, and the fd column of the benchmark.

#pragma once

#include "harness/fd.hpp"
#include "workloads/heston/model.hpp"

#include <vector>

namespace heston {

// d price / d x for x = {s0, v0, kappa, theta, xi, rho, r} (Params order),
// by central differences of price<double> on the same draws, absolute step h.
inline std::vector<double> fd_gradient(const std::vector<double>& x, const std::vector<Option>& book,
                                       const Draws& d, double dt, double h)
{
    const auto f = [&](const std::vector<double>& y) {
        const Params<double> p{y[0], y[1], y[2], y[3], y[4], y[5], y[6]};
        return price(p, book, d, dt);
    };
    return harness::fd_gradient(f, x, h);
}

}  // namespace heston
