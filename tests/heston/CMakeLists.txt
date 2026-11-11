add_executable(test_heston_model test_heston_model.cpp)
target_include_directories(test_heston_model PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_heston_model PRIVATE GTest::gtest_main)
gtest_discover_tests(test_heston_model PROPERTIES LABELS unit)

if(ADB_WITH_XAD)
  add_executable(test_heston_xad test_heston_xad.cpp)
  target_include_directories(test_heston_xad PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_heston_xad PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_heston_xad PRIVATE xad)
  gtest_discover_tests(test_heston_xad PROPERTIES LABELS unit)
endif()

if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_heston_check test_heston_check.cpp)
  target_include_directories(test_heston_check PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_heston_check PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_heston_check PRIVATE ${ADB_AD_LIBRARIES})
  gtest_discover_tests(test_heston_check PROPERTIES LABELS check)
endif()

# Runs the bench_heston executable (workloads/heston/, built by the same build).
if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_heston_bench test_heston_bench.cpp)
  target_include_directories(test_heston_bench PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_heston_bench PRIVATE GTest::gtest_main)
  add_dependencies(test_heston_bench bench_heston)
  target_compile_definitions(test_heston_bench PRIVATE
                             ADB_BENCH_HESTON="$<TARGET_FILE:bench_heston>")
  gtest_discover_tests(test_heston_bench PROPERTIES LABELS check)

  # Concurrent runs of test_heston_bench sharing one TMPDIR must not touch each
  # other's bench CSVs (several build dirs tested at once on one machine).
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME heston_bench_concurrent
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/concurrent_bench.py
                     $<TARGET_FILE:test_heston_bench> ${CMAKE_CURRENT_BINARY_DIR}/concurrent
                     HestonBench.WritesOneRowPerColumnWithRatioAndMemory
                     HestonBench.PeakMemoryIsPerColumn)
    set_tests_properties(heston_bench_concurrent PROPERTIES LABELS check
                         ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
  endif()
endif()
