// Command lines of the four bench programs (bench_lmm, bench_heston,
// bench_cva, bench_sabr).
// - Rejected: an unknown option, an option without its value, an unknown
//   column, a --plain-median that is not a number or out of range (abc,
//   1e999), a count option (--warmup) whose value is not a count. Each exits
//   non-zero (2 for lmm, cva and sabr, 1 for heston), says why on stderr
//   (naming the option where one is at fault) and writes no CSV to stdout.
// - The child side of harness::run_columns, `--run-column C [--plain-median
//   S] [--out FILE]`, writes its CSV header and row to FILE when given,
//   else to stdout. The plain row's library_version is "double, <compiler
//   flags>"; the fd row's is "central, h = <step>", the workload's
//   documented finite-difference step.
// - A timed child of a column other than plain needs --plain-median: without
//   it the child exits non-zero, says "no --plain-median" on stderr and writes
//   no row.
// - A write to --out that fails (here /dev/full) makes the parent and the
//   child exit non-zero with "cannot write FILE" on stderr.
// The program paths come as BENCH_LMM, BENCH_HESTON, BENCH_CVA, BENCH_SABR.

#include <gtest/gtest.h>

#include <sys/wait.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tests/scratch_dir.hpp"

namespace {

struct Bench {
    std::string exe;
    int bad_cli_status;  // exit status for an unusable command line
    std::string small;   // options for a short run
    std::string fd_step;  // as documented next to the workload's fd column
};

const std::vector<Bench>& benches()
{
    static const std::vector<Bench> b = {
        {BENCH_LMM, 2, "--small --paths 256 --iters 3 --warmup 1", "1e-6"},
        {BENCH_HESTON, 1, "--paths 200 --steps 50 --iters 3 --warmup 1", "1e-5"},
        {BENCH_CVA, 2, "--size small --iters 3 --warmup 1", "1e-7"},
        {BENCH_SABR, 2, "--iterations 3 --warmup 1", "1e-6"}};
    return b;
}

struct Result {
    int status = -1;
    std::string out;
};

// Runs `cmd` (a shell command line); its exit status and stdout.
Result run(const std::string& cmd)
{
    Result r;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return r;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) r.out.append(buf, n);
    const int s = pclose(p);
    r.status = WIFEXITED(s) ? WEXITSTATUS(s) : -1;
    return r;
}

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

// The one row a child `<exe> --run-column <args>` writes, as name -> value.
std::map<std::string, std::string> child_row(const std::string& exe, const std::string& args)
{
    const std::string cmd = exe + " --run-column " + args;
    const Result r = run(cmd + " 2>/dev/null");
    EXPECT_EQ(r.status, 0) << cmd;
    std::istringstream in(r.out);
    std::string header, row;
    std::getline(in, header);
    std::getline(in, row);
    const std::vector<std::string> h = split_csv(header), f = split_csv(row);
    std::map<std::string, std::string> m;
    for (std::size_t i = 0; i < h.size() && i < f.size(); ++i) m[h[i]] = f[i];
    return m;
}

void expect_rejected(const std::string& args)
{
    for (const Bench& b : benches()) {
        const std::string cmd = b.exe + " " + args;
        const Result r = run(cmd + " 2>/dev/null");
        EXPECT_EQ(r.status, b.bad_cli_status) << cmd;
        EXPECT_EQ(r.out, "") << cmd;
    }
}

// Like expect_rejected, and stderr names `option`.
void expect_rejected_by_name(const std::string& args, const std::string& option)
{
    expect_rejected(args);
    for (const Bench& b : benches()) {
        const std::string cmd = b.exe + " " + args;
        const std::string err = run(cmd + " 2>&1 >/dev/null").out;
        EXPECT_NE(err.find(option), std::string::npos) << cmd << "\n" << err;
    }
}

}  // namespace

TEST(BenchCli, UnknownOptionIsRejected)
{
    expect_rejected("--no-such-option 1");
}

TEST(BenchCli, ColumnWithoutValueIsRejected)
{
    expect_rejected("--column");
}

TEST(BenchCli, UnknownColumnIsRejected)
{
    expect_rejected("--column no-such-column");
}

TEST(BenchCli, PlainMedianThatIsNotANumberIsRejected)
{
    expect_rejected("--column plain --plain-median abc");
    expect_rejected("--column plain --plain-median 1e999");
}

TEST(BenchCli, CountOptionThatIsNotACountIsRejectedByName)
{
    expect_rejected_by_name("--column plain --warmup abc", "--warmup");
}

TEST(BenchCli, ChildWritesItsRowToOut)
{
    const testing_support::ScratchDir dir("test_bench_cli");
    const std::string csv = dir.path + "/child.csv";
    for (const Bench& b : benches()) {
        const std::string cmd = b.exe + " --run-column plain " + b.small + " --out " + csv;
        std::remove(csv.c_str());
        const Result r = run(cmd + " 2>/dev/null");
        EXPECT_EQ(r.status, 0) << cmd;
        EXPECT_EQ(r.out, "") << cmd;
        std::ifstream in(csv);
        std::ostringstream text;
        text << in.rdbuf();
        EXPECT_EQ(text.str().rfind("library,", 0), 0u) << cmd << "\n" << text.str();
        EXPECT_NE(text.str().find("\nplain,"), std::string::npos) << cmd << "\n" << text.str();
    }
}

TEST(BenchCli, FailedWriteToOutFails)
{
    for (const Bench& b : benches()) {
        for (const char* side : {"--column", "--run-column"}) {
            const std::string cmd = b.exe + " " + side + " plain " + b.small + " --out /dev/full";
            const Result r = run(cmd + " 2>&1");
            EXPECT_NE(r.status, 0) << cmd;
            EXPECT_NE(r.out.find("cannot write /dev/full"), std::string::npos) << cmd << "\n" << r.out;
        }
    }
}

TEST(BenchCli, TimedChildWithoutPlainMedianFails)
{
    for (const Bench& b : benches()) {
        const std::string cmd = b.exe + " --run-column fd " + b.small;  // fd is always built
        const Result r = run(cmd + " 2>/dev/null");
        EXPECT_NE(r.status, 0) << cmd;
        EXPECT_EQ(r.out, "") << cmd;
        const std::string err = run(cmd + " 2>&1 >/dev/null").out;
        EXPECT_NE(err.find("no --plain-median"), std::string::npos) << cmd << "\n" << err;
    }
}

TEST(BenchCli, PlainRowNamesDoubleAndItsFlags)
{
    for (const Bench& b : benches()) {
        const std::string v = child_row(b.exe, "plain " + b.small)["library_version"];
        EXPECT_EQ(v.rfind("double, ", 0), 0u) << b.exe << ": " << v;
    }
}

TEST(BenchCli, FdRowNamesItsStep)
{
    for (const Bench& b : benches()) {
        const std::string v = child_row(b.exe, "fd --plain-median 1 " + b.small)["library_version"];
        EXPECT_EQ(v, "central, h = " + b.fd_step) << b.exe;
    }
}
