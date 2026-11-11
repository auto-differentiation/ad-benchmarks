#!/usr/bin/env python3
"""Reference values for tests/sabr/test_hagan.cpp.

Independent implementation of the Hagan et al. (2002) lognormal SABR
implied-volatility approximation, "Managing Smile Risk", Wilmott Magazine,
eq. (2.17a), with eq. (2.18) at F = K, evaluated in 50-digit decimal
arithmetic (standard library only). Prints C++ initialiser rows
{F, K, T, alpha, beta, rho, nu, vol}.
"""
from decimal import Decimal as D, getcontext

getcontext().prec = 50


def pw(x, p):
    return (p * x.ln()).exp()


def hagan(F, K, T, a, b, r, n):
    F, K, T, a, b, r, n = map(D, (F, K, T, a, b, r, n))
    one = D(1)
    omb = one - b
    fk = F * K
    fk_h = pw(fk, omb / 2)  # (FK)^((1-beta)/2)
    corr = one + (omb**2 / 24 * a**2 / pw(fk, omb)
                  + r * b * n * a / (4 * fk_h)
                  + (2 - 3 * r**2) / 24 * n**2) * T
    if F == K:
        return a / pw(F, omb) * corr
    lfk = (F / K).ln()
    denom = fk_h * (one + omb**2 / 24 * lfk**2 + omb**4 / 1920 * lfk**4)
    z = n / a * fk_h * lfk
    xz = (((one - 2 * r * z + z**2).sqrt() + z - r) / (one - r)).ln()
    return a / denom * (z / xz) * corr


CASES = [
    # beta = 0.5, rates-like smile, negative rho: ATM, wings, near-ATM
    (0.04, K, 2.0, 0.04, 0.5, -0.3, 0.5)
    for K in (0.01, 0.02, 0.03, 0.04, 0.05, 0.08, 0.12)
] + [
    (0.04, 0.04 * (1 + 1e-7), 2.0, 0.04, 0.5, -0.3, 0.5),
    (0.04, 0.04 * (1 - 1e-7), 2.0, 0.04, 0.5, -0.3, 0.5),
] + [
    # beta = 1 (lognormal backbone), positive rho, high vol of vol
    (100.0, K, 1.0, 0.25, 1.0, 0.4, 0.8) for K in (50.0, 80.0, 100.0, 120.0, 200.0)
] + [
    # beta = 0 (normal backbone), long expiry
    (0.03, K, 5.0, 0.006, 0.0, 0.1, 0.3) for K in (0.005, 0.03, 0.06)
]

if __name__ == "__main__":
    for c in CASES:
        # repr() of the float inputs, so the C++ literals are the same doubles
        args = ", ".join(repr(float(x)) for x in c)
        print(f"    {{{args}, {float(hagan(*c))!r}}},")
