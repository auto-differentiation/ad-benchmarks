// Peak heap memory, measured the same way for every column and every track.
//
// Contract checked here (namespace harness, harness/heap_peak.hpp):
//   bool heap_peak_available();
//     - true when the allocation hooks are loaded in this process.
//   template <class F> std::uint64_t peak_heap_bytes(F&& body);
//     - runs body() once; returns the peak of heap bytes in use while it ran,
//       above the level when it started. Every allocation in the process
//       counts (malloc, calloc, realloc, posix_memalign, aligned_alloc,
//       operator new), whichever library makes it.
//   The hooks live in the shared library target adb_heap_peak (harness/),
//   loaded with LD_PRELOAD only for the untimed memory pass, so timed runs
//   carry no counter at all. ctest preloads it for this test.

#include "harness/heap_peak.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
// Keeps the compiler from eliding allocations whose memory is never read.
void* volatile sink = nullptr;
}  // namespace

// The counter sees operator new and the malloc family, and only the peak
// above the starting level.
TEST(HeapPeak, CounterSeesEveryAllocator)
{
    ASSERT_TRUE(harness::heap_peak_available()) << "run with LD_PRELOAD of adb_heap_peak";
    constexpr std::uint64_t mb = 1u << 20;
    std::vector<char> already(8 * mb, 1);  // live before: not counted

    const std::uint64_t by_new = harness::peak_heap_bytes([] {
        std::vector<char> a(10 * mb, 1);
        std::vector<char> b(5 * mb, 2);  // both live: peak 15 MB
        sink = a.data();
        sink = b.data();
    });
    EXPECT_GE(by_new, 15 * mb);
    EXPECT_LT(by_new, 15 * mb + mb);

    const std::uint64_t by_malloc = harness::peak_heap_bytes([] {
        void* p = std::malloc(3 * mb);
        void* q = nullptr;
        ASSERT_EQ(posix_memalign(&q, 64, 4 * mb), 0);
        sink = p;
        sink = q;
        std::free(p);
        std::free(q);
    });
    EXPECT_GE(by_malloc, 7 * mb);
    EXPECT_LT(by_malloc, 7 * mb + mb);
    EXPECT_EQ(already[0], 1);
}
