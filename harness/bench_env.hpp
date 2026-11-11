// Glue shared by the four benchmark programs (bench_heston, bench_lmm,
// bench_cva, bench_sabr): the CPU model, the UTC date, the path of the running
// executable and the git SHA that fill the CSV rows, and the CLI options every
// program shares.

#pragma once

#include "harness/csv.hpp"

#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace harness {

// "model name" of /proc/cpuinfo without its label, "unknown" if absent.
inline std::string cpu_name()
{
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("model name", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos)
                return line.substr(line.find_first_not_of(' ', colon + 1));
        }
    return "unknown";
}

// The current UTC time as "YYYY-MM-DDTHH:MM:SSZ".
inline std::string utc_date()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Path of this executable: run_columns (harness/columns.hpp) re-invokes it,
// once per column.
inline std::string self_exe()
{
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "/proc/self/exe";
}

// ADB_GIT_SHA (set by CI), "unknown" when unset or empty.
inline std::string git_sha()
{
    const char* sha = std::getenv("ADB_GIT_SHA");
    return sha && *sha ? sha : "unknown";
}

// Record fields every bench program sets from the environment: compiler, CPU,
// date, git SHA. The workload, size, library and the measured fields stay
// with the caller.
inline Record env_record()
{
    Record r;
#ifdef __VERSION__
    r.compiler = __VERSION__;
#endif
    r.cpu = cpu_name();
    r.date = utc_date();
    r.git_sha = git_sha();
    return r;
}

// The CLI options all four bench programs share:
//   --column C        run only column C (parent side)
//   --run-column C    child side of run_columns: one column
//   --memory-pass     child side: the untimed memory pass
//   --plain-median S  the baseline's median (parent's plain run), child side
//   --out FILE        CSV to FILE instead of stdout
struct BenchCli {
    std::string column;
    std::string run_column;
    bool memory_pass = false;
    double plain_median = 0.0;
    std::string out;
    // The workload's size options (--paths, ...), passed on to the children.
    std::vector<std::string> forward;
};

// Where a bench program writes its CSV: the file --out names (path), else
// stdout.
class CsvOut {
public:
    explicit CsvOut(const std::string& path) : path_(path)
    {
        if (path_.empty()) return;
        file_.open(path_);
        if (!file_) throw std::runtime_error("cannot write " + path_);
    }

    std::ostream& stream() { return path_.empty() ? std::cout : file_; }

    // Flushes the stream; throws std::runtime_error when a write failed.
    void finish()
    {
        if (!stream().flush()) throw std::runtime_error("cannot write " + (path_.empty() ? "stdout" : path_));
    }

private:
    std::string path_;
    std::ofstream file_;
};

// The value of --plain-median: seconds, a positive normal number (a
// subnormal one would make every ratio overflow).
inline double plain_median_of(const std::string& v)
{
    char* end = nullptr;
    const double s = std::strtod(v.c_str(), &end);
    if (v.empty() || *end != '\0' || !std::isnormal(s) || s < 0.0)
        throw std::invalid_argument("--plain-median must be a positive, finite, normal number, not '" + v + "'");
    return s;
}

// The value of a count option (--paths, --iters, ...): a non-negative decimal
// integer, nothing else.
inline unsigned long long count_of(const std::string& option, const std::string& v)
{
    const bool digits = !v.empty() && v.find_first_not_of("0123456789") == std::string::npos;
    errno = 0;
    const unsigned long long n = digits ? std::strtoull(v.c_str(), nullptr, 10) : 0;
    if (!digits || errno == ERANGE)
        throw std::invalid_argument(option + " must be a non-negative integer, not '" + v + "'");
    return n;
}

// Parses the options above out of argv. Anything else goes to
// workload_option(a, argc, argv, i), which returns true when it consumed the
// option: a value-taking one advances i past its value (throwing
// std::invalid_argument when there is none) and, when the children need it
// too, pushes the name and the value into cli.forward. Throws
// std::invalid_argument for an option nobody consumed or a missing value.
template <class Option>
void parse_cli(int argc, char** argv, BenchCli& cli, Option&& workload_option)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--memory-pass") {
            cli.memory_pass = true;
        } else if (a == "--column" || a == "--run-column" || a == "--plain-median" || a == "--out") {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
            const std::string v = argv[++i];
            if (a == "--column") cli.column = v;
            else if (a == "--run-column") cli.run_column = v;
            else if (a == "--plain-median") cli.plain_median = plain_median_of(v);
            else cli.out = v;
        } else if (!workload_option(a, argc, argv, i)) {
            throw std::invalid_argument("unknown option " + a);
        }
    }
}

}  // namespace harness
