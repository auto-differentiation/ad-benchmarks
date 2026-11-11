// What bench_heston gives every column's setup, extra columns included
// (harness/extra_columns.hpp): x, the point the gradient columns
// differentiate at (Params member order), the option book, the draws and dt.

#pragma once

#include "workloads/heston/model.hpp"

#include <vector>

namespace heston {

struct BenchInputs {
    std::vector<double> x;
    std::vector<Option> book;
    Draws draws;
    double dt;
};

// The input point (Params member order) and option book bench_heston times
// and the tests check. Feller holds (2 kappa theta = 0.12 > xi^2 = 0.09).
inline std::vector<double> standard_x()
{
    return {/*s0=*/100.0, /*v0=*/0.05, /*kappa=*/1.5, /*theta=*/0.04,
            /*xi=*/0.3, /*rho=*/-0.7, /*r=*/0.03};
}

inline std::vector<Option> standard_book()
{
    return {{Kind::Call, 100.0, 1.0}, {Kind::Put, 95.0, 0.5}, {Kind::AsianCall, 100.0, 1.0}};
}

}  // namespace heston
