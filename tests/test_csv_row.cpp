// CSV rows written the same way by every workload (bench_lmm, bench_heston,
// bench_cva and bench_sabr at small sizes).
// CsvRow.CompilerFieldIsTheSameInEveryWorkload: every program writes compiler =
// __VERSION__. CsvRow.GivenPlainMedianIsTheBaselineInEveryWorkload: --column C
// --plain-median X uses X as the baseline.
// The program paths come as BENCH_LMM, BENCH_HESTON, BENCH_CVA, BENCH_SABR.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string run(const std::string& cmd, int* status)
{
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

// RFC 4180 fields of one line.
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

// The single row of `<exe> --column <c> <opts>` as name -> value.
std::map<std::string, std::string> column_row(const std::string& exe, const std::string& c,
                                              const std::string& opts)
{
    int status = 0;
    const std::string cmd = exe + " --column " + c + " " + opts;
    const std::string out = run(cmd, &status);
    EXPECT_EQ(status, 0) << cmd << "\n" << out;
    std::istringstream in(out);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);)
        if (!l.empty()) lines.push_back(l);
    std::map<std::string, std::string> row;
    EXPECT_EQ(lines.size(), 2u) << cmd << "\n" << out;
    if (lines.size() != 2) return row;
    const std::vector<std::string> h = split_csv(lines[0]), f = split_csv(lines[1]);
    for (std::size_t i = 0; i < h.size() && i < f.size(); ++i) row[h[i]] = f[i];
    return row;
}

double num(const std::map<std::string, std::string>& r, const char* key)
{
    const auto it = r.find(key);
    return it == r.end() ? -1.0 : std::strtod(it->second.c_str(), nullptr);
}

}  // namespace

// Every workload writes the same `compiler` field: the compiler's __VERSION__,
// as this test (built by the same compiler) sees it.
TEST(CsvRow, CompilerFieldIsTheSameInEveryWorkload)
{
    const std::vector<std::pair<std::string, std::string>> benches = {
        {BENCH_LMM, "--small --paths 256 --iters 3 --warmup 1"},
        {BENCH_HESTON, "--paths 200 --steps 50 --iters 3 --warmup 1"},
        {BENCH_CVA, "--size small --iters 3 --warmup 1"},
        {BENCH_SABR, "--iterations 3 --warmup 1"},
    };
    for (const auto& [exe, opts] : benches) {
        SCOPED_TRACE(exe);
        EXPECT_EQ(column_row(exe, "plain", opts)["compiler"], std::string(__VERSION__));
    }
}

// `--column C --plain-median X` takes X as the plain baseline: the row's
// plain_median_s is X and ratio = median_s / X, and no plain baseline child
// runs. Only bench_heston's children say so on stderr ("plain: ...x plain");
// for the other three the baseline is seen through plain_median_s == X (a
// timed plain child would give its own median). Without --plain-median the
// bench times its own plain baseline, as before.
TEST(CsvRow, GivenPlainMedianIsTheBaselineInEveryWorkload)
{
    const double x = 0.125;  // exact in binary and far from any real median
    const std::vector<std::pair<std::string, std::string>> benches = {
        {BENCH_LMM, "--small --paths 256 --iters 3 --warmup 1"},
        {BENCH_HESTON, "--paths 200 --steps 50 --iters 3 --warmup 1"},
        {BENCH_CVA, "--size small --iters 3 --warmup 1"},
        {BENCH_SABR, "--iterations 3 --warmup 1"},
    };
    for (const auto& [exe, opts] : benches) {
        SCOPED_TRACE(exe);
        const auto given = column_row(exe, "xad", opts + " --plain-median 0.125");
        EXPECT_EQ(num(given, "plain_median_s"), x);
        EXPECT_NEAR(num(given, "ratio"), num(given, "median_s") / x, 1e-9 * num(given, "ratio"));

        const auto own = column_row(exe, "xad", opts);
        EXPECT_GT(num(own, "plain_median_s"), 0.0);
        EXPECT_NE(num(own, "plain_median_s"), x);
        EXPECT_NEAR(num(own, "ratio"), num(own, "median_s") / num(own, "plain_median_s"),
                    1e-9 * num(own, "ratio"));
    }
    int status = 0;
    const std::string err = run(std::string(BENCH_HESTON) + " --column xad --paths 200 --steps 50 --iters 3"
                                " --warmup 1 --plain-median 0.125 2>&1 >/dev/null", &status);
    EXPECT_EQ(status, 0) << err;
    EXPECT_EQ(err.find("plain:"), std::string::npos) << "bench_heston timed a plain child: " << err;
    EXPECT_NE(err.find("xad:"), std::string::npos) << err;
}
