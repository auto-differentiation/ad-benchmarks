// W3 columns by name: one run of each, and its peak heap memory.

#pragma once

#if defined(ADB_WITH_ADEPT)
#include "workloads/cva/adept.hpp"
#endif
#if defined(ADB_WITH_AUTODIFF)
#include "workloads/cva/autodiff.hpp"
#endif
#if defined(ADB_WITH_CPPAD)
#include "workloads/cva/cppad.hpp"
#endif
#include "workloads/cva/fd.hpp"
#include "workloads/cva/model.hpp"
#if defined(ADB_WITH_XAD)
#include "workloads/cva/xad.hpp"
#endif

#include "harness/builtin_columns.hpp"
#include "harness/heap_peak.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cva {

// Central FD step of the fd column (clear of the exposure kink, see the FD test).
inline constexpr double fd_step = 1e-7;

inline const std::vector<std::string>& column_names()
{
    static const std::vector<std::string> names = harness::builtin_column_names();
    return names;
}

struct ColumnRun {
    double value = 0.0;
    std::vector<double> gradient;  // flat layout; empty for "plain"
};

// One run of the named column from scratch: the CppAD recording is
// included.
inline ColumnRun run_column(const std::string& name, const Inputs<double>& in, const Config& c,
                            const std::vector<double>& z)
{
    ColumnRun r;
    if (name == "plain") {
        r.value = cva::cva(in, c, z);
#if defined(ADB_WITH_XAD)
    } else if (name == "xad") {
        r.gradient = gradient_xad(in, c, z, &r.value);
#endif
#if defined(ADB_WITH_CPPAD)
    } else if (name == "cppad") {
        CppadCva f(in, c, z);
        r.gradient = f.gradient(in, z, &r.value);
#endif
#if defined(ADB_WITH_ADEPT)
    } else if (name == "adept") {
        r.gradient = gradient_adept(in, c, z, &r.value);
#endif
#if defined(ADB_WITH_AUTODIFF)
    } else if (name == "autodiff") {
        r.gradient = gradient_autodiff(in, c, z, &r.value);
#endif
    } else if (name == "fd") {
        r.gradient = gradient_fd(in, c, z, fd_step, &r.value);
    } else {
        throw std::invalid_argument("unknown CVA column: " + name);
    }
    return r;
}

// Peak heap bytes of one run_column (needs the adb_heap_peak preload; this is
// the untimed memory pass).
inline std::uint64_t peak_memory_bytes(const std::string& name, const Inputs<double>& in, const Config& c,
                                       const std::vector<double>& z)
{
    return harness::peak_heap_bytes([&] { run_column(name, in, c, z); });
}

}  // namespace cva
