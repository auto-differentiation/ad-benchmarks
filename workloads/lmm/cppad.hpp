// W1 wiring for CppAD: ONE Monte Carlo path of lmm::path_value is recorded
// once into a CppAD::ADFun, with the inputs (L0, lambda) as independents and
// that path's normals as dynamic parameters, and optimized. A gradient then
// runs, for every path, new_dynamic(draws of the path) + Forward(0) +
// Reverse(1) on that one tape (no re-taping) and averages. max(., 0) is a
// CondExp so the tape stays valid when inputs or draws move a path across the
// kink.

#pragma once

#include "workloads/cppad_hold_memory.hpp"
#include "workloads/lmm/model.hpp"

#include <cppad/cppad.hpp>

#include <cstddef>
#include <vector>

namespace lmm {

template <>
inline CppAD::AD<double> positive_part(const CppAD::AD<double>& x)
{
    const CppAD::AD<double> zero(0.0);
    return CppAD::CondExpGt(x, zero, x, zero);
}

class CppadGradient {
public:
    // Records one path for this setup; the draws are kept for the gradients.
    // The tape does not depend on the values it is recorded at
    // (L0 = 0.05, lambda = 0.2, draws = 0).
    CppadGradient(const Setup& s, const std::vector<double>& z)
        : s_(s), z_(z), n_(static_cast<std::size_t>(2 * s.N - 1))
    {
        using AD = CppAD::AD<double>;
        const std::size_t nmat = static_cast<std::size_t>(s.Nmat);
        std::vector<AD> x(n_);
        for (int i = 0; i < s.N; ++i) x[i] = 0.05;
        for (std::size_t k = s.N; k < n_; ++k) x[k] = 0.2;
        std::vector<AD> dyn(nmat, AD(0.0));
        CppAD::Independent(x, 0, false, dyn);
        Inputs<AD> in;
        in.L0.assign(x.begin(), x.begin() + s.N);
        in.lambda.assign(x.begin() + s.N, x.end());
        PathWork<AD> w(s.N);
        std::vector<AD> y{path_value(s, in, dyn.data(), w)};
        f_.Dependent(x, y);
        f_.optimize();
    }

    // Gradient layout: [dV/dL0[0..N-1], dV/dlambda[0..N-2]].
    std::vector<double> operator()(const Inputs<double>& in)
    {
        const std::size_t nmat = static_cast<std::size_t>(s_.Nmat);
        std::vector<double> x(in.L0);
        x.insert(x.end(), in.lambda.begin(), in.lambda.end());
        std::vector<double> zp(nmat), dx(n_);
        const std::vector<double> w{1.0};
        std::vector<double> g(n_, 0.0);
        for (std::size_t p = 0; p < s_.paths; ++p) {
            for (std::size_t n = 0; n < nmat; ++n) zp[n] = z_[p * nmat + n];
            f_.new_dynamic(zp);
            f_.Forward(0, x);
            dx = f_.Reverse(1, w);
            for (std::size_t k = 0; k < n_; ++k) g[k] += dx[k];
        }
        for (double& v : g) v /= static_cast<double>(s_.paths);
        return g;
    }

private:
    Setup s_;
    std::vector<double> z_;
    std::size_t n_;
    CppAD::ADFun<double> f_;
};

}  // namespace lmm
