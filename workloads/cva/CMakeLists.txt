# W3: CVA benchmark. Runs each column in its own processes
# (harness/columns.hpp); the memory pass preloads the adb_heap_peak hooks.
add_executable(bench_cva bench_main.cpp)
target_include_directories(bench_cva PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(bench_cva PRIVATE adb::versions ${CMAKE_DL_LIBS})
adb_link_ad_libraries(bench_cva PRIVATE ${ADB_AD_LIBRARIES})
target_compile_definitions(bench_cva PRIVATE ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_cva adb_heap_peak)
