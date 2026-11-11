// Central finite-difference gradient, the reference for AD results.

#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace harness {

// g[i] = (f(x + h e_i) - f(x - h e_i)) / (2h), with the same absolute step h
// for every component. Calls f exactly 2 * x.size() times; x is not modified.
template <class F>
std::vector<double> fd_gradient(F&& f, const std::vector<double>& x, double h)
{
    // g is sized before xs is copied: the other order makes GCC 15 report a
    // false -Wstringop-overflow in the vector constructor.
    std::vector<double> g(x.size());
    std::vector<double> xs = x;
    for (std::size_t i = 0; i < x.size(); ++i) {
        xs[i] = x[i] + h;
        const double up = f(static_cast<const std::vector<double>&>(xs));
        xs[i] = x[i] - h;
        const double down = f(static_cast<const std::vector<double>&>(xs));
        xs[i] = x[i];
        g[i] = (up - down) / (2.0 * h);
    }
    return g;
}

// library_version of a central finite-difference column with step h, e.g.
// "central, h = 1e-6".
inline std::string fd_version(double h)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", h);
    std::string s = buf;
    const std::size_t e = s.find("e-0");  // %g writes 1e-06
    if (e != std::string::npos) s.erase(e + 2, 1);
    return "central, h = " + s;
}

}  // namespace harness
