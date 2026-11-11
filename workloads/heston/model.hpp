// Heston Monte Carlo pricer of an option book, written once as a template on
// the number type and used unchanged by plain double and every AD library.
//
// Scheme: full-truncation Euler (Lord, Koekkoek, van Dijk), with v+ = max(v, 0):
//   v_{t+1}     = v_t + kappa (theta - v+) dt + xi sqrt(v+ dt) Z_v
//   log S_{t+1} = log S_t + (r - v+ / 2) dt + sqrt(v+ dt) Z_s
// with Z_v = z1, Z_s = rho z1 + sqrt(1 - rho^2) z2, so rho is a live input.
// The independent normals z1, z2 are pre-generated (make_draws), outside the
// kernel, and shared by every column.
// Payoffs: Call max(S_T - K, 0), Put max(K - S_T, 0), AsianCall max(A - K, 0)
// with A the arithmetic average of S over steps 1..T/dt. Each option is
// discounted with exp(-r T).

#pragma once

#include "harness/rng.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace heston {

// Member order is also the gradient order.
template <class Real>
struct Params {
    Real s0, v0, kappa, theta, xi, rho, r;
};

enum class Kind { Call, Put, AsianCall };

struct Option {
    Kind kind;
    double strike;
    double maturity;  // years, a multiple of dt
};

// z[2 * (path * steps + step)] = z1, z[2 * (path * steps + step) + 1] = z2.
struct Draws {
    std::size_t paths = 0, steps = 0;
    std::vector<double> z;
};

inline Draws make_draws(std::size_t paths, std::size_t steps, std::uint64_t seed)
{
    harness::Rng rng(seed);
    return Draws{paths, steps, rng.normals(2 * paths * steps)};
}

// The only branches on differentiated values in the kernel: max(v, 0) (full
// truncation and the option payoffs) and sqrt(max(v, 0)) (the diffusion).
// Libraries that record a fixed operation sequence (CppAD) specialise Kinks so
// their tape stays valid at every input, e.g. with a conditional expression;
// the default is a plain branch, which adds nothing to a tape on the zero
// side. sqrt_positive_part never evaluates sqrt at 0, whose infinite
// derivative times a zero tangent would give NaN in forward mode.
template <class Real>
struct Kinks {
    static Real positive_part(const Real& v) { return v > 0.0 ? v : Real(0.0); }
    static Real sqrt_positive_part(const Real& v)
    {
        using std::sqrt;
        return v > 0.0 ? Real(sqrt(v)) : Real(0.0);
    }
};

template <class Real>
Real positive_part(const Real& v)
{
    return Kinks<Real>::positive_part(v);
}

template <class Real>
Real sqrt_positive_part(const Real& v)
{
    return Kinks<Real>::sqrt_positive_part(v);
}

// Where each option of a book falls on the time grid; computed once per book.
struct Schedule {
    std::vector<std::size_t> index;  // maturity step of each option, lround(T / dt)
    std::size_t last = 0;            // steps to simulate: the largest index
    bool asian = false;              // any Asian option (running sum of S needed)
};

inline Schedule make_schedule(const std::vector<Option>& book, std::size_t steps, double dt)
{
    Schedule s;
    s.index.resize(book.size());
    for (std::size_t k = 0; k < book.size(); ++k) {
        const long n = std::lround(book[k].maturity / dt);
        if (n < 1 || static_cast<std::size_t>(n) > steps)
            throw std::invalid_argument("heston: maturity outside the simulated steps");
        s.index[k] = static_cast<std::size_t>(n);
        if (s.index[k] > s.last) s.last = s.index[k];
        if (book[k].kind == Kind::AsianCall) s.asian = true;
    }
    return s;
}

// Functions of the inputs shared by every path.
template <class Real>
struct PathSetup {
    Real log_s0, rho_bar;
    std::vector<Real> discount;  // exp(-r T) per option
};

// Fills s in place, reusing its storage (for wirings that redo the setup per
// path).
template <class Real>
void path_setup(const Params<Real>& p, const std::vector<Option>& book, PathSetup<Real>& s)
{
    using std::exp;
    using std::log;
    using std::sqrt;
    s.log_s0 = log(p.s0);
    s.rho_bar = sqrt(1.0 - p.rho * p.rho);
    s.discount.resize(book.size());
    for (std::size_t k = 0; k < book.size(); ++k) s.discount[k] = exp(-p.r * book[k].maturity);
}

template <class Real>
PathSetup<Real> path_setup(const Params<Real>& p, const std::vector<Option>& book)
{
    PathSetup<Real> s{Real(0.0), Real(0.0), {}};
    path_setup(p, book, s);
    return s;
}

// Discounted book payoff of one path. z holds that path's normals, z1 and z2
// interleaved per step; Z is double, or a library's parameter type when one
// path is recorded once and re-evaluated with each path's normals.
template <class Real, class Z>
Real path_payoff(const Params<Real>& p, const PathSetup<Real>& s, const std::vector<Option>& book,
                 const Schedule& sch, const Z* z, double dt)
{
    using std::exp;

    const double sqrt_dt = std::sqrt(dt);
    Real payoff(0.0);
    Real x = s.log_s0;
    Real v = p.v0;
    Real s_sum(0.0);  // sum of S over steps 1..t+1, for Asian options
    for (std::size_t t = 0; t < sch.last; ++t) {
        const Z& z1 = z[2 * t];
        const Z& z2 = z[2 * t + 1];
        const Real vp = positive_part(v);
        const Real sd = sqrt_positive_part(v) * sqrt_dt;
        x += (p.r - 0.5 * vp) * dt + sd * (p.rho * z1 + s.rho_bar * z2);
        v += p.kappa * (p.theta - vp) * dt + p.xi * sd * z1;
        if (sch.asian) s_sum += exp(x);
        for (std::size_t k = 0; k < book.size(); ++k) {
            if (sch.index[k] != t + 1) continue;
            const double strike = book[k].strike;
            Real intrinsic;
            switch (book[k].kind) {
                case Kind::Call: intrinsic = exp(x) - strike; break;
                case Kind::Put: intrinsic = strike - exp(x); break;
                case Kind::AsianCall: intrinsic = s_sum / static_cast<double>(sch.index[k]) - strike; break;
            }
            payoff += s.discount[k] * positive_part(intrinsic);
        }
    }
    return payoff;
}

// Monte Carlo mean over paths of the discounted book payoff (sum over options).
template <class Real>
Real price(const Params<Real>& p, const std::vector<Option>& book, const Draws& d, double dt)
{
    const Schedule sch = make_schedule(book, d.steps, dt);
    const PathSetup<Real> s = path_setup(p, book);
    Real sum(0.0);
    for (std::size_t i = 0; i < d.paths; ++i) sum += path_payoff(p, s, book, sch, &d.z[2 * i * d.steps], dt);
    return sum / static_cast<double>(d.paths);
}

}  // namespace heston
