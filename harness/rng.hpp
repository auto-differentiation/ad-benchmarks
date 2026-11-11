// Reproducible standard normal draws for the benchmark harness.
//
// Every step is fully specified so the same seed gives the same numbers on
// any compiler and standard library:
//   - generator: std::mt19937_64 (its output sequence is fixed by the C++
//     standard, unlike the std:: distributions);
//   - uniforms:  top 53 bits of each 64-bit word, scaled to [0, 1);
//   - normals:   Marsaglia polar method, written out below. It needs only
//     sqrt (exactly rounded under IEEE 754) and one std::log per pair.
//     std::normal_distribution is not used: its algorithm is
//     implementation-defined.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace harness {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}

    // n independent N(0, 1) draws. Each call continues the stream.
    std::vector<double> normals(std::size_t n)
    {
        std::vector<double> z;
        z.reserve(n);
        while (z.size() < n) {
            double u, v, s;
            do {
                u = 2.0 * uniform() - 1.0;
                v = 2.0 * uniform() - 1.0;
                s = u * u + v * v;
            } while (s >= 1.0 || s == 0.0);
            const double f = std::sqrt(-2.0 * std::log(s) / s);
            z.push_back(u * f);
            if (z.size() < n) z.push_back(v * f);
        }
        return z;
    }

private:
    // Uniform on [0, 1) with 53 random bits.
    double uniform() { return static_cast<double>(engine_() >> 11) * 0x1.0p-53; }

    std::mt19937_64 engine_;
};

}  // namespace harness
