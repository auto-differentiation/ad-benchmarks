// Compares a computed gradient with a reference, component by component.

#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <vector>

namespace harness {

struct CheckResult {
    bool ok = false;
    std::size_t worst_index = 0;
    double worst_rel_error = 0.0;  // |grad - ref| / max(|ref|, DBL_MIN)
};

// ok iff the sizes match and |grad[i] - ref[i]| <= atol + rtol * |ref[i]| for
// every i (a NaN fails). The worst component is the one with the largest
// relative error; NaN counts as worst and ties go to the lowest index.
// On a size mismatch only the common prefix is inspected.
inline CheckResult check(const std::vector<double>& grad, const std::vector<double>& ref,
                         double rtol, double atol = 0.0)
{
    CheckResult r;
    r.ok = grad.size() == ref.size();
    const std::size_t n = std::min(grad.size(), ref.size());
    for (std::size_t i = 0; i < n; ++i) {
        const double err = std::fabs(grad[i] - ref[i]);
        if (!(err <= atol + rtol * std::fabs(ref[i]))) r.ok = false;
        const double rel = err / std::max(std::fabs(ref[i]), DBL_MIN);
        const bool worse = std::isnan(rel) ? !std::isnan(r.worst_rel_error)
                                           : rel > r.worst_rel_error;
        if (worse) {
            r.worst_index = i;
            r.worst_rel_error = rel;
        }
    }
    return r;
}

}  // namespace harness
