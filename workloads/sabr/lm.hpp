// W4 calibration: a small, plain Levenberg-Marquardt for the SABR residuals.
//
// Each iteration evaluates the Jacobian exactly once, at the current accepted
// x, then solves the damped normal equations
//     (J^T J + lambda diag(J^T J)) dx = -J^T r
// for trial steps until one lowers the cost (lambda shrinks on success and
// grows on failure). A trial step is rejected without evaluating the cost if
// it leaves the domain (alpha > 0, |rho| < 1, nu > 0); a non-finite cost is
// rejected as well. Residuals are plain double, sabr::residuals<double>, with the quote terms
// computed once per calibration.

#pragma once

#include "workloads/sabr/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sabr {

struct LmOptions {
    std::size_t max_iterations = 100;  // Jacobian evaluations
    double lambda0 = 1e-3;             // initial damping
    double xtol = 1e-12;   // converged when |dx| <= xtol * (xtol + |x|) ...
    double ftol = 1e-30;   // ... or when cost <= ftol
    std::size_t max_trials = 30;  // trial steps per iteration
};

struct LmResult {
    std::vector<double> x;
    std::size_t iterations = 0;  // Jacobian evaluations
    bool converged = false;
    double cost = 0.0;  // 0.5 * |r|^2 at x
};

namespace detail {

inline double cost_of(const std::vector<double>& r)
{
    double c = 0.0;
    for (double v : r) c += v * v;
    return 0.5 * c;
}

inline bool in_domain(const std::vector<double>& x)
{
    for (std::size_t e = 0; 3 * e + 2 < x.size(); ++e)
        if (!(x[3 * e] > 0.0 && std::fabs(x[3 * e + 1]) < 1.0 && x[3 * e + 2] > 0.0)) return false;
    return true;
}

inline double norm(const std::vector<double>& v)
{
    double s = 0.0;
    for (double a : v) s += a * a;
    return std::sqrt(s);
}

// Solves A y = b for symmetric positive definite A (n x n, row-major) by
// Cholesky; false if A is not numerically positive definite.
inline bool cholesky_solve(std::vector<double> A, std::vector<double>& b, std::size_t n)
{
    for (std::size_t j = 0; j < n; ++j) {
        double d = A[j * n + j];
        for (std::size_t k = 0; k < j; ++k) d -= A[j * n + k] * A[j * n + k];
        if (!(d > 0.0)) return false;
        d = std::sqrt(d);
        A[j * n + j] = d;
        for (std::size_t i = j + 1; i < n; ++i) {
            double s = A[i * n + j];
            for (std::size_t k = 0; k < j; ++k) s -= A[i * n + k] * A[j * n + k];
            A[i * n + j] = s / d;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {  // L y = b
        double s = b[i];
        for (std::size_t k = 0; k < i; ++k) s -= A[i * n + k] * b[k];
        b[i] = s / A[i * n + i];
    }
    for (std::size_t i = n; i-- > 0;) {  // L^T x = y
        double s = b[i];
        for (std::size_t k = i + 1; k < n; ++k) s -= A[k * n + i] * b[k];
        b[i] = s / A[i * n + i];
    }
    return true;
}

}  // namespace detail

template <class Jacobian>
LmResult levenberg_marquardt(const Market& m, std::vector<double> x0, Jacobian&& jac,
                             const LmOptions& opts = {})
{
    LmResult res;
    res.x = std::move(x0);
    const std::size_t n = res.x.size();
    const std::vector<QuoteTerms> terms = quote_terms(m);  // once per calibration
    std::vector<double> r = residuals(m, terms, res.x);
    res.cost = detail::cost_of(r);
    double lambda = opts.lambda0;

    while (res.iterations < opts.max_iterations && !res.converged) {
        if (res.cost <= opts.ftol) {
            res.converged = true;
            break;
        }
        const std::vector<double> J = jac(static_cast<const std::vector<double>&>(res.x));
        ++res.iterations;

        // Normal equations: JtJ (n x n) and g = J^T r.
        const std::size_t nr = r.size();
        std::vector<double> JtJ(n * n, 0.0), g(n, 0.0);
        for (std::size_t i = 0; i < nr; ++i) {
            const double* row = &J[i * n];
            for (std::size_t a = 0; a < n; ++a) {
                g[a] += row[a] * r[i];
                for (std::size_t b = 0; b < n; ++b) JtJ[a * n + b] += row[a] * row[b];
            }
        }

        bool accepted = false;
        for (std::size_t t = 0; t < opts.max_trials && !accepted; ++t) {
            std::vector<double> A = JtJ;
            for (std::size_t a = 0; a < n; ++a) A[a * n + a] += lambda * std::max(JtJ[a * n + a], 1e-300);
            std::vector<double> dx(n);
            for (std::size_t a = 0; a < n; ++a) dx[a] = -g[a];
            if (!detail::cholesky_solve(A, dx, n)) {
                lambda *= 10.0;
                continue;
            }
            std::vector<double> xt = res.x;
            for (std::size_t a = 0; a < n; ++a) xt[a] += dx[a];
            if (!detail::in_domain(xt)) {
                lambda *= 10.0;
                continue;
            }
            std::vector<double> rt = residuals(m, terms, xt);
            const double ct = detail::cost_of(rt);
            if (!std::isfinite(ct) || !(ct < res.cost)) {
                lambda *= 10.0;
                continue;
            }
            accepted = true;
            const double step = detail::norm(dx);
            const double xnorm = detail::norm(res.x);
            res.x = std::move(xt);
            r = std::move(rt);
            res.cost = ct;
            lambda = std::max(lambda / 10.0, 1e-12);
            if (step <= opts.xtol * (opts.xtol + xnorm) || res.cost <= opts.ftol) res.converged = true;
        }
        if (!accepted) break;  // no trial step lowers the cost: stuck
    }
    return res;
}

}  // namespace sabr
