# W3: CVA on an interest-rate swap portfolio (workloads/cva).

add_executable(test_cva_model test_model.cpp)
target_include_directories(test_cva_model PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_cva_model PRIVATE GTest::gtest_main)
gtest_discover_tests(test_cva_model PROPERTIES LABELS unit)

if(ADB_WITH_XAD)
  add_executable(test_cva_gradients test_gradients.cpp)
  target_include_directories(test_cva_gradients PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_gradients PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_cva_gradients PRIVATE xad)
  gtest_discover_tests(test_cva_gradients PROPERTIES LABELS check)
endif()

if(ADB_WITH_XAD AND ADB_WITH_CPPAD)
  add_executable(test_cva_cppad test_cppad.cpp)
  target_include_directories(test_cva_cppad PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_cppad PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_cva_cppad PRIVATE xad cppad)
  gtest_discover_tests(test_cva_cppad PROPERTIES LABELS check)
endif()

if(ADB_WITH_XAD AND ADB_WITH_ADEPT)
  add_executable(test_cva_adept test_adept.cpp)
  target_include_directories(test_cva_adept PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_adept PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_cva_adept PRIVATE xad adept)
  gtest_discover_tests(test_cva_adept PROPERTIES LABELS check)
endif()

if(ADB_WITH_XAD AND ADB_WITH_AUTODIFF)
  add_executable(test_cva_autodiff test_autodiff.cpp)
  target_include_directories(test_cva_autodiff PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_autodiff PRIVATE GTest::gtest_main)
  adb_link_ad_libraries(test_cva_autodiff PRIVATE xad autodiff)
  gtest_discover_tests(test_cva_autodiff PROPERTIES LABELS check)
endif()

add_executable(test_cva_memory test_memory.cpp)
target_include_directories(test_cva_memory PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test_cva_memory PRIVATE GTest::gtest_main ${CMAKE_DL_LIBS})
gtest_discover_tests(test_cva_memory PROPERTIES LABELS unit
                     ENVIRONMENT "$<$<TARGET_EXISTS:adb_heap_peak>:LD_PRELOAD=$<TARGET_FILE:adb_heap_peak>>")

if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_cva_columns_memory test_columns_memory.cpp)
  target_include_directories(test_cva_columns_memory PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_columns_memory PRIVATE GTest::gtest_main ${CMAKE_DL_LIBS} adb::versions)
  adb_link_ad_libraries(test_cva_columns_memory PRIVATE ${ADB_AD_LIBRARIES})
  gtest_discover_tests(test_cva_columns_memory PROPERTIES LABELS check
                       ENVIRONMENT "$<$<TARGET_EXISTS:adb_heap_peak>:LD_PRELOAD=$<TARGET_FILE:adb_heap_peak>>")
endif()

if(ADB_ALL_AD_LIBRARIES_ON)
  add_executable(test_cva_bench test_bench.cpp)
  target_include_directories(test_cva_bench PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_cva_bench PRIVATE GTest::gtest_main)
  add_dependencies(test_cva_bench bench_cva)
  target_compile_definitions(test_cva_bench PRIVATE
    "BENCH_CVA=\"$<TARGET_FILE:bench_cva>\""
    "BENCH_OUT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\"")
  gtest_discover_tests(test_cva_bench PROPERTIES LABELS slow)
endif()
