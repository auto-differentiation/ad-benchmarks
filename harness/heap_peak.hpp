// Peak heap memory of a piece of code, for the untimed memory pass.
//
// The counting is done by the allocation hooks in the shared library
// adb_heap_peak (harness/heap_peak_hooks.cpp), loaded with LD_PRELOAD. Without
// it nothing is counted and heap_peak_available() is false, so timed runs,
// which never preload it, carry no counter. This header has no link-time
// dependency on the hooks: it finds them with dlsym (link ${CMAKE_DL_LIBS}).

#pragma once

#include <dlfcn.h>

#include <cstdint>

namespace harness {

namespace detail {

struct HeapPeakHooks {
    std::uint64_t (*reset)() = nullptr;  // peak := current; returns current
    std::uint64_t (*peak)() = nullptr;   // peak since the last reset
};

inline const HeapPeakHooks& heap_peak_hooks()
{
    static const HeapPeakHooks hooks = [] {
        HeapPeakHooks h;
        h.reset = reinterpret_cast<std::uint64_t (*)()>(dlsym(RTLD_DEFAULT, "adb_heap_peak_reset"));
        h.peak = reinterpret_cast<std::uint64_t (*)()>(dlsym(RTLD_DEFAULT, "adb_heap_peak_get"));
        return h;
    }();
    return hooks;
}

}  // namespace detail

// True when the allocation hooks are loaded in this process.
inline bool heap_peak_available()
{
    const auto& h = detail::heap_peak_hooks();
    return h.reset && h.peak;
}

// Runs body() once and returns the peak heap bytes in use while it ran,
// above the level when it started (0 when the hooks are not loaded).
// Allocations are counted at their usable size (malloc_usable_size), by
// every thread and library in the process.
template <class F>
std::uint64_t peak_heap_bytes(F&& body)
{
    if (!heap_peak_available()) {
        body();
        return 0;
    }
    const auto& h = detail::heap_peak_hooks();
    const std::uint64_t start = h.reset();
    body();
    const std::uint64_t peak = h.peak();
    return peak > start ? peak - start : 0;
}

}  // namespace harness
