// W4 kernel: SABR smile, written once as a template on the number type.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace sabr {

// Parameter-independent terms of one quote (F, K, T, beta fixed), computed
// once at set-up so the kernel below does only the parameter-dependent work.
struct QuoteTerms {
    bool atm = false;    // F == K: eq. (2.18), no z / x(z)
    double T = 0.0;      // time to expiry
    double fk_h = 0.0;   // (FK)^((1-beta)/2)
    double c_aa = 0.0;   // (1-beta)^2 / 24 / (FK)^(1-beta), coefficient of alpha^2 in the correction
    double c_rna = 0.0;  // beta / (4 (FK)^((1-beta)/2)), coefficient of rho nu alpha
    double denom = 0.0;  // fk_h (1 + (1-beta)^2/24 ln^2(F/K) + (1-beta)^4/1920 ln^4(F/K))
    double zf = 0.0;     // fk_h ln(F/K), so z = nu / alpha * zf
};

inline QuoteTerms quote_terms(double F, double K, double T, double beta)
{
    QuoteTerms q;
    const double omb = 1.0 - beta;
    q.atm = F == K;
    q.T = T;
    q.fk_h = std::pow(F * K, 0.5 * omb);
    q.c_aa = omb * omb / 24.0 / (q.fk_h * q.fk_h);
    q.c_rna = beta / (4.0 * q.fk_h);
    const double lfk = q.atm ? 0.0 : std::log(F / K);
    const double lfk2 = lfk * lfk;
    q.denom = q.fk_h * (1.0 + omb * omb / 24.0 * lfk2 + omb * omb * omb * omb / 1920.0 * lfk2 * lfk2);
    q.zf = q.fk_h * lfk;
    return q;
}

// Black (lognormal) implied vol of the SABR model, Hagan et al., "Managing
// Smile Risk", Wilmott Magazine (2002), eq. (2.17a); eq. (2.18) at F == K.
// alpha, rho, nu are the calibrated parameters; the quote's data enter
// through its precomputed QuoteTerms.
//
// x(z) = ln((sqrt(1 - 2 rho z + z^2) + z - rho) / (1 - rho)) is evaluated as
// log1p(z q) with q = (1 + (z - 2 rho) / (sqrt(1 - 2 rho z + z^2) + 1)) / (1 - rho),
// which has no cancellation for small |z|, so z / x(z) keeps full precision
// near F == K without a branch on the parameters.
template <class Real>
Real hagan_vol(const QuoteTerms& t, const Real& alpha, const Real& rho, const Real& nu)
{
    using std::log1p;
    using std::sqrt;

    const Real corr = 1.0 + (t.c_aa * alpha * alpha + t.c_rna * rho * nu * alpha
                             + (2.0 - 3.0 * rho * rho) / 24.0 * nu * nu)
                                * t.T;
    if (t.atm) return alpha / t.denom * corr;

    const Real z = nu / alpha * t.zf;
    const Real s = sqrt(1.0 - 2.0 * rho * z + z * z);
    const Real q = (1.0 + (z - 2.0 * rho) / (s + 1.0)) / (1.0 - rho);
    const Real xz = log1p(z * q);
    return alpha / t.denom * (z / xz) * corr;
}

// The same vol from the raw data (computes the QuoteTerms on the fly).
template <class Real>
Real hagan_vol(double F, double K, double T, const Real& alpha, double beta, const Real& rho,
               const Real& nu)
{
    return hagan_vol(quote_terms(F, K, T, beta), alpha, rho, nu);
}

// Fixed market data of one calibration; nothing in here is calibrated.
struct Market {
    double beta = 0.5;                // same for every expiry
    std::vector<double> T, F;         // per expiry: time to expiry, forward
    std::vector<std::size_t> expiry;  // per quote: index into T, F
    std::vector<double> K, quote;     // per quote: strike, market Black vol
};

// Parameter-independent terms of every quote of m, in quote order.
inline std::vector<QuoteTerms> quote_terms(const Market& m)
{
    std::vector<QuoteTerms> t;
    t.reserve(m.K.size());
    for (std::size_t i = 0; i < m.K.size(); ++i) {
        const std::size_t e = m.expiry[i];
        t.push_back(quote_terms(m.F[e], m.K[i], m.T[e], m.beta));
    }
    return t;
}

// Residuals r[i] = model vol - market vol of quote i, with parameters
// x[3e] = alpha_e, x[3e+1] = rho_e, x[3e+2] = nu_e of expiry e = expiry[i];
// terms = quote_terms(m), computed once at set-up.
template <class Real>
std::vector<Real> residuals(const Market& m, const std::vector<QuoteTerms>& terms,
                            const std::vector<Real>& x)
{
    std::vector<Real> r;
    r.reserve(m.K.size());
    for (std::size_t i = 0; i < m.K.size(); ++i) {
        const std::size_t e = m.expiry[i];
        r.push_back(hagan_vol(terms[i], x[3 * e], x[3 * e + 1], x[3 * e + 2]) - m.quote[i]);
    }
    return r;
}

// The same, computing the quote terms on the fly (for one-off use; the
// benchmarked paths pass precomputed terms).
template <class Real>
std::vector<Real> residuals(const Market& m, const std::vector<Real>& x)
{
    return residuals(m, quote_terms(m), x);
}

// True parameters of standard_market(), 3 per expiry (alpha, rho, nu).
inline std::vector<double> standard_params()
{
    return {0.045, -0.20, 0.60,   // 0.5y
            0.047, -0.25, 0.50,   // 1y
            0.050, -0.30, 0.40,   // 2y
            0.048, -0.35, 0.35,   // 5y
            0.046, -0.40, 0.30};  // 10y
}

// 5 expiries x 20 strikes, beta = 0.5; strikes at log-moneyness
// ln(K/F) = -0.95, -0.85, ..., 0.95; quotes are hagan_vol at
// standard_params(), without noise.
inline Market standard_market()
{
    Market m;
    m.beta = 0.5;
    m.T = {0.5, 1.0, 2.0, 5.0, 10.0};
    m.F = {0.030, 0.032, 0.035, 0.037, 0.040};
    for (std::size_t e = 0; e < m.T.size(); ++e) {
        for (int j = 0; j < 20; ++j) {
            m.expiry.push_back(e);
            m.K.push_back(m.F[e] * std::exp(-0.95 + 0.1 * j));
        }
    }
    m.quote.assign(m.K.size(), 0.0);
    m.quote = residuals(m, standard_params());  // quote is still 0 here
    return m;
}

}  // namespace sabr
