// Shared Heston test case: inputs, a mixed book and pre-generated draws.

#pragma once

#include "workloads/heston/bench_inputs.hpp"
#include "workloads/heston/model.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace heston_case {

// Gradient order used by every wiring: x = {s0, v0, kappa, theta, xi, rho, r}.
inline const std::array<const char*, 7> input_names{"s0", "v0", "kappa", "theta", "xi", "rho", "r"};

struct Case {
    std::vector<double> x;
    std::vector<heston::Option> book;
    heston::Draws draws;
    double dt;
};

// Feller condition holds (2 kappa theta = 0.12 > xi^2 = 0.09) and v0 is well
// above 0, so with dt = 0.01 the variance seldom reaches the max(v, 0) kink of
// full truncation; this keeps central FD on the same draws smooth.
inline Case small(std::size_t paths = 1000)
{
    Case c;
    c.x = heston::standard_x();
    c.book = heston::standard_book();
    c.dt = 0.01;
    c.draws = heston::make_draws(paths, /*steps=*/100, /*seed=*/7);
    return c;
}

// A second input point, far enough from small().x that many paths take the
// other side of a payoff kink or of the max(v, 0) truncation: a tape whose
// branches were frozen at small().x gives wrong gradients here.
inline std::vector<double> shifted_x()
{
    return {/*s0=*/92.0, /*v0=*/0.03, /*kappa=*/1.5, /*theta=*/0.04,
            /*xi=*/0.3, /*rho=*/-0.4, /*r=*/0.03};
}

}  // namespace heston_case
