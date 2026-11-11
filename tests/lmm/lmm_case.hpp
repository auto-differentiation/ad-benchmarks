// Test cases for W1 (LMM swaption portfolio), shared by the tests in tests/lmm.
//
// Kernel contract (workloads/lmm/model.hpp):
//   lmm::Setup   { int N; int Nmat; double delta; std::vector<int> length;
//                  std::vector<double> strike; std::size_t paths; }
//     N forwards L[0..N-1] with accrual delta; Nmat Euler steps (expiry
//     T = Nmat * delta); swaption j is a payer swaption on the swap over
//     periods Nmat .. Nmat + length[j] - 1 with fixed rate strike[j].
//   lmm::Inputs<Real> { std::vector<Real> L0;      // N initial forwards
//                       std::vector<Real> lambda; } // N - 1 vols, rate i at
//                                                   // step n uses lambda[i-n-1]
//   Real lmm::price<Real>(const Setup&, const Inputs<Real>&,
//                         const std::vector<double>& z)
//     z.size() == paths * Nmat, z[p * Nmat + n] drives step n of path p.
// Gradient layout for every column: [dV/dL0[0..N-1], dV/dlambda[0..N-2]].

#pragma once

#include "harness/rng.hpp"
#include "workloads/lmm/model.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lmm_case {

// Same numbers as small_case() in tests/reference/lmm_reference.py.
inline lmm::Setup small_setup(std::size_t paths = 4096)
{
    lmm::Setup s;
    s.N = 16;
    s.Nmat = 8;
    s.delta = 0.25;
    for (int len : {2, 4, 6, 8}) {
        for (double k : {0.045, 0.05, 0.055}) {
            s.length.push_back(len);
            s.strike.push_back(k);
        }
    }
    s.paths = paths;
    return s;
}

inline lmm::Inputs<double> small_inputs()
{
    lmm::Inputs<double> in;
    for (int i = 0; i < 16; ++i) in.L0.push_back(0.05 + 0.0005 * i);
    for (int k = 0; k < 15; ++k) in.lambda.push_back(0.2 - 0.002 * k);
    return in;
}

inline std::vector<double> draws(const lmm::Setup& s, std::uint64_t seed = 42)
{
    harness::Rng rng(seed);
    return rng.normals(s.paths * static_cast<std::size_t>(s.Nmat));
}

}  // namespace lmm_case

namespace lmm_case {

// A second input point for wirings that record once and re-evaluate (CppAD):
// forwards up 10 %, vols down 10 %, so many paths change which
// swaptions finish in the money.
inline lmm::Inputs<double> shifted_inputs()
{
    lmm::Inputs<double> in = small_inputs();
    for (double& l : in.L0) l *= 1.1;
    for (double& v : in.lambda) v *= 0.9;
    return in;
}

}  // namespace lmm_case
