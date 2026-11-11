// ADB_GRAD_OUT (harness/grad_note.hpp): with it set, a bench writes each timed
// gradient to that file, one component per line, so columns can be compared at
// the published size. Every AD column of every workload writes it, and all give
// the same numbers (XAD's, rtol 1e-8, as the parity tests). Without it, a bench
// writes nothing but its CSV. A process that writes a gradient says so once on
// stderr, and once more if it cannot open the file; then an old file there is
// removed if the directory allows it, and a directory stays.
// The program paths come as BENCH_LMM, BENCH_HESTON, BENCH_CVA, BENCH_SABR.

#include "harness/check.hpp"
#include "tests/scratch_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>  // geteuid

namespace {

struct Bench {
    const char* name;
    std::string exe, opts;
    std::size_t gradient_size;
};

void PrintTo(const Bench& b, std::ostream* os) { *os << b.name; }

const std::vector<Bench> benches = {
    {"lmm", BENCH_LMM, "--small --paths 256 --iters 3 --warmup 1", 31},
    {"heston", BENCH_HESTON, "--paths 200 --steps 50 --iters 3 --warmup 1", 7},
    {"cva", BENCH_CVA, "--size small --iters 3 --warmup 1", 25},
    {"sabr", BENCH_SABR, "--iterations 3 --warmup 1", 1500},
};

// The components `b` writes for `column`.
std::vector<double> gradient_of(const Bench& b, const std::string& column,
                                const testing_support::ScratchDir& dir)
{
    const std::string file = dir.path + "/" + column + ".txt";
    const std::string cmd = "ADB_GRAD_OUT=" + file + " " + b.exe + " --column " + column + " " + b.opts
                            + " --out " + dir.path + "/" + column + ".csv 2> /dev/null";
    EXPECT_EQ(std::system(cmd.c_str()), 0) << cmd;
    std::vector<double> g;
    std::ifstream in(file);
    for (double v; in >> v;) g.push_back(v);
    return g;
}

// One process of `b` (no children) with ADB_GRAD_OUT=file: its stderr lines
// that name ADB_GRAD_OUT.
std::vector<std::string> grad_out_lines(const Bench& b, const std::string& file, const std::string& dir)
{
    const std::string err = dir + "/stderr.txt";
    const std::string cmd = "ADB_GRAD_OUT=" + file + " " + b.exe + " --run-column xad --plain-median 1 " + b.opts
                            + " > /dev/null 2> " + err;
    EXPECT_EQ(std::system(cmd.c_str()), 0) << cmd;
    std::vector<std::string> lines;
    std::ifstream in(err);
    for (std::string l; std::getline(in, l);)
        if (l.find("ADB_GRAD_OUT") != std::string::npos) lines.push_back(l);
    return lines;
}

class GradOut : public ::testing::TestWithParam<Bench> {};

}  // namespace

TEST_P(GradOut, EveryAdColumnWritesTheSameGradient)
{
    const Bench& b = GetParam();
    const testing_support::ScratchDir dir("grad_out");
    const std::vector<double> ref = gradient_of(b, "xad", dir);
    ASSERT_EQ(ref.size(), b.gradient_size) << "xad";
    for (const char* column : {"cppad", "adept", "autodiff"}) {
        const std::vector<double> g = gradient_of(b, column, dir);
        const harness::CheckResult r = harness::check(g, ref, 1e-8);
        EXPECT_TRUE(r.ok) << column << ": " << g.size() << " components of " << ref.size() << ", worst " << r.worst_index
                          << ", rel error " << r.worst_rel_error;
    }
}

TEST_P(GradOut, FdColumnWritesItsGradient)
{
    const Bench& b = GetParam();
    const testing_support::ScratchDir dir("grad_out");
    const std::vector<double> ref = gradient_of(b, "xad", dir), fd = gradient_of(b, "fd", dir);
    ASSERT_EQ(fd.size(), b.gradient_size);
    const harness::CheckResult r = harness::check(fd, ref, 1e-3, 1e-6);
    EXPECT_TRUE(r.ok) << "worst " << r.worst_index << ": fd " << fd[r.worst_index] << ", xad " << ref[r.worst_index]
                      << ", rel error " << r.worst_rel_error;
}

TEST_P(GradOut, NothingIsWrittenWithoutIt)
{
    const Bench& b = GetParam();
    const testing_support::ScratchDir dir("grad_out");
    const std::string cmd = "cd " + dir.path + " && env -u ADB_GRAD_OUT " + b.exe + " --column xad " + b.opts
                            + " --out run.csv 2> /dev/null";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;
    std::set<std::string> written;
    for (const auto& f : std::filesystem::directory_iterator(dir.path)) written.insert(f.path().filename());
    EXPECT_EQ(written, std::set<std::string>{"run.csv"});
}

TEST_P(GradOut, OneProcessSaysSoOnceOnStderr)
{
    const testing_support::ScratchDir dir("grad_out");
    EXPECT_EQ(grad_out_lines(GetParam(), dir.path + "/g.txt", dir.path).size(), 1u);
}

TEST_P(GradOut, AFailedWriteSaysSoOnce)
{
    const testing_support::ScratchDir dir("grad_out");
    EXPECT_EQ(grad_out_lines(GetParam(), dir.path + "/no_such_dir/g.txt", dir.path).size(), 2u);
}

TEST_P(GradOut, AFailedWriteLeavesNoStaleContent)
{
    if (geteuid() == 0) GTEST_SKIP() << "root can write a read-only file";
    const testing_support::ScratchDir dir("grad_out");
    const std::filesystem::path old = dir.path + "/g.txt";
    std::ofstream(old) << "stale\n";
    std::filesystem::permissions(old, std::filesystem::perms::owner_read);
    grad_out_lines(GetParam(), old.string(), dir.path);
    std::string first_line;
    std::getline(std::ifstream(old), first_line);  // later iterations may write a new file there
    EXPECT_NE(first_line, "stale");
}

TEST_P(GradOut, AFailedWriteLeavesADirectoryInPlace)
{
    const testing_support::ScratchDir dir("grad_out");
    const std::filesystem::path empty = dir.path + "/empty";
    std::filesystem::create_directory(empty);
    grad_out_lines(GetParam(), empty.string(), dir.path);
    EXPECT_TRUE(std::filesystem::is_directory(empty));
}

INSTANTIATE_TEST_SUITE_P(Workloads, GradOut, ::testing::ValuesIn(benches),
                         [](const ::testing::TestParamInfo<Bench>& i) { return std::string(i.param.name); });
