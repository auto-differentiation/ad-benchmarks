// W1 test 9: the benchmark entry point writes one CSV row per column.
//
// Contract (workloads/lmm/bench.hpp):
//   struct lmm::BenchConfig { lmm::Setup setup; lmm::Inputs<double> inputs;
//                             std::uint64_t seed; std::size_t warmup, iters; };
//   void lmm::run_bench(const lmm::BenchConfig&, std::ostream& csv);
//     Draws the normals once from harness::Rng(seed), times the plain double
//     price and every gradient column with the same harness::Timer(warmup,
//     iters), and writes one harness::CsvWriter row per column: plain, xad,
//     cppad, adept, autodiff, fd. ratio = median / plain
//     median (1 for plain), workload "lmm", size = setup.N, non-empty
//     library_version. In-process run_bench reports NO memory: all memory
//     fields (peak_memory_bytes, peak_memory_growth_bytes,
//     peak_heap_reserved_bytes) are 0; memory comes only from the
//     per-process driver (harness::run_columns, see test_lmm_process.cpp).
// The bench_lmm executable is a thin main() around run_bench (paper size by
// default); this test calls run_bench directly at the small size.

#include "tests/lmm/lmm_case.hpp"
#include "workloads/lmm/bench.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// RFC 4180 record split (quoted fields, doubled quotes), one line per record.
std::vector<std::string> split_csv(const std::string& line)
{
    std::vector<std::string> out(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                out.back() += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                out.back() += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.emplace_back();
        } else {
            out.back() += c;
        }
    }
    return out;
}

}  // namespace

TEST(LmmBench, WritesOneRowPerColumnWithFiniteRatiosAndNoMemory)
{
    lmm::BenchConfig cfg;
    cfg.setup = lmm_case::small_setup(256);
    cfg.inputs = lmm_case::small_inputs();
    cfg.seed = 42;
    cfg.warmup = 1;
    cfg.iters = 3;

    std::ostringstream out;
    lmm::run_bench(cfg, out);

    std::istringstream in(out.str());
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    const std::vector<std::string> header = split_csv(line);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    for (const char* name : {"library", "workload", "size", "median_s", "ratio",
                             "peak_memory_bytes", "peak_memory_growth_bytes",
                             "peak_heap_reserved_bytes", "library_version"}) {
        ASSERT_TRUE(col.count(name)) << "missing column " << name;
    }

    std::multiset<std::string> libs;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = split_csv(line);
        ASSERT_EQ(f.size(), header.size()) << line;
        const std::string lib = f[col["library"]];
        libs.insert(lib);
        EXPECT_EQ(f[col["workload"]], "lmm") << lib;
        EXPECT_EQ(f[col["size"]], "16") << lib;
        const double median = std::strtod(f[col["median_s"]].c_str(), nullptr);
        const double ratio = std::strtod(f[col["ratio"]].c_str(), nullptr);
        EXPECT_TRUE(std::isfinite(median) && median > 0.0) << lib << " median " << median;
        EXPECT_TRUE(std::isfinite(ratio) && ratio > 0.0) << lib << " ratio " << ratio;
        for (const char* m : {"peak_memory_bytes", "peak_memory_growth_bytes",
                              "peak_heap_reserved_bytes"}) {
            EXPECT_EQ(f[col[m]], "0") << lib << " " << m << " (in-process: no memory)";
        }
        EXPECT_FALSE(f[col["library_version"]].empty()) << lib;
        if (lib == "plain") EXPECT_DOUBLE_EQ(ratio, 1.0);
    }
    // Every built-in column exactly once; rows of registered extra columns
    // (harness/extra_columns.hpp) may come too and get the same checks above.
    for (const char* lib : {"plain", "xad", "cppad", "adept", "autodiff", "fd"})
        EXPECT_EQ(libs.count(lib), 1u) << lib;
}
