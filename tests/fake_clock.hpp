// Test clock for Timer: never reads real time. "now" only moves when the code
// under test calls FakeClock::advance, so each timed call takes exactly the
// scripted duration, however many times the Timer calls now().

#pragma once

#include <chrono>

namespace testing_support {

struct FakeClock {
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<FakeClock>;
    static constexpr bool is_steady = true;

    static time_point now() { return current; }
    static void advance(duration d) { current += d; }
    static void reset() { current = time_point{}; }

    static inline time_point current{};
};

}  // namespace testing_support
