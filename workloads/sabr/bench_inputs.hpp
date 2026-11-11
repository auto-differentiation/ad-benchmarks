// What bench_sabr gives every column's setup, extra columns included
// (harness/extra_columns.hpp): the market and x, the parameters every
// Jacobian column differentiates at.

#pragma once

#include "workloads/sabr/model.hpp"

#include <vector>

namespace sabr {

struct BenchInputs {
    Market market;
    std::vector<double> x;
};

}  // namespace sabr
