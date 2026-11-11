// The extra-column hook in bench_cva: a column registered from outside the
// bench source (cva_dummy_column.cpp, see CMakeLists.txt) is accepted by the
// bench and goes through harness::run_columns like a built-in column, so it
// gets a CSV row with a ratio against plain and the memory fields; its
// declared static preallocation (64 MiB) is in peak_heap_reserved_bytes.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef ADB_BENCH_CVA_EXTRA
#error "ADB_BENCH_CVA_EXTRA (path of bench_cva with the dummy extra column) must be defined"
#endif

namespace {

// Comma-split fields; the checked fields hold no quotes or commas.
std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> out(1);
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') quoted = !quoted;
        else if (ch == ',' && !quoted) out.emplace_back();
        else out.back() += ch;
    }
    return out;
}

}  // namespace

TEST(ExtraColumnsCva, RegisteredColumnWritesARowWithRatioAndMemory)
{
    const char* tmp = std::getenv("TMPDIR");
    const std::string csv = std::string(tmp ? tmp : ".") + "/extra_columns_cva_test.csv";
    std::remove(csv.c_str());
    const std::string cmd = std::string(ADB_BENCH_CVA_EXTRA) +
                            " --column dummy-extra --size small --paths 40 --warmup 0 --iters 2 --out " + csv;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;

    std::ifstream in(csv);
    ASSERT_TRUE(in) << "no CSV at " << csv;
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    const std::vector<std::string> header = split(line);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    for (const char* name : {"library", "workload", "size", "median_s", "plain_median_s", "ratio",
                             "peak_memory_bytes", "peak_heap_reserved_bytes", "library_version"})
        ASSERT_TRUE(col.count(name)) << "missing column " << name;

    std::vector<std::vector<std::string>> rows;
    while (std::getline(in, line))
        if (!line.empty()) rows.push_back(split(line));
    ASSERT_EQ(rows.size(), 1u);
    const std::vector<std::string>& r = rows[0];
    ASSERT_EQ(r.size(), header.size());
    EXPECT_EQ(r[col["library"]], "dummy-extra");
    EXPECT_EQ(r[col["workload"]], "cva");
    EXPECT_EQ(r[col["size"]], "40");  // size = paths
    EXPECT_EQ(r[col["library_version"]], "dummy 0.1");
    EXPECT_GT(std::stod(r[col["median_s"]]), 0.0);
    EXPECT_GT(std::stod(r[col["plain_median_s"]]), 0.0);
    EXPECT_GT(std::stod(r[col["ratio"]]), 0.0);
    EXPECT_GT(std::stod(r[col["peak_memory_bytes"]]), 0.0);
    // Once, on top of a set-up and evaluation heap well below 1 MiB.
    EXPECT_GE(std::stod(r[col["peak_heap_reserved_bytes"]]), double(64 << 20));
    EXPECT_LT(std::stod(r[col["peak_heap_reserved_bytes"]]), double(65 << 20));
}

// The bench adds nothing of its own to an extra column's memory pass (e.g. no
// Plan built for the built-in columns): dummy-noop, which allocates nothing,
// has a heap reserved of under 1 KiB.
TEST(ExtraColumnsCva, BenchAddsNoHeapToAnExtraColumnsMemoryPass)
{
    const char* tmp = std::getenv("TMPDIR");
    const std::string csv = std::string(tmp ? tmp : ".") + "/extra_columns_cva_noop_test.csv";
    std::remove(csv.c_str());
    const std::string cmd = std::string(ADB_BENCH_CVA_EXTRA) +
                            " --column dummy-noop --size small --paths 40 --warmup 0 --iters 2 --out " + csv;
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;

    std::ifstream in(csv);
    std::string line;
    ASSERT_TRUE(std::getline(in, line)) << "no CSV at " << csv;
    const std::vector<std::string> header = split(line);
    std::map<std::string, std::size_t> col;
    for (std::size_t i = 0; i < header.size(); ++i) col[header[i]] = i;
    ASSERT_TRUE(std::getline(in, line));
    const std::vector<std::string> r = split(line);
    EXPECT_EQ(r[col["library"]], "dummy-noop");
    EXPECT_LT(std::stod(r[col["peak_heap_reserved_bytes"]]), 1024.0);
}
