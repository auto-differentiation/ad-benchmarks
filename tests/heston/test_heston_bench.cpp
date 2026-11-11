// The bench_heston entry point: runs every column at a small size and writes
// one CSV row per column (harness::CsvWriter format).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tests/scratch_dir.hpp"

#ifndef ADB_BENCH_HESTON
#error "ADB_BENCH_HESTON (path of the bench_heston executable) must be defined"
#endif

namespace {

// RFC 4180 fields of one line (quoted fields may hold commas and "").
std::vector<std::string> split_csv(const std::string& line)
{
    std::vector<std::string> out(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') { out.back() += '"'; ++i; }
            else if (ch == '"') quoted = false;
            else out.back() += ch;
        } else if (ch == '"') quoted = true;
        else if (ch == ',') out.emplace_back();
        else out.back() += ch;
    }
    return out;
}

}  // namespace

TEST(HestonBench, WritesOneRowPerColumnWithRatioAndMemory)
{
    const testing_support::ScratchDir dir("test_heston_bench");
    const std::string csv = dir.path + "/bench_heston_test.csv";
    const std::string cmd = std::string(ADB_BENCH_HESTON) +
                            " --paths 200 --steps 50 --warmup 1 --iters 3 --out " + csv;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;

    std::ifstream in(csv);
    ASSERT_TRUE(in) << "no CSV at " << csv;
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    const std::vector<std::string> header = split_csv(line);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    for (const char* name : {"library", "workload", "size", "median_s", "plain_median_s", "ratio",
                             "peak_memory_bytes", "library_version"})
        ASSERT_TRUE(col.count(name)) << "missing column " << name;

    std::map<std::string, std::vector<std::string>> rows;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = split_csv(line);
        ASSERT_EQ(f.size(), header.size()) << line;
        EXPECT_TRUE(rows.emplace(f[col["library"]], f).second) << "duplicate row " << f[col["library"]];
    }

    // Every built-in column exactly once (duplicates fail above); rows of
    // registered extra columns (harness/extra_columns.hpp) may follow and get
    // the same checks below.
    const std::set<std::string> expected{"plain", "xad", "cppad", "adept", "autodiff", "fd"};
    for (const std::string& lib : expected) ASSERT_TRUE(rows.count(lib)) << "no row for " << lib;

    const double plain = std::stod(rows["plain"][col["median_s"]]);
    ASSERT_GT(plain, 0.0);
    for (const auto& [lib, f] : rows) {
        SCOPED_TRACE(lib);
        EXPECT_EQ(f[col["workload"]], "heston");
        EXPECT_EQ(f[col["size"]], "200");  // size = paths
        const double median = std::stod(f[col["median_s"]]);
        EXPECT_GT(median, 0.0);
        EXPECT_DOUBLE_EQ(std::stod(f[col["plain_median_s"]]), plain);
        EXPECT_NEAR(std::stod(f[col["ratio"]]), median / plain, 1e-12 * (median / plain));
        EXPECT_GT(std::stoull(f[col["peak_memory_bytes"]]), 0u);
        EXPECT_FALSE(f[col["library_version"]].empty());
    }
    EXPECT_DOUBLE_EQ(std::stod(rows["plain"][col["ratio"]]), 1.0);
}

namespace {

// Rows of a bench CSV keyed by library, each a map column name -> field.
std::map<std::string, std::map<std::string, std::string>> read_rows(const std::string& csv)
{
    std::map<std::string, std::map<std::string, std::string>> rows;
    std::ifstream in(csv);
    std::string line;
    if (!std::getline(in, line)) return rows;
    const std::vector<std::string> header = split_csv(line);
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = split_csv(line);
        std::map<std::string, std::string> row;
        for (std::size_t i = 0; i < header.size() && i < f.size(); ++i) row[header[i]] = f[i];
        rows[row["library"]] = row;
    }
    return rows;
}

}  // namespace

// peak_memory_bytes belongs to its own column only: plain, FD and forward-mode
// autodiff hold no tape, so their peak must stay at the shared baseline
// (binary, draws). A column measured after a tape library must not inherit
// the memory that library keeps (e.g. a global tape that keeps its chunks
// after a reset).
TEST(HestonBench, PeakMemoryIsPerColumn)
{
    const testing_support::ScratchDir dir("test_heston_bench");
    const std::string csv = dir.path + "/bench_heston_memory.csv";
    const std::string cmd = std::string(ADB_BENCH_HESTON) +
                            " --paths 1000 --steps 50 --warmup 0 --iters 1 --out " + csv;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;

    auto rows = read_rows(csv);  // registered extra columns may add rows
    for (const char* lib : {"plain", "xad", "cppad", "adept", "autodiff", "fd"})
        ASSERT_TRUE(rows.count(lib)) << "no row for " << lib;
    const auto mem = [&](const char* lib) { return std::stod(rows[lib]["peak_memory_bytes"]); };
    const double plain = mem("plain");
    ASSERT_GT(plain, 0.0);
    for (const char* lib : {"fd", "autodiff"})
        EXPECT_LE(mem(lib), 1.25 * plain) << lib << " " << mem(lib) << " vs plain " << plain;
}
