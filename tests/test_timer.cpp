// H2: Timer runs warmup + N iterations and reports median and IQR.
//
// The clock is injected. FakeClock never reads real time: its "now" only moves
// when the timed body calls FakeClock::advance, so each iteration takes exactly
// the scripted duration, however many times the Timer calls now().
//
// Contract checked here:
//   harness::Timer<Clock> t(warmup, iters);
//   harness::TimingStats s = t.run(body);
//   - body is called warmup + iters times, warmup calls first;
//   - only the last iters calls are measured, one sample per call;
//   - s.iterations == iters;
//   - s.median and s.iqr are in seconds (double);
//   - quantiles use linear interpolation between order statistics
//     (numpy's default): for sorted x[0..n-1] and p in [0,1],
//     h = (n-1)*p, q(p) = x[floor(h)] + (h - floor(h))*(x[floor(h)+1] - x[floor(h)]);
//     median = q(0.5), iqr = q(0.75) - q(0.25).

#include "harness/timer.hpp"

#include "fake_clock.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace {

using testing_support::FakeClock;

using std::chrono::milliseconds;

// Runs a Timer whose i-th body call takes durations[i] on the fake clock.
// Returns the stats; *calls receives the number of body calls.
harness::TimingStats run_scripted(std::size_t warmup, std::size_t iters,
                                  const std::vector<milliseconds>& durations,
                                  std::size_t* calls)
{
    FakeClock::reset();
    std::size_t i = 0;
    harness::Timer<FakeClock> t(warmup, iters);
    harness::TimingStats s = t.run([&] {
        if (i < durations.size()) FakeClock::advance(durations[i]);
        ++i;
    });
    *calls = i;
    return s;
}

constexpr double tol = 1e-12;

}  // namespace

TEST(Timer, OddCountMedianAndIqr)
{
    // Two warmup calls that take far longer than the rest: counting them
    // would move both the median and the IQR.
    const std::vector<milliseconds> d = {milliseconds(1000), milliseconds(1000),
                                         milliseconds(5), milliseconds(1), milliseconds(4),
                                         milliseconds(2), milliseconds(3)};
    std::size_t calls = 0;
    const harness::TimingStats s = run_scripted(2, 5, d, &calls);

    EXPECT_EQ(calls, 7u);  // warmup + iters
    EXPECT_EQ(s.iterations, 5u);
    // sorted 1,2,3,4,5 ms: median 3, q25 = 2, q75 = 4
    EXPECT_NEAR(s.median, 0.003, tol);
    EXPECT_NEAR(s.iqr, 0.002, tol);
}

TEST(Timer, EvenCountMedianAndIqrInterpolate)
{
    const std::vector<milliseconds> d = {milliseconds(1000), milliseconds(4), milliseconds(1),
                                         milliseconds(3), milliseconds(2)};
    std::size_t calls = 0;
    const harness::TimingStats s = run_scripted(1, 4, d, &calls);

    EXPECT_EQ(calls, 5u);
    EXPECT_EQ(s.iterations, 4u);
    // sorted 1,2,3,4 ms: median 2.5, q25 = 1.75 (h=0.75), q75 = 3.25 (h=2.25)
    EXPECT_NEAR(s.median, 0.0025, tol);
    EXPECT_NEAR(s.iqr, 0.0015, tol);
}

TEST(Timer, NoWarmup)
{
    const std::vector<milliseconds> d = {milliseconds(7), milliseconds(1), milliseconds(10)};
    std::size_t calls = 0;
    const harness::TimingStats s = run_scripted(0, 3, d, &calls);

    EXPECT_EQ(calls, 3u);
    EXPECT_EQ(s.iterations, 3u);
    // sorted 1,7,10 ms: median 7, q25 = 4 (h=0.5), q75 = 8.5 (h=1.5)
    EXPECT_NEAR(s.median, 0.007, tol);
    EXPECT_NEAR(s.iqr, 0.0045, tol);
}
