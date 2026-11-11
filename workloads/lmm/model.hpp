// W1 kernel: swaption portfolio under a single-factor log-normal LIBOR market
// model (Giles & Glasserman, "Smoking Adjoints", 2006). Written once as a
// template on the number type, used unchanged by plain double and every AD
// library.
//
// N forwards L[0..N-1] with accrual delta, Nmat Euler steps on log L under the
// spot measure (rate i at step n uses vol lambda[i - n - 1]), then payer
// swaptions expiring at T = Nmat * delta: swaption j pays
// 100 * max(1 - B_m - K_j * S_m, 0) with m = Nmat + length[j] - 1, discounted
// with L[0..Nmat-1] along the path. Price = mean over paths.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace lmm {

struct Setup {
    int N = 0;
    int Nmat = 0;
    double delta = 0.25;
    std::vector<int> length;     // swap periods per swaption
    std::vector<double> strike;  // fixed rate per swaption
    std::size_t paths = 0;
};

template <class Real>
struct Inputs {
    std::vector<Real> L0;      // N initial forwards
    std::vector<Real> lambda;  // N - 1 vols
};

// max(x, 0). Customisation point: a library that records a fixed tape (e.g.
// CppAD) can specialise this with a conditional expression.
template <class Real>
Real positive_part(const Real& x)
{
    return x > 0.0 ? x : Real(0.0);
}

// Per-path scratch (forwards, bonds, annuities), allocated once per price or
// gradient. For a pathwise tape, release() ends the elements' lifetimes before
// the tape is reset (their slots/indices belong to the path being discarded)
// and acquire() makes fresh ones for the next path; the storage is kept, so
// neither allocates.
template <class Real>
struct PathWork {
    std::vector<Real> L, B, S;
    explicit PathWork(int N) { acquire(N); }
    void acquire(int N)
    {
        L.resize(N);
        B.resize(N);
        S.resize(N);
    }
    void release()
    {
        L.clear();
        B.clear();
        S.clear();
    }
};

// Discounted portfolio payoff of one Monte Carlo path; z[0..Nmat-1] are that
// path's draws. Z is double, or an active type when the draws themselves are
// recorded with a one-path tape (CppAD: dynamic parameters).
template <class Real, class Z>
Real path_value(const Setup& s, const Inputs<Real>& in, const Z* z, PathWork<Real>& w)
{
    using std::exp;
    using std::sqrt;
    const int N = s.N;
    const int Nmat = s.Nmat;
    const double delta = s.delta;
    const double sd = sqrt(delta);

    std::vector<Real>& L = w.L;
    std::vector<Real>& B = w.B;
    std::vector<Real>& S = w.S;
    for (int i = 0; i < N; ++i) L[i] = in.L0[i];
    for (int n = 0; n < Nmat; ++n) {
        const Z dw = sd * z[n];
        Real drift(0.0);
        for (int i = n + 1; i < N; ++i) {
            const Real& v = in.lambda[i - n - 1];
            drift += delta * v * L[i] / (1.0 + delta * L[i]);
            L[i] *= exp(delta * v * drift - 0.5 * v * v * delta + v * dw);
        }
    }
    Real b(1.0), sum(0.0);
    for (int m = Nmat; m < N; ++m) {
        b /= 1.0 + delta * L[m];
        sum += delta * b;
        B[m] = b;
        S[m] = sum;
    }
    Real payoff(0.0);
    for (std::size_t j = 0; j < s.length.size(); ++j) {
        const int m = Nmat + s.length[j] - 1;
        payoff += 100.0 * positive_part(Real(1.0 - B[m] - s.strike[j] * S[m]));
    }
    Real disc(1.0);
    for (int n = 0; n < Nmat; ++n) disc /= 1.0 + delta * L[n];
    return payoff * disc;
}

// z.size() == paths * Nmat; z[p * Nmat + n] drives step n of path p.
template <class Real>
Real price(const Setup& s, const Inputs<Real>& in, const std::vector<double>& z)
{
    PathWork<Real> w(s.N);
    Real total(0.0);
    for (std::size_t p = 0; p < s.paths; ++p)
        total += path_value(s, in, z.data() + p * static_cast<std::size_t>(s.Nmat), w);
    return total / static_cast<double>(s.paths);
}

}  // namespace lmm
