// W3 CVA reference gradient by central finite differences.

#pragma once

#include "workloads/cva/model.hpp"

#include "harness/fd.hpp"

#include <vector>

namespace cva {

// Central FD of cva::cva<double> over the flat layout of to_vector, with the
// same shocks z for every evaluation and the absolute step h. *value (if
// given) receives the unbumped CVA.
inline std::vector<double> gradient_fd(const Inputs<double>& in, const Config& c, const Plan& pl,
                                       const std::vector<double>& z, double h, double* value = nullptr)
{
    const auto f = [&](const std::vector<double>& x) { return cva::cva(from_vector(x, c), c, pl, z); };
    if (value) *value = cva::cva(in, c, pl, z);
    return harness::fd_gradient(f, to_vector(in), h);
}

inline std::vector<double> gradient_fd(const Inputs<double>& in, const Config& c,
                                       const std::vector<double>& z, double h, double* value = nullptr)
{
    return gradient_fd(in, c, make_plan(c), z, h, value);
}

}  // namespace cva
