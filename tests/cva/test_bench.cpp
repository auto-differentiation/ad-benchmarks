// W3: the bench_cva entry point writes one CSV row per column.
//
// Contract checked here:
//   bench_cva --size small --out <file.csv>
//     - runs the CVA workload at its small size for every column, plain,
//       xad, cppad, adept, autodiff, fd, through the
//       shared per-process driver harness::run_columns (untimed memory pass
//       with the heap hooks preloaded, then a timed pass without), on the
//       same inputs, shocks and timer settings, and writes one shared-CSV row
//       per column: workload "cva", size = number of paths, median_s > 0,
//       plain_median_s = the plain row's median in every row, ratio =
//       median_s / plain_median_s (1 for plain), library_version non-empty;
//     - memory fields of the shared CSV: peak_memory_bytes (headline, peak
//       resident growth), peak_memory_growth_bytes, peak_heap_reserved_bytes;
//       the heap figure is > 0 for every column and the headline is > 0 for
//       the taping columns (xad, cppad, adept);
//     - exits 0.
//   The test gets the program path as BENCH_CVA and a scratch directory as
//   BENCH_OUT_DIR (compile definitions).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

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

}  // namespace

TEST(CvaBench, SmallRunWritesOneRowPerColumn)
{
    const std::string csv = std::string(BENCH_OUT_DIR) + "/bench_cva_small.csv";
    std::remove(csv.c_str());
    const std::string cmd = std::string(BENCH_CVA) + " --size small --out " + csv;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;

    std::ifstream in(csv);
    ASSERT_TRUE(in) << csv;
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    const std::vector<std::string> header = split_csv(line);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    for (const char* name : {"library", "workload", "size", "median_s", "plain_median_s", "ratio",
                             "peak_memory_bytes", "peak_memory_growth_bytes", "peak_heap_reserved_bytes",
                             "library_version"})
        ASSERT_TRUE(col.count(name)) << name;

    std::map<std::string, std::vector<std::string>> rows;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f = split_csv(line);
        ASSERT_EQ(f.size(), header.size()) << line;
        EXPECT_TRUE(rows.emplace(f[col["library"]], f).second) << "duplicate row " << f[col["library"]];
    }
    // Every built-in column exactly once (duplicates fail above); rows of
    // registered extra columns (harness/extra_columns.hpp) may come too and
    // get the same checks below.
    const std::set<std::string> expected = {"plain", "xad", "cppad", "adept", "autodiff", "fd"};
    for (const std::string& name : expected) ASSERT_TRUE(rows.count(name)) << "no row for " << name;

    const double plain = std::stod(rows["plain"][col["median_s"]]);
    EXPECT_GT(plain, 0.0);
    const std::string size = rows["plain"][col["size"]];
    const std::set<std::string> taping = {"xad", "cppad", "adept"};
    for (const auto& [name, f] : rows) {
        EXPECT_EQ(f[col["workload"]], "cva") << name;
        EXPECT_EQ(f[col["size"]], size) << name;
        const double median = std::stod(f[col["median_s"]]);
        EXPECT_GT(median, 0.0) << name;
        EXPECT_EQ(std::stod(f[col["plain_median_s"]]), plain) << name;
        EXPECT_NEAR(std::stod(f[col["ratio"]]), median / plain, 1e-12 * median / plain) << name;
        EXPECT_GT(std::stoull(f[col["peak_heap_reserved_bytes"]]), 0u) << name;
        (void)std::stoull(f[col["peak_memory_growth_bytes"]]);
        if (taping.count(name)) EXPECT_GT(std::stoull(f[col["peak_memory_bytes"]]), 0u) << name;
        EXPECT_FALSE(f[col["library_version"]].empty()) << name;
    }
    EXPECT_EQ(std::stod(rows["plain"][col["ratio"]]), 1.0);
    EXPECT_GT(std::stoull(size), 0u);
}
