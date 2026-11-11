// Generic benchmark driver: every column runs in fresh child processes, so a
// column's timing and memory never include what an earlier column left behind
// (global tapes, allocator caches).
//
// Per column, run_columns starts the benchmark executable twice:
//   1. memory pass, untimed: `<exe> --run-column <c> --memory-pass <args...>`
//      with LD_PRELOAD=<heap hooks> (harness/heap_peak_hooks.cpp). The child
//      evaluates the column once inside measure_memory() and writes the
//      harness CSV header and one row with the memory fields: the headline
//      peak_memory_bytes / peak_memory_growth_bytes are RESIDENT memory (what
//      the process actually touches; untouched reservations do not count),
//      and peak_heap_reserved_bytes is the heap counter's growth (bytes
//      allocated, touched or not) plus the column's own static
//      preallocation, as an extra column declares it
//      (harness::extra_static_prealloc; see measure_memory);
//   2. timed pass, no hooks: `<exe> --run-column <c> <args...>`, header and
//      one row with the timings.
// The output row is the timed row with the memory fields taken from the
// memory row.
// Baseline (default "plain"): its timed pass runs first, once, and its
// median_s is passed to every other column's timed pass as
// `--plain-median <s>`, so all rows share one plain_median_s and ratio =
// median_s / that median. The baseline row itself is that first run. An
// empty baseline disables this. A given baseline_median (> 0, e.g. the
// parent's own --plain-median) replaces that first run: no baseline child,
// every timed pass gets `--plain-median <baseline_median>`. The first child's header is written once; every child must
// print the same header. A child that cannot start, exits non-zero, or
// prints a different header or not exactly one row is an error
// (std::runtime_error). Children inherit stderr.

#pragma once

#include "harness/heap_peak.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <malloc.h>
#include <sstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace harness {

// Memory use of one evaluation, all in bytes:
// - rss_peak: peak resident set size of the process while body() ran
//   (Linux VmHWM, reset through /proc/self/clear_refs after malloc_trim just
//   before; if the reset is unavailable, the process-lifetime peak);
// - rss_growth: rss_peak minus the resident size just before body() (VmRSS);
// - heap_peak: peak heap bytes allocated while body() ran (heap hooks), and
//   heap_growth: heap_peak minus the heap allocated just before, counting
//   reserved but untouched memory too;
// - heap_reserved: heap_growth + static_prealloc, the heap the column's
//   library reserves before main() (e.g. a global tape), which every process
//   of the benchmark carries and so is not in heap_growth. An extra column
//   declares it (ExtraColumn::static_prealloc, harness/extra_columns.hpp),
//   e.g. measured with peak_heap_bytes() around constructing another
//   instance of that global object.
struct MemoryUse {
    std::uint64_t rss_peak = 0;
    std::uint64_t rss_growth = 0;
    std::uint64_t heap_peak = 0;
    std::uint64_t heap_growth = 0;
    std::uint64_t heap_reserved = 0;
};

namespace detail {

// "VmHWM" or "VmRSS" of this process in bytes, 0 if absent.
inline std::uint64_t proc_status_bytes(const char* key)
{
    std::ifstream f("/proc/self/status");
    std::string line;
    const std::size_t n = std::strlen(key);
    while (std::getline(f, line)) {
        if (line.compare(0, n, key) == 0 && line.size() > n && line[n] == ':') {
            std::istringstream ss(line.substr(n + 1));
            std::uint64_t kb = 0;
            ss >> kb;
            return kb * 1024;
        }
    }
    return 0;
}

// Returns allocator caches to the system, then sets VmHWM to the current RSS.
inline bool reset_rss_peak()
{
    malloc_trim(0);
    std::ofstream f("/proc/self/clear_refs");
    if (!f) return false;
    f << "5";
    f.flush();
    return static_cast<bool>(f);
}

}  // namespace detail

// For the memory pass; throws if the heap hooks are not preloaded.
template <class F>
MemoryUse measure_memory(F&& body, std::uint64_t static_prealloc = 0)
{
    if (!heap_peak_available())
        throw std::runtime_error("memory pass without the heap hooks (LD_PRELOAD)");
    const auto& h = detail::heap_peak_hooks();
    detail::reset_rss_peak();
    const std::uint64_t rss_start = detail::proc_status_bytes("VmRSS");
    const std::uint64_t heap_start = h.reset();
    body();
    MemoryUse u;
    u.heap_peak = h.peak();
    u.rss_peak = detail::proc_status_bytes("VmHWM");
    u.heap_growth = u.heap_peak > heap_start ? u.heap_peak - heap_start : 0;
    u.rss_growth = u.rss_peak > rss_start ? u.rss_peak - rss_start : 0;
    u.heap_reserved = u.heap_growth + static_prealloc;
    return u;
}

namespace detail {

// stdout of `exe argv...` (env entries "K=V" added); throws unless the child
// exits with status 0.
inline std::string run_child(const std::string& exe, const std::vector<std::string>& argv,
                             const std::vector<std::string>& env = {})
{
    int fd[2];
    if (pipe(fd) != 0) throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    const pid_t pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        throw std::runtime_error(std::string("fork: ") + std::strerror(errno));
    }
    if (pid == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        for (const std::string& e : env) {
            const std::size_t eq = e.find('=');
            setenv(e.substr(0, eq).c_str(), e.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> args;
        args.push_back(const_cast<char*>(exe.c_str()));
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execv(exe.c_str(), args.data());
        _exit(127);
    }
    close(fd[1]);
    std::string out;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(fd[0], buf, sizeof buf);
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0 || errno != EINTR) {
            break;
        }
    }
    close(fd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("child '" + exe + "' failed (status " + std::to_string(status) + ")");
    return out;
}

// CSV line -> raw fields (quotes kept), split at commas outside quotes.
inline std::vector<std::string> split_raw(const std::string& line)
{
    std::vector<std::string> f(1);
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        if (c == ',' && !quoted) {
            f.emplace_back();
        } else {
            f.back() += c;
        }
    }
    return f;
}

inline std::string join_raw(const std::vector<std::string>& f)
{
    std::string s;
    for (std::size_t i = 0; i < f.size(); ++i) s += (i ? "," : "") + f[i];
    return s;
}

// Header line and its single row line.
inline void header_and_row(const std::string& text, const std::string& column, std::string& header,
                           std::string& row)
{
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        if (eol > pos) lines.push_back(text.substr(pos, eol - pos));
        pos = eol + 1;
    }
    if (lines.size() != 2)
        throw std::runtime_error("column " + column + ": expected a CSV header and one row");
    header = lines[0];
    row = lines[1];
}

}  // namespace detail

// See the file comment. exe is the benchmark executable (typically the
// running one), heap_hooks the path of the adb_heap_peak shared library.
inline void run_columns(const std::string& exe, const std::string& heap_hooks,
                        const std::vector<std::string>& columns,
                        const std::vector<std::string>& args, std::ostream& out,
                        const std::string& baseline = "plain", double baseline_median = 0.0)
{
    std::string base_header, base_row;
    std::vector<std::string> timed_extra;
    const bool given = !baseline.empty() && baseline_median > 0.0;
    if (given) {
        std::ostringstream s;
        s.precision(17);
        s << baseline_median;
        timed_extra = {"--plain-median", s.str()};
    } else if (!baseline.empty()) {
        std::vector<std::string> argv{"--run-column", baseline};
        argv.insert(argv.end(), args.begin(), args.end());
        detail::header_and_row(detail::run_child(exe, argv), baseline, base_header, base_row);
        const std::vector<std::string> names = detail::split_raw(base_header);
        const std::vector<std::string> f = detail::split_raw(base_row);
        std::size_t i = 0;
        while (i < names.size() && names[i] != "median_s") ++i;
        if (i == names.size() || f.size() != names.size())
            throw std::runtime_error("baseline " + baseline + ": no median_s");
        timed_extra = {"--plain-median", f[i]};
    }

    std::string header;
    for (const std::string& c : columns) {
        std::vector<std::string> argv{"--run-column", c, "--memory-pass"};
        argv.insert(argv.end(), args.begin(), args.end());
        std::string mem_header, mem_row;
        detail::header_and_row(detail::run_child(exe, argv, {"LD_PRELOAD=" + heap_hooks}), c,
                               mem_header, mem_row);

        std::string time_header, time_row;
        if (!baseline.empty() && !given && c == baseline) {
            time_header = base_header;
            time_row = base_row;
        } else {
            argv.erase(argv.begin() + 2);  // timed pass: no --memory-pass, no hooks
            argv.insert(argv.begin() + 2, timed_extra.begin(), timed_extra.end());
            detail::header_and_row(detail::run_child(exe, argv), c, time_header, time_row);
        }

        if (header.empty()) {
            header = time_header;
            out << header << '\n';
        }
        if (time_header != header || mem_header != header)
            throw std::runtime_error("column " + c + ": CSV header differs");

        const std::vector<std::string> names = detail::split_raw(header);
        const std::vector<std::string> mem = detail::split_raw(mem_row);
        std::vector<std::string> row = detail::split_raw(time_row);
        if (mem.size() != names.size() || row.size() != names.size())
            throw std::runtime_error("column " + c + ": row does not match the header");
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == "peak_memory_bytes" || names[i] == "peak_memory_growth_bytes" ||
                names[i] == "peak_heap_reserved_bytes")
                row[i] = mem[i];
        out << detail::join_raw(row) << '\n';
        out.flush();
    }
}

}  // namespace harness
