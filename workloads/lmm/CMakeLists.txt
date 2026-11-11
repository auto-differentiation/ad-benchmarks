# W1: LMM swaption portfolio. adb_lmm_bench links every enabled AD library.
add_library(adb_lmm_bench STATIC bench.cpp)
target_include_directories(adb_lmm_bench PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(adb_lmm_bench PUBLIC adb::versions ${CMAKE_DL_LIBS})
adb_link_ad_libraries(adb_lmm_bench PUBLIC ${ADB_AD_LIBRARIES})

# Runs each column in its own processes (harness/columns.hpp); the memory pass
# preloads the adb_heap_peak hooks.
add_executable(bench_lmm bench_main.cpp)
target_link_libraries(bench_lmm PRIVATE adb_lmm_bench)
target_compile_definitions(bench_lmm PRIVATE ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_lmm adb_heap_peak)
