// Keep freed memory in CppAD's pool (its documented option for repeated sweeps), for every CppAD column.

#pragma once

#include <cppad/utility/thread_alloc.hpp>

inline const bool adb_cppad_hold_memory = (CppAD::thread_alloc::hold_memory(true), true);
