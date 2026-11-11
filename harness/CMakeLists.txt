# Allocation hooks for harness/heap_peak.hpp, preloaded (LD_PRELOAD) only for
# the untimed memory pass.
add_library(adb_heap_peak SHARED heap_peak_hooks.cpp)
