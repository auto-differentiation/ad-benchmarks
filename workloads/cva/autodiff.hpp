// W3 CVA gradient with autodiff, forward mode (autodiff::dual).
//
// Why forward and not reverse (autodiff::var): autodiff's reverse mode builds
// a tree of reference-counted nodes and propagates adjoints by recursion over
// that tree, revisiting shared subexpressions once per use. The CVA kernel
// shares many (step Cholesky factors and drifts, bond A(t, T) terms, curve
// discounts) across all paths. On small development cases reverse var took
// over a thousand times plain and over a hundred times XAD, and extrapolated
// to over a minute per gradient at the timing size. Forward mode (one kernel
// pass per input, 39 inputs) costs about 50 times plain there.

#pragma once

#include "workloads/cva/model.hpp"

#include <autodiff/forward/dual.hpp>

#include <cstddef>
#include <vector>

namespace cva {

// d CVA / d inputs in the flat layout of to_vector: one forward pass of the
// kernel per input, seeded with that input's unit tangent. *value (if given)
// receives the CVA of the first pass.
inline std::vector<double> gradient_autodiff(const Inputs<double>& in, const Config& c, const Plan& pl,
                                             const std::vector<double>& z, double* value = nullptr)
{
    using autodiff::dual;
    const std::vector<double> flat = to_vector(in);
    std::vector<dual> x(flat.begin(), flat.end());
    std::vector<double> g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i].grad = 1.0;
        const dual y = cva::cva(from_vector(x, c), c, pl, z);
        x[i].grad = 0.0;
        g[i] = y.grad;
        if (i == 0 && value) *value = y.val;
    }
    return g;
}

inline std::vector<double> gradient_autodiff(const Inputs<double>& in, const Config& c,
                                             const std::vector<double>& z, double* value = nullptr)
{
    return gradient_autodiff(in, c, make_plan(c), z, value);
}

}  // namespace cva
