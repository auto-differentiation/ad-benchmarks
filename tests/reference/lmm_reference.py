#!/usr/bin/env python3
"""Independent reference for the W1 LMM swaption portfolio (tests/lmm).

Written from the model description (Giles & Glasserman, "Smoking Adjoints",
Risk 2006), not from the C++ kernel: single-factor log-normal LIBOR market
model, forwards L[0..N-1] with accrual delta, Euler steps on log L under the
spot measure for Nmat steps, time-homogeneous vols lambda[k] (rate i at step n
uses lambda[i - n - 1]), then a portfolio of payer swaptions expiring at
T = Nmat * delta, paying 100 * max(1 - B_m - K * S_m, 0), discounted to 0 with
the forwards L[0..Nmat-1] along the path. Price = mean over paths.

The normals are regenerated here exactly as harness::Rng does it
(std::mt19937_64, top 53 bits, Marsaglia polar), so the C++ and Python runs
use identical draws: z[p * Nmat + n] drives step n of path p.

Pure Python standard library, no numpy. Usage: python3 lmm_reference.py
"""

import math

MASK64 = (1 << 64) - 1


class MT19937_64:
    """std::mt19937_64 (C++11 [rand.predef])."""

    def __init__(self, seed):
        self.mt = [0] * 312
        self.mt[0] = seed & MASK64
        for i in range(1, 312):
            prev = self.mt[i - 1]
            self.mt[i] = (6364136223846793005 * (prev ^ (prev >> 62)) + i) & MASK64
        self.idx = 312

    def _twist(self):
        mt = self.mt
        for i in range(312):
            x = (mt[i] & 0xFFFFFFFF80000000) | (mt[(i + 1) % 312] & 0x7FFFFFFF)
            xa = x >> 1
            if x & 1:
                xa ^= 0xB5026F5AA96619E9
            mt[i] = mt[(i + 156) % 312] ^ xa
        self.idx = 0

    def __call__(self):
        if self.idx >= 312:
            self._twist()
        y = self.mt[self.idx]
        self.idx += 1
        y ^= (y >> 29) & 0x5555555555555555
        y ^= (y << 17) & 0x71D67FFFEDA60000
        y ^= (y << 37) & 0xFFF7EEE000000000
        y ^= y >> 43
        return y & MASK64


def normals(seed, n):
    eng = MT19937_64(seed)

    def uniform():
        return float(eng() >> 11) * 2.0 ** -53

    z = []
    while len(z) < n:
        while True:
            u = 2.0 * uniform() - 1.0
            v = 2.0 * uniform() - 1.0
            s = u * u + v * v
            if not (s >= 1.0 or s == 0.0):
                break
        f = math.sqrt(-2.0 * math.log(s) / s)
        z.append(u * f)
        if len(z) < n:
            z.append(v * f)
    return z


def price(N, Nmat, delta, L0, lam, lengths, strikes, z, paths):
    total = 0.0
    sd = math.sqrt(delta)
    for p in range(paths):
        L = list(L0)
        for n in range(Nmat):
            dw = sd * z[p * Nmat + n]
            drift = 0.0
            for i in range(n + 1, N):
                v = lam[i - n - 1]
                drift += delta * v * L[i] / (1.0 + delta * L[i])
                L[i] *= math.exp(delta * v * drift - 0.5 * v * v * delta + v * dw)
        # bond prices B[m] = P(T, T_{m+1}) / and annuity S[m], m >= Nmat
        B, S = {}, {}
        b, s = 1.0, 0.0
        for m in range(Nmat, N):
            b /= 1.0 + delta * L[m]
            s += delta * b
            B[m], S[m] = b, s
        payoff = 0.0
        for length, K in zip(lengths, strikes):
            m = Nmat + length - 1
            payoff += 100.0 * max(1.0 - B[m] - K * S[m], 0.0)
        disc = 1.0
        for n in range(Nmat):
            disc /= 1.0 + delta * L[n]
        total += payoff * disc
    return total / paths


def small_case():
    N, Nmat, delta = 16, 8, 0.25
    L0 = [0.05 + 0.0005 * i for i in range(N)]
    lam = [0.2 - 0.002 * k for k in range(N - 1)]
    lengths, strikes = [], []
    for length in (2, 4, 6, 8):
        for K in (0.045, 0.05, 0.055):
            lengths.append(length)
            strikes.append(K)
    return N, Nmat, delta, L0, lam, lengths, strikes


def main():
    # Self-check against the C++ standard: the 10000th output of a
    # default-constructed std::mt19937_64 (seed 5489).
    eng = MT19937_64(5489)
    for _ in range(9999):
        eng()
    assert eng() == 9981545732273789042, "mt19937_64 mismatch"

    seed, paths = 42, 4096
    N, Nmat, delta, L0, lam, lengths, strikes = small_case()
    z = normals(seed, paths * Nmat)
    print("first normals:", repr(z[0]), repr(z[1]))
    v = price(N, Nmat, delta, L0, lam, lengths, strikes, z, paths)
    print(f"small case: N={N} Nmat={Nmat} seed={seed} paths={paths} price={v!r}")


if __name__ == "__main__":
    main()
