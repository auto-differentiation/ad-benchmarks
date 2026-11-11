// W4: the bench_sabr entry point runs every column through the shared
// per-process driver harness::run_columns, like bench_lmm, bench_heston and
// bench_cva.
//
// Contract checked here:
//   bench_sabr [--warmup N] [--iterations N] --out <file.csv>
//     - runs every column, plain, xad, cppad, adept, autodiff, fd, each in
//       its own child processes through
//       harness::run_columns (untimed memory pass with the adb_heap_peak hooks
//       preloaded, then a timed pass without); plain is timed once and its
//       median is shared by every row;
//     - timed unit unchanged: plain = one residuals<double> evaluation (100
//       vols), every other column = one full 100 x 15 residual Jacobian; set-up
//       (CppAD recording) is untimed;
//     - writes the shared CSV header and one row per built-in column (rows of
//       registered extra columns may come too, with the same checks):
//       workload "sabr", size 100, median_s > 0, iqr_s >= 0, plain_median_s =
//       the plain row's median, ratio = median_s / plain_median_s (1 for
//       plain), library_version, compiler, cpu, date non-empty;
//     - memory fields from the memory pass (one evaluation from scratch inside
//       harness::measure_memory): 0 < peak_memory_bytes < 1e9 (peak resident
//       set), peak_memory_growth_bytes a number, peak_heap_reserved_bytes > 0
//       for every column (no built-in column uses a global tape, so none has
//       a static preallocation to add);
//     - exits 0.
//   bench_sabr --column C [opts]: the header and exactly one row, for C.
//   The test gets the program path as BENCH_SABR and a scratch directory as
//   BENCH_OUT_DIR (compile definitions).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kOpts = " --warmup 1 --iterations 3";

// RFC 4180 fields of one line (no embedded newlines in our records).
std::vector<std::string> split_csv(const std::string& line)
{
    std::vector<std::string> out(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') out.back() += '"', ++i;
            else if (ch == '"') quoted = false;
            else out.back() += ch;
        } else if (ch == '"') quoted = true;
        else if (ch == ',') out.emplace_back();
        else out.back() += ch;
    }
    return out;
}

// Non-empty lines of a file.
std::vector<std::string> read_lines(const std::string& path)
{
    std::ifstream in(path);
    std::vector<std::string> v;
    for (std::string l; std::getline(in, l);)
        if (!l.empty()) v.push_back(l);
    return v;
}

std::vector<std::string> run_bench(const std::string& args, const std::string& name)
{
    const std::string csv = std::string(BENCH_OUT_DIR) + "/" + name;
    std::remove(csv.c_str());
    const std::string cmd = std::string(BENCH_SABR) + args + " --out " + csv;
    EXPECT_EQ(std::system(cmd.c_str()), 0) << cmd;
    return read_lines(csv);
}

}  // namespace

TEST(SabrBench, EveryColumnRunsInItsOwnProcessesWithRatioAndMemory)
{
    const std::vector<std::string> lines = run_bench(kOpts, "bench_sabr_test.csv");
    ASSERT_FALSE(lines.empty());
    const std::vector<std::string> header = split_csv(lines[0]);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    for (const char* name : {"library", "workload", "size", "median_s", "iqr_s", "plain_median_s",
                             "ratio", "peak_memory_bytes", "peak_memory_growth_bytes",
                             "peak_heap_reserved_bytes", "library_version", "compiler", "cpu", "date"})
        ASSERT_TRUE(col.count(name)) << "missing column " << name;

    // Every built-in column exactly once (duplicates fail below); rows of
    // registered extra columns (harness/extra_columns.hpp) may come too and
    // get the same checks.
    const std::vector<std::string> expected = {"plain", "xad", "cppad", "adept", "autodiff", "fd"};
    ASSERT_GE(lines.size(), 1 + expected.size());
    std::map<std::string, std::vector<std::string>> rows;
    for (std::size_t k = 1; k < lines.size(); ++k) {
        const std::vector<std::string> r = split_csv(lines[k]);
        ASSERT_EQ(r.size(), header.size()) << lines[k];
        EXPECT_TRUE(rows.emplace(r[col["library"]], r).second) << "duplicate row " << r[col["library"]];
    }
    for (const std::string& name : expected) ASSERT_TRUE(rows.count(name)) << "no row for " << name;

    const double plain = std::strtod(rows["plain"][col["median_s"]].c_str(), nullptr);
    EXPECT_GT(plain, 0.0);
    for (const auto& [lib, r] : rows) {
        auto num = [&](const char* c) { return std::strtod(r[col[c]].c_str(), nullptr); };
        EXPECT_EQ(r[col["workload"]], "sabr") << lib;
        EXPECT_EQ(r[col["size"]], "100") << lib;
        const double median = num("median_s");
        EXPECT_GT(median, 0.0) << lib;
        EXPECT_GE(num("iqr_s"), 0.0) << lib;
        EXPECT_EQ(num("plain_median_s"), plain) << lib;
        EXPECT_NEAR(num("ratio"), median / plain, 1e-12 * median / plain) << lib;
        EXPECT_GT(num("peak_memory_bytes"), 0.0) << lib;
        EXPECT_LT(num("peak_memory_bytes"), 1e9) << lib;
        EXPECT_NO_THROW((void)std::stoull(r[col["peak_memory_growth_bytes"]])) << lib;
        EXPECT_GT(num("peak_heap_reserved_bytes"), 0.0) << lib;
        for (const char* c : {"library_version", "compiler", "cpu", "date"})
            EXPECT_FALSE(r[col[c]].empty()) << lib << " " << c;
    }
    EXPECT_EQ(std::strtod(rows["plain"][col["ratio"]].c_str(), nullptr), 1.0);

    // One column alone: same header, exactly one row.
    const std::vector<std::string> single = run_bench(" --column xad" + kOpts, "bench_sabr_xad.csv");
    ASSERT_EQ(single.size(), 2u);
    EXPECT_EQ(single[0], lines[0]);
    EXPECT_EQ(split_csv(single[1])[col["library"]], "xad");
}
