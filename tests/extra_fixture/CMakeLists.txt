# Test fixture standing in for an external ADB_EXTRA_DIR: it registers one
# dummy extra column, "dummy-extra", for the Heston bench. It is linked into a
# copy of bench_heston (bench_heston_extra_fixture) only, never into the real
# bench, so the public benchmark has no dummy column.
add_executable(bench_heston_extra_fixture ${PROJECT_SOURCE_DIR}/workloads/heston/bench_heston.cpp
               dummy_column.cpp)
target_include_directories(bench_heston_extra_fixture PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(bench_heston_extra_fixture PRIVATE adb::versions ${CMAKE_DL_LIBS})
adb_link_ad_libraries(bench_heston_extra_fixture PRIVATE ${ADB_AD_LIBRARIES})
target_compile_definitions(bench_heston_extra_fixture PRIVATE
                           ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_heston_extra_fixture adb_heap_peak)

add_executable(test_extra_columns test_extra_columns.cpp)
target_link_libraries(test_extra_columns PRIVATE GTest::gtest_main)
target_compile_definitions(test_extra_columns PRIVATE
                           ADB_BENCH_EXTRA="$<TARGET_FILE:bench_heston_extra_fixture>")
add_dependencies(test_extra_columns bench_heston_extra_fixture)
gtest_discover_tests(test_extra_columns PROPERTIES LABELS unit)

# ADB_EXTRA_DIR end to end: a separate build of the suite in
# <build>/extra_dir_check with -DADB_EXTRA_DIR=tests/extra_dir_fixture must
# give the real bench_heston the dummy column. It reuses this build's
# downloaded sources (initial cache below) but configures and builds the deps
# it needs once (incremental afterwards), so it is labelled slow.
set(adb_extra_cache "${CMAKE_BINARY_DIR}/extra_dir_check-init.cmake")
set(adb_extra_init "")
foreach(dep googletest adb_xad adb_cppad adb_adept adb_autodiff adb_eigen)
  string(TOUPPER "${dep}" upper)
  if(${dep}_SOURCE_DIR)
    string(APPEND adb_extra_init
           "set(FETCHCONTENT_SOURCE_DIR_${upper} \"${${dep}_SOURCE_DIR}\" CACHE PATH \"\")\n")
  endif()
endforeach()
foreach(v CMAKE_C_COMPILER CMAKE_CXX_COMPILER ADB_ARCH_FLAGS ADB_WITH_XAD ADB_WITH_CPPAD
          ADB_WITH_ADEPT ADB_WITH_AUTODIFF)
  string(APPEND adb_extra_init "set(${v} \"${${v}}\" CACHE STRING \"\")\n")
endforeach()
file(GENERATE OUTPUT "${adb_extra_cache}" CONTENT "${adb_extra_init}")
add_test(NAME ExtraDirHook.AddsColumnToRealBench
         COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DBIN=${CMAKE_BINARY_DIR}/extra_dir_check
                 "-DGEN=${CMAKE_GENERATOR}" -DINIT_CACHE=${adb_extra_cache}
                 -DFIXTURE=${PROJECT_SOURCE_DIR}/tests/extra_dir_fixture
                 -DMAIN_BENCH=$<TARGET_FILE:bench_heston>
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_extra_dir.cmake)
set_tests_properties(ExtraDirHook.AddsColumnToRealBench PROPERTIES LABELS slow TIMEOUT 1800)

# Each ADB_WITH_<LIB> column is optional: a separate configure of the suite in
# <build>/configure_without_adept with -DADB_WITH_ADEPT=OFF (same downloaded
# sources and settings as above, otherwise) must succeed. Configure only, no
# build. It installs CppAD at configure time (about 6 s on a
# 16-vCPU box the first time, less afterwards), so it is a check, not slow.
add_test(NAME ConfigureOptions.SuiteConfiguresWithAdeptOff
         COMMAND ${CMAKE_COMMAND} -S ${PROJECT_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}/configure_without_adept
                 -G "${CMAKE_GENERATOR}" -C ${adb_extra_cache} -DCMAKE_BUILD_TYPE=Release
                 -DADB_WITH_ADEPT=OFF)
set_tests_properties(ConfigureOptions.SuiteConfiguresWithAdeptOff PROPERTIES LABELS check TIMEOUT 600
                     FIXTURES_SETUP adb_without_adept)

# A column turned off must also build: every target of that configure (the
# benches and the tests still registered) builds in the same nested dir, so
# later runs are incremental. ctest runs the configure above first. A cold
# build takes about 2 min on a 16-vCPU box, so it is labelled slow.
add_test(NAME ConfigureOptions.SuiteBuildsWithAdeptOff
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/configure_without_adept)
set_tests_properties(ConfigureOptions.SuiteBuildsWithAdeptOff PROPERTIES LABELS slow TIMEOUT 3600
                     FIXTURES_REQUIRED adb_without_adept FIXTURES_SETUP adb_without_adept_built)

# The unit and check tests that build registers must pass there too (a bench
# test may not expect a row for the column that is off). ctest runs the
# configure and build above first; the nested ConfigureOptions tests are
# excluded so the check does not configure a build inside the nested one.
# Slow, as it needs that build.
add_test(NAME ConfigureOptions.RegisteredTestsPassWithAdeptOff
         COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}/configure_without_adept
                 -L "unit|check" -E "^ConfigureOptions[.]" --no-tests=error --output-on-failure)
set_tests_properties(ConfigureOptions.RegisteredTestsPassWithAdeptOff PROPERTIES LABELS slow TIMEOUT 3600
                     FIXTURES_REQUIRED adb_without_adept_built)

# The same hook in bench_lmm: a copy of it (bench_lmm_extra_fixture) with the
# dummy LMM column; the real bench_lmm has no dummy column.
add_executable(bench_lmm_extra_fixture ${PROJECT_SOURCE_DIR}/workloads/lmm/bench_main.cpp
               lmm_dummy_column.cpp)
target_link_libraries(bench_lmm_extra_fixture PRIVATE adb_lmm_bench)
target_compile_definitions(bench_lmm_extra_fixture PRIVATE
                           ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_lmm_extra_fixture adb_heap_peak)

add_executable(test_extra_columns_lmm test_extra_columns_lmm.cpp)
target_link_libraries(test_extra_columns_lmm PRIVATE GTest::gtest_main)
target_compile_definitions(test_extra_columns_lmm PRIVATE
                           ADB_BENCH_LMM_EXTRA="$<TARGET_FILE:bench_lmm_extra_fixture>")
add_dependencies(test_extra_columns_lmm bench_lmm_extra_fixture)
gtest_discover_tests(test_extra_columns_lmm PROPERTIES LABELS unit)

# The same hook in bench_cva: a copy of it (bench_cva_extra_fixture) with the
# dummy CVA column; the real bench_cva has no dummy column.
add_executable(bench_cva_extra_fixture ${PROJECT_SOURCE_DIR}/workloads/cva/bench_main.cpp
               cva_dummy_column.cpp)
target_include_directories(bench_cva_extra_fixture PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(bench_cva_extra_fixture PRIVATE adb::versions ${CMAKE_DL_LIBS})
adb_link_ad_libraries(bench_cva_extra_fixture PRIVATE ${ADB_AD_LIBRARIES})
target_compile_definitions(bench_cva_extra_fixture PRIVATE
                           ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_cva_extra_fixture adb_heap_peak)

add_executable(test_extra_columns_cva test_extra_columns_cva.cpp)
target_link_libraries(test_extra_columns_cva PRIVATE GTest::gtest_main)
target_compile_definitions(test_extra_columns_cva PRIVATE
                           ADB_BENCH_CVA_EXTRA="$<TARGET_FILE:bench_cva_extra_fixture>")
add_dependencies(test_extra_columns_cva bench_cva_extra_fixture)
gtest_discover_tests(test_extra_columns_cva PROPERTIES LABELS unit)

# The same hook in bench_sabr: a copy of it (bench_sabr_extra_fixture) with the
# dummy SABR column; the real bench_sabr has no dummy column.
add_executable(bench_sabr_extra_fixture ${PROJECT_SOURCE_DIR}/workloads/sabr/bench_main.cpp
               sabr_dummy_column.cpp)
target_link_libraries(bench_sabr_extra_fixture PRIVATE sabr_bench)
target_compile_definitions(bench_sabr_extra_fixture PRIVATE
                           ADB_HEAP_PEAK_LIB="$<TARGET_FILE:adb_heap_peak>")
add_dependencies(bench_sabr_extra_fixture adb_heap_peak)

add_executable(test_extra_columns_sabr test_extra_columns_sabr.cpp)
target_link_libraries(test_extra_columns_sabr PRIVATE GTest::gtest_main)
target_compile_definitions(test_extra_columns_sabr PRIVATE
                           ADB_BENCH_SABR_EXTRA="$<TARGET_FILE:bench_sabr_extra_fixture>")
add_dependencies(test_extra_columns_sabr bench_sabr_extra_fixture)
gtest_discover_tests(test_extra_columns_sabr PROPERTIES LABELS unit)
