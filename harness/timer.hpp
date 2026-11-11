// Wall-clock timing of a benchmark body: warmup calls, then one sample per
// measured call, summarised by median and interquartile range.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

namespace harness {

struct TimingStats {
    std::size_t iterations = 0;
    double median = 0.0;  // seconds
    double iqr = 0.0;     // seconds, q(0.75) - q(0.25)
};

// Quantile p of sorted x by linear interpolation between order statistics
// (numpy's default): h = (n-1)p, q = x[floor h] + (h - floor h)(x[floor h + 1] - x[floor h]).
inline double quantile_sorted(const std::vector<double>& x, double p)
{
    const double h = static_cast<double>(x.size() - 1) * p;
    const std::size_t lo = static_cast<std::size_t>(std::floor(h));
    if (lo + 1 >= x.size()) return x[lo];
    return x[lo] + (h - static_cast<double>(lo)) * (x[lo + 1] - x[lo]);
}

template <class Clock = std::chrono::steady_clock>
class Timer {
public:
    Timer(std::size_t warmup, std::size_t iters) : warmup_(warmup), iters_(iters) {}

    template <class Body>
    TimingStats run(Body&& body) const
    {
        for (std::size_t i = 0; i < warmup_; ++i) body();

        std::vector<double> samples;
        samples.reserve(iters_);
        for (std::size_t i = 0; i < iters_; ++i) {
            const auto t0 = Clock::now();
            body();
            const auto t1 = Clock::now();
            samples.push_back(std::chrono::duration<double>(t1 - t0).count());
        }

        TimingStats s;
        s.iterations = iters_;
        if (samples.empty()) return s;
        std::sort(samples.begin(), samples.end());
        s.median = quantile_sorted(samples, 0.5);
        s.iqr = quantile_sorted(samples, 0.75) - quantile_sorted(samples, 0.25);
        return s;
    }

private:
    std::size_t warmup_;
    std::size_t iters_;
};

}  // namespace harness
