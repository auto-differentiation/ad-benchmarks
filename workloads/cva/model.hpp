// W3: CVA on a swap portfolio under a correlated multi-factor Gaussian
// short-rate model (G2++ generalised to F factors).
//
// The kernel is written once as templates on the number type Real and is used
// unchanged by plain double and every AD library. Config holds what is not
// differentiated; Inputs<Real> holds the differentiated inputs. The normal
// draws are pre-generated outside the kernel (harness::Rng) and passed in as
// a vector of Shock: double for most libraries, the library's number type
// where the shocks are parameters of a recorded tape (CppAD dynamic
// parameters), so one recording serves every set of shocks.
//
// Model: r(t) = phi(t) + sum_i x_i(t), x_i(0) = 0,
//   dx_i = -a_i x_i dt + sigma_i dW_i,  d<W_i, W_j> = rho_ij dt   (under Q),
// with phi fitted to the initial zero curve. Zero-coupon bonds are closed form
// (Brigo & Mercurio, Interest Rate Models, sec. 4.2):
//   P(t, T) = P(0, T) / P(0, t) exp(A(t, T) - sum_i B_i(t, T) x_i(t)),
//   B_i(t, T) = (1 - e^{-a_i (T - t)}) / a_i,
//   A(t, T) = (V(T - t) - V(T) + V(t)) / 2,
//   V(tau) = sum_ij rho_ij sigma_i sigma_j / (a_i a_j)
//            [tau + (e^{-a_i tau} - 1) / a_i + (e^{-a_j tau} - 1) / a_j
//                 - (e^{-(a_i + a_j) tau} - 1) / (a_i + a_j)].
//
// Simulation measure: the T*-forward measure, numeraire P(t, T*), with T* the
// horizon (latest exposure date or swap maturity). There the factors are still
// Gaussian and each step s -> t is exact:
//   x_i(t) = e^{-a_i (t - s)} x_i(s) - M_i(s, t) + (L z)_i,
//   M_i(s, t) = sum_j rho_ij sigma_i sigma_j / a_j
//               [(1 - e^{-a_i (t - s)}) / a_i
//                - (e^{-a_j (T* - t)} - e^{-a_j T* - a_i t + (a_i + a_j) s}) / (a_i + a_j)],
// L the lower Cholesky factor of the step covariance
//   C_ij = rho_ij sigma_i sigma_j (1 - e^{-(a_i + a_j)(t - s)}) / (a_i + a_j).
// Discounting is then exact on every path: D(0, t) is never integrated, since
//   E^Q[D(0, t) X(t)] = P(0, T*) E^{T*}[X(t) / P(t, T*)].
//
// Initial curve: continuously compounded zero rates at curve_times, linear
// interpolation in the zero rate, flat extrapolation on both sides.
//
// Swaps: single curve, fixed and float legs with the same schedule
// start + period, start + 2 period, ..., maturity, accrual = period. Payer pays
// fixed. Cash flows paid on an exposure date are no longer in the exposure.
// The float coupon of the period in progress is fixed at its start T_{j-1} at
// 1 / P(T_{j-1}, T_j); the model reads that bond on the latest grid date on or
// before T_{j-1} (t_0 = 0 included) as the forward P(t_m, T_j) / P(t_m, T_{j-1}).
// This is exact when fixing dates are on the exposure grid.
//
// Counterparty default: piecewise-constant hazard rate, hazard_rates[j] on
// (hazard_times[j-1], hazard_times[j]] (hazard_times[-1] = 0), the last one
// extended flat; S(t) = exp(-int_0^t h). Unilateral CVA on the exposure grid:
//   CVA = LGD * sum_k P(0, t_k) EE+(t_k) (S(t_{k-1}) - S(t_k)).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace cva {

struct Swap {
    double notional = 0.0;
    double start = 0.0;
    double maturity = 0.0;
    double period = 0.0;
    bool payer = true;
};

struct Config {
    std::vector<double> mean_reversions;  // a_i > 0, one per factor
    std::vector<double> dates;            // exposure dates t_1 < ... < t_K (t_0 = 0)
    std::size_t n_paths = 0;
    std::vector<double> curve_times;      // pillars of the initial zero curve
    std::vector<Swap> swaps;
    std::vector<double> hazard_times;     // right ends of the hazard intervals
};

template <class Real>
struct Inputs {
    std::vector<Real> vols;          // sigma_i, one per factor
    std::vector<Real> correlations;  // strict lower triangle of rho, row by row
    std::vector<Real> zero_rates;    // continuously compounded, at curve_times
    std::vector<Real> fixed_rates;   // one per swap
    std::vector<Real> hazard_rates;  // one per hazard interval
    Real lgd = Real(0.0);            // loss given default
};

namespace detail {

constexpr double time_eps = 1e-10;

// rho_ij, any i, j (1 on the diagonal).
template <class Real>
Real rho(const Inputs<Real>& in, std::size_t i, std::size_t j)
{
    if (i == j) return Real(1.0);
    if (i < j) std::swap(i, j);
    return in.correlations[i * (i - 1) / 2 + j];
}

// ln P(0, t) from the zero curve.
template <class Real>
Real log_discount(const Inputs<Real>& in, const Config& c, double t)
{
    const auto& ts = c.curve_times;
    const auto& rs = in.zero_rates;
    if (t <= ts.front()) return -rs.front() * t;
    if (t >= ts.back()) return -rs.back() * t;
    const std::size_t hi = std::upper_bound(ts.begin(), ts.end(), t) - ts.begin();
    const std::size_t lo = hi - 1;
    const double w = (t - ts[lo]) / (ts[hi] - ts[lo]);
    const Real r = rs[lo] * (1.0 - w) + rs[hi] * w;
    return -r * t;
}

// int_0^t h(u) du for the piecewise-constant hazard rate.
template <class Real>
Real cumulative_hazard(const Inputs<Real>& in, const Config& c, double t)
{
    const auto& ts = c.hazard_times;
    Real H(0.0);
    double left = 0.0;
    for (std::size_t j = 0; j < ts.size() && left < t; ++j) {
        const double right = j + 1 == ts.size() ? t : std::min(t, ts[j]);
        H = H + in.hazard_rates[j] * (right - left);
        left = right;
    }
    return H;
}

// max(v, 0). The one branch on a differentiated value in the kernel; libraries
// that record a fixed operation sequence can overload it with a conditional
// expression.
template <class Real>
Real positive_part(const Real& v)
{
    return v > 0.0 ? v : Real(0.0);
}

}  // namespace detail

inline std::size_t n_factors(const Config& c) { return c.mean_reversions.size(); }

// Number of differentiated inputs, the length of the flat layout
// zero_rates, vols, correlations, hazard_rates, lgd, fixed_rates.
inline std::size_t n_inputs(const Config& c)
{
    const std::size_t F = n_factors(c);
    return c.curve_times.size() + F + F * (F - 1) / 2 + c.hazard_times.size() + 1 + c.swaps.size();
}

// Inputs in the flat layout (see n_inputs).
inline std::vector<double> to_vector(const Inputs<double>& in)
{
    std::vector<double> v;
    v.insert(v.end(), in.zero_rates.begin(), in.zero_rates.end());
    v.insert(v.end(), in.vols.begin(), in.vols.end());
    v.insert(v.end(), in.correlations.begin(), in.correlations.end());
    v.insert(v.end(), in.hazard_rates.begin(), in.hazard_rates.end());
    v.push_back(in.lgd);
    v.insert(v.end(), in.fixed_rates.begin(), in.fixed_rates.end());
    return v;
}

// Inverse of to_vector; the sizes come from the configuration.
template <class Real>
Inputs<Real> from_vector(const std::vector<Real>& v, const Config& c)
{
    const std::size_t F = n_factors(c);
    Inputs<Real> in;
    auto it = v.begin();
    const auto take = [&](std::vector<Real>& dst, std::size_t n) {
        dst.assign(it, it + n);
        it += n;
    };
    take(in.zero_rates, c.curve_times.size());
    take(in.vols, F);
    take(in.correlations, F * (F - 1) / 2);
    take(in.hazard_rates, c.hazard_times.size());
    in.lgd = *it++;
    take(in.fixed_rates, c.swaps.size());
    return in;
}

// Number of N(0, 1) draws simulate_factors consumes.
inline std::size_t n_normals(const Config& c)
{
    return c.n_paths * c.dates.size() * n_factors(c);
}

// T*, the maturity of the numeraire bond.
inline double horizon(const Config& c)
{
    double h = c.dates.empty() ? 0.0 : c.dates.back();
    for (const Swap& s : c.swaps) h = std::max(h, s.maturity);
    return h;
}

// ---------------------------------------------------------------------------
// The kernel in two parts, so that tape libraries can record the work shared
// by all paths once and each path on its own:
//   Plan            what depends on the configuration only (doubles);
//   Shared<Real>    what depends on the inputs but not on the path: step
//                   drifts and Cholesky factors, the bond factors A, the
//                   per-date CVA weights;
//   per path        simulate_path, path_deflated_exposure, path_cva, which
//                   read only the Plan, the Shared values and the path's shocks.
// cva() = sum over paths of path_cva(); every column evaluates exactly this.
// ---------------------------------------------------------------------------

// Configuration-only data: grid, bond maturities, swap schedules, decays, B.
struct Plan {
    struct Schedule {
        std::vector<std::size_t> mat;     // index into mats of T_0 = start, T_1, ..., T_n
        std::vector<double> time;         // T_0, ..., T_n
        std::vector<std::size_t> fixing;  // per period l = 1..n (index l - 1): grid index of its fixing
    };
    std::size_t F = 0, K = 0, M = 0;
    std::vector<double> grid;      // t_0 = 0, t_1..t_K
    std::vector<double> mats;      // bond maturities; mats[0] = T* (numeraire)
    std::vector<Schedule> sched;   // one per swap
    std::vector<double> decay;     // [k * F + i] = e^{-a_i (t_{k+1} - t_k)}
    std::vector<double> B;         // [(g * M + m) * F + i] = B_i(t_g, T_m), 0 if T_m <= t_g
    std::vector<unsigned char> alive;  // [g * M + m]: T_m > t_g

    // Coefficients that depend on the configuration only, so every column
    // computes them once in setup (make_plan) rather than per evaluation:
    std::vector<double> chol_coef;   // [(k * F + i) * F + j] = (1 - e^{-(a_i + a_j) dt_k}) / (a_i + a_j)
    std::vector<double> drift_coef;  // [(k * F + i) * F + j]: M_i(t_k, t_{k+1}) = sum_j rho_ij s_i s_j coef
    std::vector<double> A_coef;      // [(g * M + m) * F * F + i * F + j]: A(t_g, T_m) = sum_ij rho_ij s_i s_j coef

    // Cash flows still to come at each exposure date: for k = 1..K the swaps
    // alive at t_k with their first unpaid period j and, once the swap has
    // started, the grid index of that period's fixing.
    struct Flow {
        std::size_t swap = 0, j = 1;
        bool started = false;
        std::size_t fixing_grid = 0;
    };
    std::vector<std::vector<Flow>> flows;  // [k - 1]
    std::vector<std::vector<double>> accrual;  // [swap][l] = T_l - T_{l-1} (l >= 1)
};

inline Plan make_plan(const Config& c)
{
    Plan pl;
    const auto& a = c.mean_reversions;
    pl.F = n_factors(c);
    pl.K = c.dates.size();
    const std::size_t F = pl.F, K = pl.K;
    pl.grid.assign(K + 1, 0.0);
    std::copy(c.dates.begin(), c.dates.end(), pl.grid.begin() + 1);

    pl.mats = {horizon(c)};
    const auto mat_index = [&](double T) {
        for (std::size_t m = 0; m < pl.mats.size(); ++m)
            if (std::fabs(pl.mats[m] - T) < detail::time_eps) return m;
        pl.mats.push_back(T);
        return pl.mats.size() - 1;
    };
    pl.sched.resize(c.swaps.size());
    for (std::size_t s = 0; s < c.swaps.size(); ++s) {
        const Swap& sw = c.swaps[s];
        Plan::Schedule& sc = pl.sched[s];
        const std::size_t n = std::max<long>(1, std::lround((sw.maturity - sw.start) / sw.period));
        for (std::size_t l = 0; l <= n; ++l) {
            const double T = l == n ? sw.maturity : sw.start + double(l) * sw.period;
            sc.time.push_back(T);
            sc.mat.push_back(mat_index(T));
        }
        for (std::size_t l = 1; l <= n; ++l) {
            std::size_t g = 0;
            while (g + 1 <= K && pl.grid[g + 1] <= sc.time[l - 1] + detail::time_eps) ++g;
            sc.fixing.push_back(g);
        }
    }
    pl.M = pl.mats.size();
    const std::size_t M = pl.M;

    pl.decay.resize(K * F);
    for (std::size_t k = 0; k < K; ++k)
        for (std::size_t i = 0; i < F; ++i)
            pl.decay[k * F + i] = std::exp(-a[i] * (pl.grid[k + 1] - pl.grid[k]));

    pl.B.assign((K + 1) * M * F, 0.0);
    pl.alive.assign((K + 1) * M, 0);
    for (std::size_t g = 0; g <= K; ++g)
        for (std::size_t m = 0; m < M; ++m) {
            const double tau = pl.mats[m] - pl.grid[g];
            if (tau <= detail::time_eps) continue;
            pl.alive[g * M + m] = 1;
            for (std::size_t i = 0; i < F; ++i) pl.B[(g * M + m) * F + i] = -std::expm1(-a[i] * tau) / a[i];
        }

    // V(tau) = sum_ij rho_ij s_i s_j v_ij(tau) (see the file comment).
    const auto v = [&](std::size_t i, std::size_t j, double tau) {
        const double s = a[i] + a[j];
        return (tau + std::expm1(-a[i] * tau) / a[i] + std::expm1(-a[j] * tau) / a[j] - std::expm1(-s * tau) / s) /
               (a[i] * a[j]);
    };
    const double Ts = pl.mats[0];
    pl.chol_coef.assign(K * F * F, 0.0);
    pl.drift_coef.assign(K * F * F, 0.0);
    for (std::size_t k = 0; k < K; ++k) {
        const double s0 = pl.grid[k], t = pl.grid[k + 1], dt = t - s0;
        for (std::size_t i = 0; i < F; ++i)
            for (std::size_t j = 0; j < F; ++j) {
                const double s = a[i] + a[j];
                pl.chol_coef[(k * F + i) * F + j] = -std::expm1(-s * dt) / s;
                pl.drift_coef[(k * F + i) * F + j] =
                    (-std::expm1(-a[i] * dt) / a[i] -
                     (std::exp(-a[j] * (Ts - t)) - std::exp(-a[j] * Ts - a[i] * t + s * s0)) / s) /
                    a[j];
            }
    }
    pl.A_coef.assign((K + 1) * M * F * F, 0.0);
    for (std::size_t g = 0; g <= K; ++g)
        for (std::size_t m = 0; m < M; ++m) {
            if (!pl.alive[g * M + m]) continue;
            const double t = pl.grid[g], T = pl.mats[m];
            for (std::size_t i = 0; i < F; ++i)
                for (std::size_t j = 0; j < F; ++j)
                    pl.A_coef[(g * M + m) * F * F + i * F + j] = 0.5 * (v(i, j, T - t) - v(i, j, T) + v(i, j, t));
        }

    pl.flows.assign(K, {});
    pl.accrual.resize(c.swaps.size());
    for (std::size_t s = 0; s < c.swaps.size(); ++s) {
        const Plan::Schedule& sc = pl.sched[s];
        pl.accrual[s].assign(sc.time.size(), 0.0);
        for (std::size_t l = 1; l < sc.time.size(); ++l) pl.accrual[s][l] = sc.time[l] - sc.time[l - 1];
    }
    for (std::size_t k = 1; k <= K; ++k) {
        const double t = pl.grid[k];
        for (std::size_t s = 0; s < c.swaps.size(); ++s) {
            const Plan::Schedule& sc = pl.sched[s];
            const std::size_t n = sc.time.size() - 1;
            if (t >= sc.time[n] - detail::time_eps) continue;  // all paid
            Plan::Flow f;
            f.swap = s;
            while (sc.time[f.j] <= t + detail::time_eps) ++f.j;  // first payment still to come
            f.started = t > sc.time[0] + detail::time_eps;
            if (f.started) f.fixing_grid = sc.fixing[f.j - 1];
            pl.flows[k - 1].push_back(f);
        }
    }
    return pl;
}

// Input-dependent values shared by all paths.
template <class Real>
struct Shared {
    std::vector<Real> drift;   // [k * F + i] = M_i(t_k, t_{k+1})
    std::vector<Real> chol;    // [(k * F + i) * F + j], j <= i: step Cholesky factor (lower)
    std::vector<Real> lnfac;   // [g * M + m] = ln(P(0, T_m) / P(0, t_g)) + A(t_g, T_m), if alive
    std::vector<Real> weight;  // [k - 1] = LGD P(0, T*) (S(t_{k-1}) - S(t_k)) / n_paths
    std::vector<Real> fixed;   // fixed rate of each swap
};

// The simulation part of Shared: step drifts and Cholesky factors.
template <class Real>
void make_steps(const Inputs<Real>& in, const Config& c, const Plan& pl, Shared<Real>& sh)
{
    using std::sqrt;
    (void)c;
    const std::size_t F = pl.F, K = pl.K;
    // rho_ij s_i s_j, shared by all steps.
    std::vector<Real> cov(F * F, Real(0.0));
    for (std::size_t i = 0; i < F; ++i)
        for (std::size_t j = 0; j < F; ++j) cov[i * F + j] = detail::rho(in, i, j) * in.vols[i] * in.vols[j];
    sh.drift.assign(K * F, Real(0.0));
    sh.chol.assign(K * F * F, Real(0.0));
    for (std::size_t k = 0; k < K; ++k) {
        const double* dc = &pl.drift_coef[k * F * F];
        const double* cc = &pl.chol_coef[k * F * F];
        for (std::size_t i = 0; i < F; ++i) {
            Real m(0.0);
            for (std::size_t j = 0; j < F; ++j) m = m + cov[i * F + j] * dc[i * F + j];
            sh.drift[k * F + i] = m;
        }
        // Lower Cholesky factor of the step covariance cov_ij * cc_ij.
        Real* L = &sh.chol[k * F * F];
        for (std::size_t i = 0; i < F; ++i)
            for (std::size_t j = 0; j <= i; ++j) {
                Real v = cov[i * F + j] * cc[i * F + j];
                for (std::size_t m = 0; m < j; ++m) v = v - L[i * F + m] * L[j * F + m];
                if (i == j)
                    L[i * F + i] = sqrt(v);
                else
                    L[i * F + j] = v / L[j * F + j];
            }
    }
}

template <class Real>
Shared<Real> make_shared(const Inputs<Real>& in, const Config& c, const Plan& pl)
{
    using std::exp;
    const std::size_t K = pl.K, M = pl.M;
    Shared<Real> sh;
    make_steps(in, c, pl, sh);

    std::vector<Real> lnP0(M);
    for (std::size_t m = 0; m < M; ++m) lnP0[m] = detail::log_discount(in, c, pl.mats[m]);
    const std::size_t F = pl.F;
    std::vector<Real> cov(F * F, Real(0.0));
    for (std::size_t i = 0; i < F; ++i)
        for (std::size_t j = 0; j < F; ++j) cov[i * F + j] = detail::rho(in, i, j) * in.vols[i] * in.vols[j];
    sh.lnfac.assign((K + 1) * M, Real(0.0));
    for (std::size_t g = 0; g <= K; ++g) {
        const Real lnP0t = detail::log_discount(in, c, pl.grid[g]);
        for (std::size_t m = 0; m < M; ++m) {
            if (!pl.alive[g * M + m]) continue;
            const double* ac = &pl.A_coef[(g * M + m) * F * F];
            Real A(0.0);
            for (std::size_t ij = 0; ij < F * F; ++ij) A = A + cov[ij] * ac[ij];
            sh.lnfac[g * M + m] = lnP0[m] - lnP0t + A;
        }
    }

    sh.fixed = in.fixed_rates;

    // CVA = LGD sum_k P(0, t_k) EE+(t_k) PD_k with
    // P(0, t_k) EE+(t_k) = P(0, T*) E^{T*}[V^+(t_k) / P(t_k, T*)].
    sh.weight.resize(K);
    const Real scale = in.lgd * exp(lnP0[0]) / double(c.n_paths);
    Real S_prev(1.0);
    for (std::size_t k = 1; k <= K; ++k) {
        const Real S = exp(-detail::cumulative_hazard(in, c, pl.grid[k]));
        sh.weight[k - 1] = scale * (S_prev - S);
        S_prev = S;
    }
    return sh;
}

// One path's factors xp[k * F + i] = x_i(t_k), k = 0..K, from its shocks
// zp[k * F + i] (step k, t_k -> t_{k+1}).
template <class Real, class Shock>
void simulate_path(const Shared<Real>& sh, const Plan& pl, const Shock* zp, Real* xp)
{
    const std::size_t F = pl.F, K = pl.K;
    for (std::size_t i = 0; i < F; ++i) xp[i] = Real(0.0);
    for (std::size_t k = 0; k < K; ++k) {
        const Real* prev = xp + k * F;
        Real* next = xp + (k + 1) * F;
        const Shock* zk = zp + k * F;
        const Real* L = &sh.chol[k * F * F];
        for (std::size_t i = 0; i < F; ++i) {
            Real shock = L[i * F] * zk[0];
            for (std::size_t j = 1; j <= i; ++j) shock = shock + L[i * F + j] * zk[j];
            next[i] = pl.decay[k * F + i] * prev[i] - sh.drift[k * F + i] + shock;
        }
    }
}

// Per-path scratch (factors, bonds, exposures), allocated once per
// evaluation and reused by every path. For a pathwise tape, release() ends
// the elements' lifetimes before the tape is reset (their slots belong to the
// discarded path) and acquire() makes fresh ones in the kept storage.
template <class Real>
struct PathWork {
    std::vector<Real> x, bonds, e;
    explicit PathWork(const Plan& pl) { acquire(pl); }
    void acquire(const Plan& pl)
    {
        x.resize((pl.K + 1) * pl.F);
        bonds.resize((pl.K + 1) * pl.M);
        e.resize(pl.K);
    }
    void release()
    {
        x.clear();
        bonds.clear();
        e.clear();
    }
};

// One path's deflated positive exposures w.e[k - 1] = V(t_k)^+ / P(t_k, T*),
// k = 1..K.
template <class Real, class Shock>
void path_deflated_exposure(const Shared<Real>& sh, const Plan& pl, const Config& c, const Shock* zp,
                            PathWork<Real>& w)
{
    using std::exp;
    const std::size_t F = pl.F, K = pl.K, M = pl.M;
    std::vector<Real>& x = w.x;
    simulate_path(sh, pl, zp, x.data());

    // P(t_g, T_m) = exp(lnfac - B . x(t_g)), 1 once T_m <= t_g.
    std::vector<Real>& bonds = w.bonds;
    for (std::size_t g = 0; g <= K; ++g) {
        const Real* xg = &x[g * F];
        for (std::size_t m = 0; m < M; ++m) {
            if (!pl.alive[g * M + m]) {
                bonds[g * M + m] = Real(1.0);
                continue;
            }
            const double* Bgm = &pl.B[(g * M + m) * F];
            Real e = sh.lnfac[g * M + m];
            for (std::size_t i = 0; i < F; ++i) e = e - Bgm[i] * xg[i];
            bonds[g * M + m] = exp(e);
        }
    }

    for (std::size_t k = 1; k <= K; ++k) {
        const Real* Pk = &bonds[k * M];
        Real v(0.0);
        for (const Plan::Flow& f : pl.flows[k - 1]) {
            const Swap& sw = c.swaps[f.swap];
            const Plan::Schedule& sc = pl.sched[f.swap];
            const std::vector<double>& acc = pl.accrual[f.swap];
            const std::size_t n = sc.time.size() - 1, j = f.j;
            Real flt;
            if (!f.started) {
                flt = Pk[sc.mat[0]] - Pk[sc.mat[n]];
            } else {
                const Real* Pf = &bonds[f.fixing_grid * M];
                const Real fixed_bond = Pf[sc.mat[j]] / Pf[sc.mat[j - 1]];
                flt = Pk[sc.mat[j]] / fixed_bond - Pk[sc.mat[n]];
            }
            Real annuity(0.0);
            for (std::size_t l = j; l <= n; ++l) annuity = annuity + acc[l] * Pk[sc.mat[l]];
            const Real value = sw.notional * (flt - sh.fixed[f.swap] * annuity);
            if (sw.payer)
                v = v + value;
            else
                v = v - value;
        }
        w.e[k - 1] = detail::positive_part(v) / Pk[0];
    }
}

// One path's CVA contribution: sum_k weight_k V(t_k)^+ / P(t_k, T*).
template <class Real, class Shock>
Real path_cva(const Shared<Real>& sh, const Plan& pl, const Config& c, const Shock* zp, PathWork<Real>& w)
{
    path_deflated_exposure(sh, pl, c, zp, w);
    Real v(0.0);
    for (std::size_t k = 0; k < pl.K; ++k) v = v + sh.weight[k] * w.e[k];
    return v;
}

// The same with its own scratch (for one-path recordings).
template <class Real, class Shock>
Real path_cva(const Shared<Real>& sh, const Plan& pl, const Config& c, const Shock* zp)
{
    PathWork<Real> w(pl);
    return path_cva(sh, pl, c, zp, w);
}

// Shared values as one flat vector (drift, lower Cholesky entries, lnfac of
// live bonds, weight, fixed) and back: the inputs of a one-path model.
inline std::size_t n_shared(const Plan& pl, const Config& c)
{
    std::size_t live = 0;
    for (unsigned char a : pl.alive) live += a;
    return pl.K * pl.F + pl.K * pl.F * (pl.F + 1) / 2 + live + pl.K + c.swaps.size();
}

template <class Real>
std::vector<Real> shared_to_vector(const Shared<Real>& sh, const Plan& pl)
{
    const std::size_t F = pl.F;
    std::vector<Real> v(sh.drift);
    for (std::size_t k = 0; k < pl.K; ++k)
        for (std::size_t i = 0; i < F; ++i)
            for (std::size_t j = 0; j <= i; ++j) v.push_back(sh.chol[(k * F + i) * F + j]);
    for (std::size_t gm = 0; gm < pl.alive.size(); ++gm)
        if (pl.alive[gm]) v.push_back(sh.lnfac[gm]);
    v.insert(v.end(), sh.weight.begin(), sh.weight.end());
    v.insert(v.end(), sh.fixed.begin(), sh.fixed.end());
    return v;
}

template <class Real>
Shared<Real> shared_from_vector(const std::vector<Real>& v, const Plan& pl, const Config& c)
{
    const std::size_t F = pl.F, K = pl.K;
    Shared<Real> sh;
    auto it = v.begin();
    sh.drift.assign(it, it + K * F);
    it += K * F;
    sh.chol.assign(K * F * F, Real(0.0));
    for (std::size_t k = 0; k < K; ++k)
        for (std::size_t i = 0; i < F; ++i)
            for (std::size_t j = 0; j <= i; ++j) sh.chol[(k * F + i) * F + j] = *it++;
    sh.lnfac.assign(pl.alive.size(), Real(0.0));
    for (std::size_t gm = 0; gm < pl.alive.size(); ++gm)
        if (pl.alive[gm]) sh.lnfac[gm] = *it++;
    sh.weight.assign(it, it + K);
    it += K;
    sh.fixed.assign(it, it + c.swaps.size());
    return sh;
}

// Factor paths x[(p * (K + 1) + k) * F + i] = x_i(t_k) on path p, k = 0..K,
// under the T*-forward measure, from the shocks z[(p * K + k) * F + i] of
// step k (t_k -> t_{k+1}).
template <class Real, class Shock>
std::vector<Real> simulate_factors(const Inputs<Real>& in, const Config& c, const std::vector<Shock>& z)
{
    const Plan pl = make_plan(c);
    Shared<Real> sh;
    make_steps(in, c, pl, sh);
    const std::size_t F = pl.F, K = pl.K;
    std::vector<Real> x(c.n_paths * (K + 1) * F);
    for (std::size_t p = 0; p < c.n_paths; ++p) simulate_path(sh, pl, &z[p * K * F], &x[p * (K + 1) * F]);
    return x;
}

// EE+(t_k), k = 1..K, of the portfolio in t_k money:
// E^Q[D(0, t_k) V(t_k)^+] / P(0, t_k) = P(0, T*) / P(0, t_k) E^{T*}[V^+ / P(t_k, T*)],
// the average over n_paths paths.
template <class Real, class Shock>
std::vector<Real> expected_exposure(const Inputs<Real>& in, const Config& c, const std::vector<Shock>& z)
{
    using std::exp;
    const Plan pl = make_plan(c);
    const Shared<Real> sh = make_shared(in, c, pl);
    const std::size_t K = pl.K, P = c.n_paths;
    std::vector<Real> sum(K, Real(0.0));
    PathWork<Real> w(pl);
    for (std::size_t p = 0; p < P; ++p) {
        path_deflated_exposure(sh, pl, c, &z[p * K * pl.F], w);
        for (std::size_t k = 0; k < K; ++k) sum[k] = sum[k] + w.e[k];
    }
    const Real lnP0T = detail::log_discount(in, c, pl.mats[0]);
    std::vector<Real> ee(K);
    for (std::size_t k = 1; k <= K; ++k)
        ee[k - 1] = exp(lnP0T - detail::log_discount(in, c, pl.grid[k])) * sum[k - 1] / double(P);
    return ee;
}

// Unilateral CVA: LGD * sum_k P(0, t_k) EE+(t_k) (S(t_{k-1}) - S(t_k)),
// evaluated as the sum over paths of path_cva. The Plan (configuration only)
// is built once in setup; the overload without it builds it.
template <class Real, class Shock>
Real cva(const Inputs<Real>& in, const Config& c, const Plan& pl, const std::vector<Shock>& z)
{
    const Shared<Real> sh = make_shared(in, c, pl);
    const std::size_t KF = pl.K * pl.F;
    PathWork<Real> w(pl);
    Real sum(0.0);
    for (std::size_t p = 0; p < c.n_paths; ++p) sum = sum + path_cva(sh, pl, c, &z[p * KF], w);
    return sum;
}

template <class Real, class Shock>
Real cva(const Inputs<Real>& in, const Config& c, const std::vector<Shock>& z)
{
    return cva(in, c, make_plan(c), z);
}

}  // namespace cva
