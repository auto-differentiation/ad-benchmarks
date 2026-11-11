// W1 test 10b: every benchmark column runs in its own process, so a column's
// peak memory never includes what an earlier column left behind (e.g. a
// global tape, which stays allocated after its column).
//
// Contract:
//   bench_lmm --column <lib> [opts]   runs only that column (plus the untimed
//       plain timing it needs for the ratio) and writes the CSV header and
//       exactly one row for <lib>;
//   bench_lmm [opts]                  runs every column, each through
//       harness::run_columns (harness/columns.hpp, generic, reused by W2-W4):
//       one child process per column (`<exe> --column <lib> [opts]`), one
//       header, then the rows in column order;
//   --small selects the small case of tests/lmm/lmm_case.hpp (N = 16,
//       Nmat = 8, 12 swaptions, small_inputs()); --paths/--iters/--warmup/
//       --seed as before.
//   Memory comes from the driver's untimed memory pass, one evaluation:
//   peak_memory_growth_bytes = VmHWM after - VmRSS just before the evaluation
//       (headline: resident memory the column adds; draws/setup already
//       allocated); peak_memory_bytes = VmHWM of that process;
//   peak_heap_reserved_bytes = heap peak during the evaluation minus heap in
//       use just before it (preloaded hooks), plus the column's own static
//       preallocation (0 for the built-in columns). Includes memory reserved
//       but never touched.
// The fd column allocates almost nothing beyond the plain price, so in its own
// process its peak RSS is close to plain's.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tests/scratch_dir.hpp"

namespace {

const std::string kOpts = " --small --paths 256 --iters 3 --warmup 1";

std::string run(const std::string& args, int* status)
{
    const std::string cmd = std::string(BENCH_LMM_EXE) + args;
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        *status = -1;
        return out;
    }
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    *status = pclose(p);
    return out;
}

std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string l;
    while (std::getline(in, l))
        if (!l.empty()) v.push_back(l);
    return v;
}

// Fields of a CSV line (RFC 4180 quoting).
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

using Row = std::map<std::string, std::string>;

std::vector<Row> rows(const std::string& csv)
{
    const std::vector<std::string> l = lines(csv);
    std::vector<Row> out;
    if (l.empty()) return out;
    const std::vector<std::string> h = split_csv(l[0]);
    for (std::size_t i = 1; i < l.size(); ++i) {
        const std::vector<std::string> f = split_csv(l[i]);
        Row r;
        for (std::size_t k = 0; k < h.size() && k < f.size(); ++k) r[h[k]] = f[k];
        out.push_back(r);
    }
    return out;
}

double num(const Row& r, const char* key)
{
    const auto it = r.find(key);
    return it == r.end() ? -1.0 : std::strtod(it->second.c_str(), nullptr);
}

}  // namespace

TEST(LmmBench, SingleColumnRunWritesHeaderAndOneRow)
{
    int status = 0;
    const std::string out = run(" --column xad" + kOpts, &status);
    ASSERT_EQ(status, 0) << out;
    const std::vector<Row> r = rows(out);
    ASSERT_EQ(r.size(), 1u) << out;
    EXPECT_EQ(r[0].at("library"), "xad");
    EXPECT_GT(num(r[0], "ratio"), 0.0);
    EXPECT_GT(num(r[0], "peak_memory_bytes"), 0.0);
    EXPECT_GE(num(r[0], "peak_memory_growth_bytes"), 0.0);
}

TEST(LmmBench, EachColumnRunsInItsOwnProcess)
{
    int status = 0;
    const std::string out = run(kOpts, &status);
    ASSERT_EQ(status, 0) << out;
    const std::vector<std::string> l = lines(out);
    ASSERT_FALSE(l.empty());

    // One header, rows in column order (the built-in columns first; rows of
    // registered extra columns, harness/extra_columns.hpp, may follow and get
    // the same memory checks), same header as a single-column run.
    int single_status = 0;
    const std::string single = run(" --column fd" + kOpts, &single_status);
    ASSERT_EQ(single_status, 0) << single;
    EXPECT_EQ(l[0], lines(single).at(0));
    const std::vector<Row> r = rows(out);
    const std::vector<std::string> order{"plain", "xad", "cppad", "adept", "autodiff", "fd"};
    ASSERT_GE(r.size(), order.size()) << out;
    std::map<std::string, Row> by;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const std::string lib = r[i].at("library");
        if (i < order.size()) EXPECT_EQ(lib, order[i]);
        EXPECT_TRUE(by.emplace(lib, r[i]).second) << "two rows for " << lib;
        EXPECT_GE(num(r[i], "peak_memory_growth_bytes"), 0.0) << lib;
        EXPECT_LE(num(r[i], "peak_memory_growth_bytes"), num(r[i], "peak_memory_bytes")) << lib;
        EXPECT_GT(num(r[i], "peak_heap_reserved_bytes"), 0.0) << lib;
    }

    // No leftovers: fd (after the taping columns) peaks near plain.
    const double plain = num(by["plain"], "peak_memory_bytes");
    const double fd = num(by["fd"], "peak_memory_bytes");
    EXPECT_LT(fd, 1.25 * plain) << "fd " << fd << " plain " << plain;

    // Growth is resident memory, not reservation: XAD reserves its default
    // tape (~200 MB of heap) but one small path touches only a few MB of it.
    EXPECT_LT(num(by["xad"], "peak_memory_growth_bytes"),
              0.5 * num(by["xad"], "peak_heap_reserved_bytes"))
        << "xad growth " << num(by["xad"], "peak_memory_growth_bytes") << " heap reserved "
        << num(by["xad"], "peak_heap_reserved_bytes");

    // Heap reserved is per column: plain and fd carry no other column's tape.
    EXPECT_LT(num(by["plain"], "peak_heap_reserved_bytes"), 1e6);
    EXPECT_LT(num(by["fd"], "peak_heap_reserved_bytes"), 1e6);

    // The driver's fd row matches a standalone fd run in memory (same process
    // shape), within 25 %.
    const double fd_alone = num(rows(single).at(0), "peak_memory_bytes");
    EXPECT_NEAR(fd, fd_alone, 0.25 * fd_alone);
}

// --out FILE writes the CSV to FILE instead of stdout, like the other bench
// programs (scripts/bench_all.sh passes --out to every one of them).
TEST(LmmBench, OutWritesTheCsvToAFile)
{
    const testing_support::ScratchDir dir("test_lmm_process");
    const std::string csv = dir.path + "/bench_lmm_out_test.csv";
    int status = 0;
    const std::string out = run(" --column xad" + kOpts + " --out " + csv, &status);
    ASSERT_EQ(status, 0) << out;
    EXPECT_EQ(out, "") << "nothing on stdout with --out";

    std::ifstream f(csv);
    ASSERT_TRUE(f) << "no CSV at " << csv;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::vector<Row> r = rows(text);
    ASSERT_EQ(r.size(), 1u) << text;
    EXPECT_EQ(r[0].at("library"), "xad");
    EXPECT_GT(num(r[0], "ratio"), 0.0);
}
