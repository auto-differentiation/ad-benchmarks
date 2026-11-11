// H4: a CSV row holds library, workload, size, median, IQR, plain-time ratio,
// peak memory (absolute, growth over the process baseline, heap reserved),
// versions,
// compiler, CPU, date and git SHA.
//
// Contract checked here:
//   harness::CsvWriter w(std::ostream&);  w.row(const harness::Record&);
//   - the header line is written exactly once, before the first row:
//       library,workload,size,median_s,iqr_s,plain_median_s,ratio,
//       peak_memory_bytes,peak_memory_growth_bytes,peak_heap_reserved_bytes,
//       library_version,
//       compiler,cpu,date,git_sha
//     (one line, no spaces);
//   - every line ends with "\n";
//   - all values are written as given (nothing is detected or recomputed);
//   - integers (size, peak_memory_bytes, peak_memory_growth_bytes,
//     peak_heap_reserved_bytes) in plain decimal;
//   - doubles (median_s, iqr_s, plain_median_s, ratio) as printf "%.17g",
//     enough digits to round-trip exactly;
//   - strings quoted as in RFC 4180: a field containing a comma, a double
//     quote, CR or LF is wrapped in double quotes, with embedded double
//     quotes doubled; other fields are written bare.

#include "harness/csv.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace {

const std::string kHeader =
    "library,workload,size,median_s,iqr_s,plain_median_s,ratio,"
    "peak_memory_bytes,peak_memory_growth_bytes,peak_heap_reserved_bytes,library_version,"
    "compiler,cpu,date,git_sha\n";

harness::Record xad_record()
{
    harness::Record r;
    r.library = "XAD";
    r.workload = "swaption_lmm";
    r.size = 1000;
    r.median_s = 0.1;
    r.iqr_s = 0.00042;
    r.plain_median_s = 0.02;
    r.ratio = 5.0;
    r.peak_memory_bytes = 123456789;
    r.peak_memory_growth_bytes = 98765432;
    r.peak_heap_reserved_bytes = 222333444;
    r.library_version = "2.0.0 \"main@1a2b3c4\"";
    r.compiler = "GCC 13.2.0";
    r.cpu = "Example CPU @ 3.00GHz, 8 cores";
    r.date = "2026-10-28T09:30:00Z";
    r.git_sha = "f1b8d01";
    return r;
}

harness::Record fd_record()
{
    harness::Record r;
    r.library = "FD";
    r.workload = "swaption_lmm";
    r.size = 1000;
    r.median_s = 1e-7;
    r.iqr_s = 0.0;
    r.plain_median_s = 0.02;
    r.ratio = 3.0000000000000004;
    r.peak_memory_bytes = 0;
    r.peak_memory_growth_bytes = 0;
    r.peak_heap_reserved_bytes = 0;
    r.library_version = "none";
    r.compiler = "GCC 13.2.0";
    r.cpu = "Example CPU";
    r.date = "2026-10-28T09:30:00Z";
    r.git_sha = "f1b8d01";
    return r;
}

}  // namespace

TEST(Csv, HeaderOnceThenRowsWithQuotingAndFullPrecision)
{
    std::ostringstream out;
    {
        harness::CsvWriter w(out);
        w.row(xad_record());
        w.row(fd_record());
    }

    const std::string expected =
        kHeader +
        "XAD,swaption_lmm,1000,0.10000000000000001,0.00042000000000000002,0.02,5,"
        "123456789,98765432,222333444,\"2.0.0 \"\"main@1a2b3c4\"\"\",GCC 13.2.0,"
        "\"Example CPU @ 3.00GHz, 8 cores\",2026-10-28T09:30:00Z,f1b8d01\n"
        "FD,swaption_lmm,1000,9.9999999999999995e-08,0,0.02,3.0000000000000004,"
        "0,0,0,none,GCC 13.2.0,Example CPU,2026-10-28T09:30:00Z,f1b8d01\n";

    EXPECT_EQ(out.str(), expected);
}
