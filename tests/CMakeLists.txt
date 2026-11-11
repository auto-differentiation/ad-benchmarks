include(GoogleTest)

add_executable(test_rng test_rng.cpp)
target_include_directories(test_rng PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_rng PRIVATE GTest::gtest_main)
gtest_discover_tests(test_rng PROPERTIES LABELS unit)

add_executable(test_timer test_timer.cpp)
target_include_directories(test_timer PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_timer PRIVATE GTest::gtest_main)
gtest_discover_tests(test_timer PROPERTIES LABELS unit)

add_executable(test_csv test_csv.cpp)
target_include_directories(test_csv PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_csv PRIVATE GTest::gtest_main)
gtest_discover_tests(test_csv PROPERTIES LABELS unit)

add_executable(test_check test_check.cpp)
target_include_directories(test_check PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_check PRIVATE GTest::gtest_main)
gtest_discover_tests(test_check PROPERTIES LABELS unit)

add_executable(test_fd test_fd.cpp)
target_include_directories(test_fd PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_fd PRIVATE GTest::gtest_main)
gtest_discover_tests(test_fd PROPERTIES LABELS unit)

add_executable(test_bench_env test_bench_env.cpp)
target_include_directories(test_bench_env PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_bench_env PRIVATE GTest::gtest_main)
gtest_discover_tests(test_bench_env PROPERTIES LABELS unit)

# Adept's configuration as every column using adb::adept sees it.
if(ADB_WITH_ADEPT)
  add_executable(test_adept_config test_adept_config.cpp)
  target_link_libraries(test_adept_config PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_adept_config PRIVATE adept)
  gtest_discover_tests(test_adept_config PROPERTIES LABELS unit)
endif()

# Only the public suite (no ADB_EXTRA_DIR): XAD is built with its defaults.
if(NOT ADB_EXTRA_DIR AND ADB_WITH_XAD)
  add_executable(test_xad_config test_xad_config.cpp)
  target_link_libraries(test_xad_config PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_xad_config PRIVATE xad)
  gtest_discover_tests(test_xad_config PROPERTIES LABELS unit)
endif()

# W1: swaption portfolio, LMM
add_executable(test_lmm_price lmm/test_lmm_price.cpp)
target_include_directories(test_lmm_price PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_lmm_price PRIVATE GTest::gtest_main)
gtest_discover_tests(test_lmm_price PROPERTIES LABELS unit)

# No-arbitrage property of the kernel (plain double, 200000 paths).
add_executable(test_lmm_martingale lmm/test_lmm_martingale.cpp)
target_include_directories(test_lmm_martingale PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_lmm_martingale PRIVATE GTest::gtest_main)
gtest_discover_tests(test_lmm_martingale PROPERTIES LABELS check)

if(ADB_WITH_XAD)
  add_executable(test_lmm_live lmm/test_lmm_live.cpp)
  target_include_directories(test_lmm_live PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_live PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_lmm_live PRIVATE xad)
  gtest_discover_tests(test_lmm_live PROPERTIES LABELS check)
endif()

if(ADB_WITH_XAD)
  add_executable(test_lmm_fd lmm/test_lmm_fd.cpp)
  target_include_directories(test_lmm_fd PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_fd PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_lmm_fd PRIVATE xad)
  gtest_discover_tests(test_lmm_fd PROPERTIES LABELS check)
endif()

if(ADB_WITH_CPPAD AND ADB_WITH_XAD)
  add_executable(test_lmm_cppad lmm/test_lmm_cppad.cpp)
  target_include_directories(test_lmm_cppad PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_cppad PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_lmm_cppad PRIVATE cppad xad)
  gtest_discover_tests(test_lmm_cppad PROPERTIES LABELS check)
endif()

if(ADB_WITH_ADEPT AND ADB_WITH_XAD)
  add_executable(test_lmm_adept lmm/test_lmm_adept.cpp)
  target_include_directories(test_lmm_adept PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_adept PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_lmm_adept PRIVATE adept xad)
  gtest_discover_tests(test_lmm_adept PROPERTIES LABELS check)
endif()

if(ADB_WITH_AUTODIFF AND ADB_WITH_XAD)
  add_executable(test_lmm_autodiff lmm/test_lmm_autodiff.cpp)
  target_include_directories(test_lmm_autodiff PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_autodiff PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_lmm_autodiff PRIVATE autodiff xad)
  gtest_discover_tests(test_lmm_autodiff PROPERTIES LABELS check)
endif()

if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_lmm_bench lmm/test_lmm_bench.cpp)
  target_include_directories(test_lmm_bench PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_bench PRIVATE adb_lmm_bench GTest::gtest_main)
  gtest_discover_tests(test_lmm_bench PROPERTIES LABELS unit)

  # Runs the bench_lmm executable (one child process per column).
  add_executable(test_lmm_process lmm/test_lmm_process.cpp)
  target_include_directories(test_lmm_process PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_lmm_process PRIVATE GTest::gtest_main)
  target_compile_definitions(test_lmm_process PRIVATE BENCH_LMM_EXE="$<TARGET_FILE:bench_lmm>")
  add_dependencies(test_lmm_process bench_lmm)
  gtest_discover_tests(test_lmm_process PROPERTIES LABELS unit)

  # Concurrent runs of test_lmm_process sharing one TMPDIR must not touch each
  # other's --out CSV. One bench_lmm --out run is short, so every copy repeats
  # the test and all copies start at once: their runs keep overlapping.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME lmm_process_concurrent
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/concurrent_bench.py
                     $<TARGET_FILE:test_lmm_process> ${CMAKE_CURRENT_BINARY_DIR}/lmm_concurrent
                     LmmBench.OutWritesTheCsvToAFile --copies 4 --repeat 20 --stagger 0)
    set_tests_properties(lmm_process_concurrent PROPERTIES LABELS unit
                         ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
  endif()
endif()

# CSV rows written the same way by every workload (runs all four bench programs).
add_executable(test_csv_row test_csv_row.cpp)
target_link_libraries(test_csv_row PRIVATE GTest::gtest_main)
target_compile_definitions(test_csv_row PRIVATE
  BENCH_LMM="$<TARGET_FILE:bench_lmm>" BENCH_HESTON="$<TARGET_FILE:bench_heston>"
  BENCH_CVA="$<TARGET_FILE:bench_cva>" BENCH_SABR="$<TARGET_FILE:bench_sabr>")
add_dependencies(test_csv_row bench_lmm bench_heston bench_cva bench_sabr)
gtest_discover_tests(test_csv_row PROPERTIES LABELS unit)

# Command lines every bench program rejects (runs all four bench programs).
add_executable(test_bench_cli test_bench_cli.cpp)
target_include_directories(test_bench_cli PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_bench_cli PRIVATE GTest::gtest_main)
target_compile_definitions(test_bench_cli PRIVATE
  BENCH_LMM="$<TARGET_FILE:bench_lmm>" BENCH_HESTON="$<TARGET_FILE:bench_heston>"
  BENCH_CVA="$<TARGET_FILE:bench_cva>" BENCH_SABR="$<TARGET_FILE:bench_sabr>")
add_dependencies(test_bench_cli bench_lmm bench_heston bench_cva bench_sabr)
gtest_discover_tests(test_bench_cli PROPERTIES LABELS unit)

# ADB_GRAD_OUT writes the gradient of every AD column (runs all four bench programs).
if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_grad_out test_grad_out.cpp)
  target_include_directories(test_grad_out PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_grad_out PRIVATE GTest::gtest_main)
  target_compile_definitions(test_grad_out PRIVATE
    BENCH_LMM="$<TARGET_FILE:bench_lmm>" BENCH_HESTON="$<TARGET_FILE:bench_heston>"
    BENCH_CVA="$<TARGET_FILE:bench_cva>" BENCH_SABR="$<TARGET_FILE:bench_sabr>")
  add_dependencies(test_grad_out bench_lmm bench_heston bench_cva bench_sabr)
  gtest_discover_tests(test_grad_out PROPERTIES LABELS unit)
endif()

add_subdirectory(heston)

add_subdirectory(cva)

add_subdirectory(sabr)

# Extra-column hook (harness/extra_columns.hpp), with a dummy column standing in
# for an external ADB_EXTRA_DIR.
add_subdirectory(extra_fixture)

# Python scripts (scripts/): pytest suite, when pytest is available.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import pytest"
                  RESULT_VARIABLE ADB_PYTEST_MISSING OUTPUT_QUIET ERROR_QUIET)
  if(ADB_PYTEST_MISSING)
    message(STATUS "pytest not found: tests/scripts not registered")
  else()
    add_test(NAME scripts_pytest
             COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                     ${PROJECT_SOURCE_DIR}/tests/scripts)
    set_tests_properties(scripts_pytest PROPERTIES LABELS unit ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
    # Workflow files (.github/workflows): actionlint + structure, label lint.
    find_program(ADB_ACTIONLINT actionlint)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import yaml"
                    RESULT_VARIABLE ADB_PYYAML_MISSING OUTPUT_QUIET ERROR_QUIET)
    if(ADB_ACTIONLINT AND NOT ADB_PYYAML_MISSING)
      add_test(NAME workflows_pytest
               COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                       ${PROJECT_SOURCE_DIR}/tests/workflows)
      set_tests_properties(workflows_pytest PROPERTIES LABELS lint ENVIRONMENT PYTHONDONTWRITEBYTECODE=1)
    else()
      message(STATUS "actionlint or PyYAML not found: tests/workflows not registered")
    endif()
  endif()
endif()
