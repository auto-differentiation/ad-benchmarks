// W1 benchmark: times the plain double price and every gradient column of the
// LMM swaption portfolio with the same harness::Timer and writes one CSV row
// per column.

#pragma once

#include "workloads/lmm/model.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace lmm {

struct BenchConfig {
    Setup setup;
    Inputs<double> inputs;
    std::uint64_t seed = 42;
    std::size_t warmup = 1;
    std::size_t iters = 5;
    // > 0: the plain median (seconds) every ratio is taken against, measured
    // once by the driver (--plain-median); 0: plain must be the first column
    // timed, and its median is used.
    double plain_median_s = 0.0;
};

// Draws the normals once from harness::Rng(seed); then, per column (plain,
// xad, cppad, adept, autodiff, fd), runs warmup + iters
// timed calls and writes one harness::CsvWriter row.
// - Timed: one price (plain) or one full gradient. CppAD recording happens
//   before timing.
// - autodiff is forward mode (autodiff::dual, 2N - 1 passes), see autodiff.hpp.
// - fd: central differences of the plain price, h = 1e-6 (2(2N - 1) prices).
// - Memory, only from the per-process driver (harness/columns.hpp): in the
//   memory pass (heap hooks preloaded, no timing) each column's set-up
//   (e.g. CppAD recording) and one
//   evaluation run inside harness::measure_memory:
//     peak_memory_bytes        = peak resident set size during the evaluation
//                                (VmHWM, reset just before);
//     peak_memory_growth_bytes = that peak minus the resident size just
//                                before it (headline: memory actually
//                                touched; the draws are already allocated);
//     peak_heap_reserved_bytes = heap allocated during the evaluation above
//                                the level before it, touched or not, plus
//                                the column's static preallocation
//                                (an extra column's own declaration).
//   A timed or in-process run writes 0 in every memory field.
// column empty: every column, in this process (bench_lmm runs each column in
// its own process instead, see harness/columns.hpp), plain first. Otherwise
// only that column, timed against cfg.plain_median_s.
// Throws std::invalid_argument for an unknown column, std::runtime_error when
// a column other than plain is timed without a plain median.
void run_bench(const BenchConfig& cfg, std::ostream& csv, const std::string& column = "",
               bool memory_pass = false);

// Column names in report order: plain, xad, cppad, adept, autodiff, fd,
// then the extra columns registered for lmm::BenchInputs
// (harness/extra_columns.hpp).
std::vector<std::string> column_names();

}  // namespace lmm
