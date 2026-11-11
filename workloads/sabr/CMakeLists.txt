# W4: SABR smile calibration benchmark.

add_library(sabr_bench STATIC bench.cpp)
target_include_directories(sabr_bench PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(sabr_bench PUBLIC adb::versions ${CMAKE_DL_LIBS})
adb_link_ad_libraries(sabr_bench PUBLIC ${ADB_AD_LIBRARIES})

# Runs each column in its own processes (harness/columns.hpp); the memory pass
# preloads the adb_heap_peak hooks.
add_executable(bench_sabr bench_main.cpp)
target_link_libraries(bench_sabr PRIVATE sabr_bench)
target_compile_definitions(bench_sabr PRIVATE ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_sabr adb_heap_peak)
