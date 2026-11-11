# ADB_EXTRA_DIR hook, end to end (ctest ExtraDirHook.AddsColumnToRealBench):
#   1. configures a separate build of the suite (BIN) with
#      -DADB_EXTRA_DIR=FIXTURE (tests/extra_dir_fixture), reusing the parent
#      build's downloaded sources (initial cache INIT_CACHE), and builds only
#      bench_heston there;
#   2. that bench_heston must run the column the external dir added
#      (--column dummy-extra) and write its CSV row;
#   3. the parent build's bench_heston (MAIN_BENCH, no ADB_EXTRA_DIR) must
#      not know that column.
# Inputs (-D): SRC, BIN, GEN, INIT_CACHE, FIXTURE, MAIN_BENCH.

foreach(v SRC BIN GEN INIT_CACHE FIXTURE MAIN_BENCH)
  if(NOT DEFINED ${v})
    message(FATAL_ERROR "check_extra_dir.cmake: -D${v}=... missing")
  endif()
endforeach()

execute_process(COMMAND ${CMAKE_COMMAND} -S "${SRC}" -B "${BIN}" -G "${GEN}" -C "${INIT_CACHE}"
                        -DCMAKE_BUILD_TYPE=Release "-DADB_EXTRA_DIR=${FIXTURE}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "configuring the build with ADB_EXTRA_DIR failed (${rc}):\n${out}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build "${BIN}" --target bench_heston
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "building bench_heston with ADB_EXTRA_DIR failed (${rc}):\n${out}")
endif()

set(csv "${BIN}/extra_dir_check.csv")
file(REMOVE "${csv}")
execute_process(COMMAND "${BIN}/workloads/heston/bench_heston" --column dummy-extra --paths 20 --steps 2
                        --warmup 0 --iters 1 --out "${csv}"
                RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "bench_heston built with ADB_EXTRA_DIR does not run the column the "
                      "external dir added (exit ${rc}):\n${err}")
endif()
file(STRINGS "${csv}" lines)
list(LENGTH lines n)
if(NOT n EQUAL 2)
  message(FATAL_ERROR "expected a CSV header and one row, got ${n} lines in ${csv}")
endif()
list(GET lines 1 row)
if(NOT row MATCHES "^dummy-extra,heston,20,")
  message(FATAL_ERROR "unexpected row: ${row}")
endif()

execute_process(COMMAND "${MAIN_BENCH}" --column dummy-extra --paths 20 --steps 2 --warmup 0
                        --iters 1 --out "${BIN}/extra_dir_check_main.csv"
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "the default build's bench_heston runs dummy-extra; the fixture leaked "
                      "into the real bench")
endif()
message(STATUS "ADB_EXTRA_DIR hook: dummy-extra row written by the real bench_heston")
