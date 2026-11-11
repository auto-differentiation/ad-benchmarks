cmake_minimum_required(VERSION 3.20)
project(ad_benchmarks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Same floating-point and instruction-set flags for our code, the plain double
# baseline and every AD library (cmake/deps.cmake passes them on):
# - no fused multiply-add contraction, so results (e.g. Rng normals) are
#   bit-identical across compilers and machines;
# - one ISA level, ADB_ARCH_FLAGS (default x86-64-v3: AVX2 + FMA).
set(adb_arch_default "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set(adb_arch_default "-march=x86-64-v3")
endif()
set(ADB_ARCH_FLAGS "${adb_arch_default}" CACHE STRING
    "Instruction-set flags for all benchmark code and AD libraries")
set(ADB_CXX_FLAGS "${ADB_ARCH_FLAGS}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  string(STRIP "-ffp-contract=off ${ADB_CXX_FLAGS}" ADB_CXX_FLAGS)
endif()
separate_arguments(adb_cxx_flags_list NATIVE_COMMAND "${ADB_CXX_FLAGS}")
add_compile_options(${adb_cxx_flags_list})

include(cmake/deps.cmake)

enable_testing()
add_subdirectory(harness)
add_subdirectory(workloads/lmm)
add_subdirectory(workloads/heston)
add_subdirectory(workloads/cva)
add_subdirectory(workloads/sabr)
# Optional directory outside this tree with extra benchmark columns: it adds
# its sources to the bench targets (target_sources), which register the
# columns (harness/extra_columns.hpp).
set(ADB_EXTRA_DIR "" CACHE PATH "Optional directory with extra benchmark columns")
if(ADB_EXTRA_DIR)
  add_subdirectory(${ADB_EXTRA_DIR} extra)
endif()
add_subdirectory(tests)
add_subdirectory(smoke)
