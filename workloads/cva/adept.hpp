// W3 CVA gradient with Adept-2 reverse mode, pathwise.
//
// Adept's stack has no partial reverse sweep or reset to a position, so the
// pathwise pattern is written with separate recordings:
//   1. the shared values (make_shared) in plain double;
//   2. per path: a new recording with the shared values as independent
//      adoubles, path_cva, one adjoint sweep; d path / d shared accumulated;
//   3. a recording of make_shared from the inputs, its outputs seeded with the
//      accumulated adjoints, one adjoint sweep to the inputs.
// Compared with XAD this computes the shared part once more in plain double
// (step 1), which is small next to the paths.

#pragma once

#include "workloads/cva/model.hpp"

#include <adept.h>

#include <cstddef>
#include <vector>

namespace cva {

namespace detail {

// Applies f(a, b) to the matching elements of two Shared values.
template <class A, class B, class F>
void for_each_shared(Shared<A>& a, Shared<B>& b, F f)
{
    const auto each = [&](std::vector<A>& va, std::vector<B>& vb) {
        for (std::size_t i = 0; i < va.size(); ++i) f(va[i], vb[i]);
    };
    each(a.drift, b.drift);
    each(a.chol, b.chol);
    each(a.lnfac, b.lnfac);
    each(a.weight, b.weight);
    each(a.fixed, b.fixed);
}

}  // namespace detail

// d CVA / d inputs in the flat layout of to_vector. *value (if given)
// receives the CVA computed on the stack.
inline std::vector<double> gradient_adept(const Inputs<double>& in, const Config& c, const Plan& pl,
                                          const std::vector<double>& z, double* value = nullptr)
{
    using adept::adouble;
    static adept::Stack stack;  // kept across calls, as the other tapes

    // 1. Shared values, as independent variables of the path recordings.
    Shared<double> acc = make_shared(in, c, pl);
    Shared<adouble> sh;
    sh.drift.assign(acc.drift.begin(), acc.drift.end());
    sh.chol.assign(acc.chol.begin(), acc.chol.end());
    sh.lnfac.assign(acc.lnfac.begin(), acc.lnfac.end());
    sh.weight.assign(acc.weight.begin(), acc.weight.end());
    sh.fixed.assign(acc.fixed.begin(), acc.fixed.end());
    detail::for_each_shared(acc, sh, [](double& a, adouble&) { a = 0.0; });

    // 2. Paths.
    const std::size_t KF = pl.K * pl.F;
    double v = 0.0;
    PathWork<adouble> w(pl);
    for (std::size_t p = 0; p < c.n_paths; ++p) {
        w.release();
        stack.new_recording();
        w.acquire(pl);
        adouble y = path_cva(sh, pl, c, &z[p * KF], w);
        y.set_gradient(1.0);
        stack.compute_adjoint();
        v += y.value();
        detail::for_each_shared(acc, sh, [](double& a, adouble& s) { a += s.get_gradient(); });
    }
    w.release();

    // 3. Shared part, seeded with the accumulated adjoints.
    const std::vector<double> flat = to_vector(in);
    std::vector<adouble> x(flat.begin(), flat.end());
    stack.new_recording();
    Shared<adouble> sx = make_shared(from_vector(x, c), c, pl);
    detail::for_each_shared(acc, sx, [](double& a, adouble& s) { s.set_gradient(a); });
    stack.compute_adjoint();

    if (value) *value = v;
    std::vector<double> g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) g[i] = x[i].get_gradient();
    return g;
}

inline std::vector<double> gradient_adept(const Inputs<double>& in, const Config& c,
                                          const std::vector<double>& z, double* value = nullptr)
{
    return gradient_adept(in, c, make_plan(c), z, value);
}

}  // namespace cva
