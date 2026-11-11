// W4 benchmark, child side of harness::run_columns: one column, one CSV row.

#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace sabr {

// Column names in report order: plain, xad, cppad, adept, autodiff, fd.
const std::vector<std::string>& column_names();

struct ColumnOptions {
    std::size_t warmup = 100;
    std::size_t iterations = 2000;
    bool memory_pass = false;
    // Timed pass of a column other than plain: the plain median every ratio
    // is taken against (from the plain child, --plain-median).
    double plain_median_s = 0.0;
};

// Writes the harness::CsvWriter header and one row for `column` (a built-in
// or a column registered for sabr::BenchInputs, harness/extra_columns.hpp),
// at the standard market and parameters. Timed unit:
//   plain:  one residuals<double> evaluation (100 vols) from precomputed
//           quote terms;
//   others: one full 100 x 15 residual Jacobian.
// Set-up (quote terms, Jacobian object, CppAD recording) happens before the
// timer.
// Timed pass: timings, memory fields 0. Memory pass (heap hooks preloaded,
// untimed): set-up plus one evaluation inside harness::measure_memory;
// peak_memory_bytes = peak resident set, peak_memory_growth_bytes = its
// growth, peak_heap_reserved_bytes = heap allocated plus the column's static
// preallocation (none for the built-in columns: none uses a global tape; an
// extra column's own declaration, harness::extra_static_prealloc).
// Throws std::invalid_argument for an unknown column, std::runtime_error for
// a timed non-plain column without a plain median.
void run_column(const std::string& column, const ColumnOptions& opts, std::ostream& csv);

}  // namespace sabr
