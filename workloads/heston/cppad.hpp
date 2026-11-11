// CppAD wiring of the Heston book: one path recorded once into an ADFun and
// optimized, then evaluated per path (see CppadGradient).
//
// The kernel's kinks (full truncation max(v, 0) and the payoffs max(., 0)) are
// recorded as conditional expressions (Kinks specialisation below), so the
// tape is valid at every input, not only at the recording point.

#pragma once

#include "workloads/cppad_hold_memory.hpp"
#include "workloads/heston/model.hpp"

#include <cppad/cppad.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace heston {

template <class Base>
struct Kinks<CppAD::AD<Base>> {
    static CppAD::AD<Base> positive_part(const CppAD::AD<Base>& v)
    {
        const CppAD::AD<Base> zero(0.0);
        return CppAD::CondExpGt(v, zero, v, zero);
    }
    // sqrt of the selected value, so the argument is never negative; at 0 the
    // reverse sweep routes the sqrt partial to the constant branch.
    static CppAD::AD<Base> sqrt_positive_part(const CppAD::AD<Base>& v) { return sqrt(positive_part(v)); }
};

// Gradient d price / d x for x = {s0, v0, kappa, theta, xi, rho, r} (Params
// order) with the Monte Carlo pattern for CppAD: ONE path is recorded once,
// the inputs as independents and that path's normals as dynamic parameters,
// and optimized once (constructor, at x0). operator() then evaluates the tape
// per path, new_dynamic(normals) + Forward(0) + Reverse(1), and accumulates
// the gradient. d must outlive this object.
class CppadGradient {
public:
    CppadGradient(const std::vector<double>& x0, const std::vector<Option>& book, const Draws& d, double dt)
        : d_(d), sch_(make_schedule(book, d.steps, dt)), z_(2 * sch_.last), w_{1.0 / static_cast<double>(d.paths)}
    {
        using AD = CppAD::AD<double>;
        std::vector<AD> ax(x0.begin(), x0.end());
        std::vector<AD> az(d.z.begin(), d.z.begin() + static_cast<std::ptrdiff_t>(z_.size()));
        CppAD::Independent(ax, /*abort_op_index=*/0, /*record_compare=*/false, az);
        const Params<AD> p{ax[0], ax[1], ax[2], ax[3], ax[4], ax[5], ax[6]};
        const PathSetup<AD> s = path_setup(p, book);
        std::vector<AD> ay{path_payoff(p, s, book, sch_, az.data(), dt)};
        f_.Dependent(ax, ay);
        f_.optimize();
    }

    std::vector<double> operator()(const std::vector<double>& x)
    {
        std::vector<double> g(x.size(), 0.0);
        for (std::size_t i = 0; i < d_.paths; ++i) {
            const double* zi = &d_.z[2 * i * d_.steps];
            std::copy(zi, zi + z_.size(), z_.begin());
            f_.new_dynamic(z_);
            f_.Forward(0, x);
            const std::vector<double> gi = f_.Reverse(1, w_);
            for (std::size_t j = 0; j < g.size(); ++j) g[j] += gi[j];
        }
        return g;
    }

private:
    const Draws& d_;
    Schedule sch_;
    std::vector<double> z_;  // normals of the current path
    std::vector<double> w_;  // 1 / paths
    CppAD::ADFun<double> f_;
};

}  // namespace heston
