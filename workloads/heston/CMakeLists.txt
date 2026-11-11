# W2: Heston option-book Monte Carlo benchmark program.
add_executable(bench_heston bench_heston.cpp)
target_include_directories(bench_heston PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(bench_heston PRIVATE adb::versions)
adb_link_ad_libraries(bench_heston PRIVATE ${ADB_AD_LIBRARIES})
target_link_libraries(bench_heston PRIVATE ${CMAKE_DL_LIBS})
# Runs each column in its own processes (harness/columns.hpp); the memory pass
# preloads the adb_heap_peak hooks.
target_compile_definitions(bench_heston PRIVATE ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_heston adb_heap_peak)
