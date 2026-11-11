# AD libraries under benchmark, each pinned to an exact commit.
#
# Every library is downloaded as the source archive of one commit SHA and
# checked against a SHA256 hash, so a moved tag or changed upstream cannot
# silently change what is benchmarked. The release tag is recorded next to the
# SHA and both go into build/generated/versions.hpp (CSV library_version).
#
# All of them are compiled with ADB_CXX_FLAGS (top-level CMakeLists.txt): XAD
# and Adept through the directory compile options, CppAD through
# CMAKE_CXX_FLAGS of its install step.
#
# Targets: adb::xad, adb::cppad, adb::adept, adb::autodiff (adb::eigen for
# autodiff), and adb::versions for the header.

include(FetchContent)

option(ADB_WITH_XAD "Benchmark XAD" ON)
option(ADB_WITH_CPPAD "Benchmark CppAD" ON)
option(ADB_WITH_ADEPT "Benchmark Adept-2" ON)
option(ADB_WITH_AUTODIFF "Benchmark autodiff" ON)

# adb_pin(<name> <tag> <commit sha> <archive url> <archive sha256>)
# Declares the FetchContent dependency adb_<name> and sets
# ADB_<NAME>_VERSION to "<tag>@<short sha>" of the pin, or to
# "<branch>@<short sha>" of the checkout when FETCHCONTENT_SOURCE_DIR_ADB_<NAME>
# points at the top of a git work tree, or to "local@<dir name>" when it points
# at any other dir (e.g. a `git archive` copy, even one inside another repo's
# work tree, whose branch and HEAD say nothing about it). A non-empty
# -DADB_<NAME>_VERSION_LABEL=<text> wins over all of these, e.g. to name the
# branch and commit a `git archive` copy was made from. Only XAD is added as
# a CMake subproject; the others are download-only (SOURCE_SUBDIR does not
# exist, so FetchContent_MakeAvailable never adds their CMakeLists.txt).
function(adb_pin name tag sha url hash)
  string(TOUPPER "${name}" upper)
  string(SUBSTRING "${sha}" 0 7 short)
  set(version "${tag}@${short}")
  set(src "${FETCHCONTENT_SOURCE_DIR_ADB_${upper}}")
  if(src)
    find_package(Git QUIET)
    # Only trust git when src is the top of its own work tree: git -C also
    # succeeds in any subdir of an enclosing repo. Compare real paths so
    # symlinks and trailing slashes do not matter.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${src}" rev-parse --show-toplevel
                    OUTPUT_VARIABLE top RESULT_VARIABLE top_rc
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(src_real "${src}" REALPATH)
    set(branch_rc 1)
    if(top_rc EQUAL 0)
      get_filename_component(top_real "${top}" REALPATH)
      if(top_real STREQUAL src_real)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${src}" rev-parse --abbrev-ref HEAD
                        OUTPUT_VARIABLE branch RESULT_VARIABLE branch_rc
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${src}" rev-parse --short=7 HEAD
                        OUTPUT_VARIABLE head RESULT_VARIABLE head_rc
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      endif()
    endif()
    if(branch_rc EQUAL 0 AND head_rc EQUAL 0)
      set(version "${branch}@${head}")
    else()
      get_filename_component(dir_name "${src}" NAME)
      set(version "local@${dir_name}")
    endif()
  endif()
  if(NOT "${ADB_${upper}_VERSION_LABEL}" STREQUAL "")
    set(version "${ADB_${upper}_VERSION_LABEL}")
  endif()
  set(ADB_${upper}_VERSION "${version}" PARENT_SCOPE)
  set(subdir "")
  if(NOT name STREQUAL "xad")
    set(subdir SOURCE_SUBDIR adb-download-only)
  endif()
  FetchContent_Declare(adb_${name} URL "${url}" URL_HASH SHA256=${hash}
                       DOWNLOAD_EXTRACT_TIMESTAMP TRUE ${subdir})
endfunction()

# The XAD pin is a parameter: another commit needs its archive hash too
# (-DADB_XAD_COMMIT=<sha> -DADB_XAD_SHA256=<hash>; the URL follows the commit
# unless -DADB_XAD_URL is given).
set(adb_xad_release_commit aaf4e9cad59cf1bfe9728b491f95f011db687f18)  # tag v2.2.0
set(ADB_XAD_COMMIT ${adb_xad_release_commit} CACHE STRING "XAD commit benchmarked")
set(ADB_XAD_SHA256 c00718055c9db666ff0745802be1a58f5107215dee5ee08636e8ce2304c8e2b9
    CACHE STRING "SHA256 of the XAD archive")
set(ADB_XAD_URL https://github.com/auto-differentiation/xad/archive/${ADB_XAD_COMMIT}.tar.gz
    CACHE STRING "XAD source archive")
# The pinned commit is release v2.2.0; any other commit is labelled as main.
if(ADB_XAD_COMMIT STREQUAL adb_xad_release_commit)
  set(adb_xad_tag v2.2.0)
else()
  set(adb_xad_tag main)
endif()
adb_pin(xad ${adb_xad_tag} ${ADB_XAD_COMMIT} ${ADB_XAD_URL} ${ADB_XAD_SHA256})
adb_pin(cppad 20250000.2 f7d1601aa71dcf78c4342b069520a43d3cf9fac1
  https://github.com/coin-or/CppAD/archive/f7d1601aa71dcf78c4342b069520a43d3cf9fac1.tar.gz
  01384f30e4dd8b6a1ffdebbd96c474abcf1d468953990582ba1d8baa5816665b)
adb_pin(adept v2.1.3 b44ae4974e8983666dc2ec34cf067c527eb58d03
  https://github.com/rjhogan/Adept-2/archive/b44ae4974e8983666dc2ec34cf067c527eb58d03.tar.gz
  9d6073013785e12582ebee700ac7c73bac8dac50660c3778127a239cc6ce325f)
adb_pin(autodiff v1.1.2 2e2f3a2b16afcd9c04e76c8a689e9fd23ff78679
  https://github.com/autodiff/autodiff/archive/2e2f3a2b16afcd9c04e76c8a689e9fd23ff78679.tar.gz
  db15fa580be2b03ea696902b1a94dd2a29ff67ef8dea3fb740185182f8dafa04)
adb_pin(eigen 3.4.0 3147391d946bb4b6c68edd901f2add6ac1f31f8c
  https://gitlab.com/libeigen/eigen/-/archive/3147391d946bb4b6c68edd901f2add6ac1f31f8c/eigen-3147391d946bb4b6c68edd901f2add6ac1f31f8c.tar.gz
  0c8c490764f9c2a793133491adca0cd073b73e0bde965c68cbe58d91b5ed4261)

macro(adb_populate name)
  FetchContent_MakeAvailable(adb_${name})
endmacro()

# Configures, builds and installs a CMake project into <prefix> at configure
# time, once (redone only if its pinned version or arguments change).
# pkg-config sees an empty search path, so nothing installed on the machine
# (e.g. a system Eigen, which CppAD probes for and then requires from every
# user of <cppad/cppad.hpp>) changes what is built.
function(adb_install_cmake name src prefix)
  set(args -G "${CMAKE_GENERATOR}"
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      "-DCMAKE_CXX_FLAGS=${ADB_CXX_FLAGS}"
      -DCMAKE_INSTALL_PREFIX=${prefix}
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev
      ${ARGN})
  set(stamp "${prefix}/.adb_${name}_stamp")
  string(TOUPPER "${name}" upper)
  set(no_pkg "${CMAKE_BINARY_DIR}/_deps/adb-empty-pkgconfig")
  file(MAKE_DIRECTORY "${no_pkg}")
  set(env ${CMAKE_COMMAND} -E env PKG_CONFIG_LIBDIR=${no_pkg} PKG_CONFIG_PATH=${no_pkg})
  string(SHA256 key "${ADB_${upper}_VERSION};${src};${args};no-system-pkgconfig")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" old)
    if(old STREQUAL key)
      return()
    endif()
  endif()
  set(bin "${CMAKE_BINARY_DIR}/_deps/adb_${name}-ext")
  message(STATUS "Building ${name} into ${prefix}")
  file(REMOVE_RECURSE "${bin}")  # drop cached package probes
  execute_process(COMMAND ${env} ${CMAKE_COMMAND} -S "${src}" -B "${bin}" ${args}
                  RESULT_VARIABLE rc OUTPUT_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Configuring ${name} failed (${rc})")
  endif()
  execute_process(COMMAND ${env} ${CMAKE_COMMAND} --build "${bin}" --target install
                  RESULT_VARIABLE rc OUTPUT_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Building ${name} failed (${rc})")
  endif()
  file(WRITE "${stamp}" "${key}")
endfunction()

set(ADB_DEPS_PREFIX "${CMAKE_BINARY_DIR}/_deps/install")

if(ADB_WITH_XAD)
  set(XAD_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  # Off by default; can be enabled from the command line.
  if(NOT DEFINED XAD_ENABLE_CODEGEN)
    set(XAD_ENABLE_CODEGEN OFF CACHE BOOL "")
  endif()
  FetchContent_MakeAvailable(adb_xad)
  add_library(adb_xad INTERFACE)
  target_link_libraries(adb_xad INTERFACE XAD::xad)
  add_library(adb::xad ALIAS adb_xad)
endif()

if(ADB_WITH_CPPAD)
  adb_populate(cppad)
  adb_install_cmake(cppad "${adb_cppad_SOURCE_DIR}" "${ADB_DEPS_PREFIX}"
                    -Dcppad_prefix=${ADB_DEPS_PREFIX} -Dcppad_static_lib=TRUE
                    -Dcmake_install_libdirs=lib)
  find_library(ADB_CPPAD_LIB NAMES cppad_lib PATHS "${ADB_DEPS_PREFIX}/lib"
               NO_DEFAULT_PATH REQUIRED)
  add_library(adb_cppad INTERFACE)
  target_include_directories(adb_cppad SYSTEM INTERFACE "${ADB_DEPS_PREFIX}/include")
  target_link_libraries(adb_cppad INTERFACE "${ADB_CPPAD_LIB}")
  add_library(adb::cppad ALIAS adb_cppad)
endif()

if(ADB_WITH_ADEPT)
  # Adept-2 builds with autotools; compile its library sources directly,
  # without BLAS/LAPACK (only needed for its matrix solvers).
  adb_populate(adept)
  file(GLOB adept_sources "${adb_adept_SOURCE_DIR}/adept/*.cpp")
  add_library(adb_adept STATIC ${adept_sources})
  set(adept_config "${CMAKE_BINARY_DIR}/generated/adept")
  file(WRITE "${adept_config}/config.h"
       "#define CXX \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\"\n"
       "#define CXXFLAGS \"(built by ad-benchmarks CMake)\"\n")
  target_include_directories(adb_adept SYSTEM PUBLIC "${adb_adept_SOURCE_DIR}/include")
  target_include_directories(adb_adept PRIVATE "${adept_config}")
  target_compile_definitions(adb_adept PRIVATE HAVE_CONFIG_H)
  target_compile_definitions(adb_adept PUBLIC ADEPT_STACK_THREAD_UNSAFE=1)
  add_library(adb::adept ALIAS adb_adept)
endif()

if(ADB_WITH_AUTODIFF)
  adb_populate(eigen)
  add_library(adb_eigen INTERFACE)
  target_include_directories(adb_eigen SYSTEM INTERFACE "${adb_eigen_SOURCE_DIR}")
  add_library(adb::eigen ALIAS adb_eigen)

  adb_populate(autodiff)
  add_library(adb_autodiff INTERFACE)
  target_include_directories(adb_autodiff SYSTEM INTERFACE "${adb_autodiff_SOURCE_DIR}")
  target_link_libraries(adb_autodiff INTERFACE adb::eigen)
  add_library(adb::autodiff ALIAS adb_autodiff)
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/versions.hpp.in"
               "${CMAKE_BINARY_DIR}/generated/versions.hpp" @ONLY)
add_library(adb_versions INTERFACE)
target_include_directories(adb_versions INTERFACE "${CMAKE_BINARY_DIR}/generated")
add_library(adb::versions ALIAS adb_versions)

# Optional AD libraries, each with its ADB_WITH_<LIB> option above.
set(ADB_AD_LIBRARIES xad cppad adept autodiff)

# ON when every optional AD library is on: tests that expect every built-in
# column are registered only then.
set(ADB_ALL_AD_LIBRARIES_ON ON)
foreach(lib IN LISTS ADB_AD_LIBRARIES)
  string(TOUPPER "${lib}" upper)
  if(NOT ADB_WITH_${upper})
    set(ADB_ALL_AD_LIBRARIES_ON OFF)
  endif()
endforeach()

# adb_link_ad_libraries(<target> <scope> <lib>...): links adb::<lib> and
# defines ADB_WITH_<LIB> for each listed library whose column is on; libraries
# turned off are skipped.
function(adb_link_ad_libraries target scope)
  foreach(lib IN LISTS ARGN)
    string(TOUPPER "${lib}" upper)
    if(ADB_WITH_${upper})
      target_link_libraries(${target} ${scope} adb::${lib})
      target_compile_definitions(${target} ${scope} ADB_WITH_${upper})
    endif()
  endforeach()
endfunction()
