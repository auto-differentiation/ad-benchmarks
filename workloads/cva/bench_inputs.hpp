// What bench_cva gives every column's setup, extra columns included
// (harness/extra_columns.hpp): the configuration, the model inputs, x =
// to_vector(inputs) (the point the gradient columns differentiate at) and the
// shocks z.

#pragma once

#include "workloads/cva/model.hpp"

#include <vector>

namespace cva {

struct BenchInputs {
    Config config;
    Inputs<double> inputs;
    std::vector<double> x;
    std::vector<double> z;
};

}  // namespace cva
