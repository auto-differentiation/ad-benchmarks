// What run_bench gives every column's setup, extra columns included
// (harness/extra_columns.hpp): the setup, the model inputs, x = L0 then
// lambda (the point the gradient columns differentiate at) and the normals z.

#pragma once

#include "workloads/lmm/model.hpp"

#include <vector>

namespace lmm {

struct BenchInputs {
    Setup setup;
    Inputs<double> inputs;
    std::vector<double> x;
    std::vector<double> z;
};

}  // namespace lmm
