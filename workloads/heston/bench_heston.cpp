// Heston MC Greeks benchmark: the plain double price and the full gradient of
// every column (AD libraries and central FD) on the same inputs, book, draws
// and timer; one CSV row per column (harness::CsvWriter).
//
//   bench_heston --paths N --steps M [--warmup W] [--iters I] [--column C] [--out file.csv]
//   bench_heston --run-column C [--memory-pass] --paths N --steps M ...   (child side)
//
// Every column runs in its own processes (harness::run_columns): an untimed
// memory pass under the heap hooks (setup + one evaluation inside
// harness::measure_memory) and a timed pass. plain is timed once first; its
// median reaches the other timed passes as --plain-median, so every row has
// the same plain_median_s and ratio = median_s / plain_median_s.
//
// Time grid: one year in M steps, dt = 1 / M, so the book (maturities 0.5 and
// 1 year) fits any M >= 2 (M even keeps 0.5 on the grid). Inputs and book are
// heston::standard_x() and standard_book() (bench_inputs.hpp), which the
// tests use too. Library tape sizes are printed to stderr.
//
// Columns registered for heston::BenchInputs from outside this file
// (harness/extra_columns.hpp) are accepted too and run after the built-ins.

#include "harness/grad_note.hpp"
#include "harness/bench_env.hpp"
#include "harness/builtin_columns.hpp"
#include "harness/columns.hpp"
#include "harness/csv.hpp"
#include "harness/extra_columns.hpp"
#include "harness/fd.hpp"
#include "harness/timer.hpp"
#include "versions.hpp"
#if defined(ADB_WITH_ADEPT)
#include "workloads/heston/adept.hpp"
#endif
#if defined(ADB_WITH_AUTODIFF)
#include "workloads/heston/autodiff.hpp"
#endif
#include "workloads/heston/bench_inputs.hpp"
#if defined(ADB_WITH_CPPAD)
#include "workloads/heston/cppad.hpp"
#endif
#include "workloads/heston/fd.hpp"
#include "workloads/heston/model.hpp"
#if defined(ADB_WITH_XAD)
#include "workloads/heston/xad.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

volatile double sink = 0.0;  // keeps every timed result alive
constexpr double fd_step = 1e-5;  // the fd column's central-difference step

void consume(double v) { sink = sink + v; }
void consume(const std::vector<double>& g) { adbg::note(g); consume(g.empty() ? 0.0 : g[0]); }

// Prints a library's tape size to stderr, once per library.
void tape_note(const char* library, std::size_t bytes)
{
    static std::vector<std::string> done;
    if (std::find(done.begin(), done.end(), library) != done.end()) return;
    done.emplace_back(library);
    std::cerr << library << ": tape " << bytes << " bytes (shared part + one path)\n";
}

// Built-in columns, then the registered extra ones.
std::vector<std::string> column_names()
{
    std::vector<std::string> names = harness::builtin_column_names();
    for (const auto& e : harness::extra_columns<heston::BenchInputs>()) names.push_back(e.name);
    return names;
}

struct Options {
    std::size_t paths = 1000, steps = 100, warmup = 2, iters = 10;
    harness::BenchCli cli;
};

Options parse(int argc, char** argv)
{
    Options o;
    const std::vector<std::string> names = column_names();
    const auto size_option = [&](const std::string& a, int argc, char** argv, int& i) {
        if (a != "--paths" && a != "--steps" && a != "--warmup" && a != "--iters") return false;
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
        const std::string v = argv[++i];
        const std::size_t n = harness::count_of(a, v);
        if (a == "--paths") o.paths = n;
        else if (a == "--steps") o.steps = n;
        else if (a == "--warmup") o.warmup = n;
        else o.iters = n;
        o.cli.forward.push_back(a);
        o.cli.forward.push_back(v);
        return true;
    };
    harness::parse_cli(argc, argv, o.cli, size_option);
    for (const std::string& c : {o.cli.column, o.cli.run_column})
        if (!c.empty() && std::find(names.begin(), names.end(), c) == names.end())
            throw std::invalid_argument("unknown column " + c);
    if (o.paths == 0 || o.steps == 0 || o.iters == 0) throw std::invalid_argument("paths, steps, iters must be positive");
    return o;
}

void run_parent(const Options& o, std::ostream& out)
{
    harness::run_columns(harness::self_exe(), ADB_HEAP_PEAK_LIB,
                         o.cli.column.empty() ? column_names() : std::vector<std::string>{o.cli.column},
                         o.cli.forward, out, "plain", o.cli.plain_median);
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const Options o = parse(argc, argv);
        harness::CsvOut csv_out(o.cli.out);
        std::ostream& out = csv_out.stream();
        if (o.cli.run_column.empty()) {
            run_parent(o, out);
            csv_out.finish();
            return 0;
        }

        // Child: one column, timed or (--memory-pass) measured.
        const heston::BenchInputs in{heston::standard_x(), heston::standard_book(),
                                     heston::make_draws(o.paths, o.steps, /*seed=*/7),
                                     1.0 / static_cast<double>(o.steps)};
        const std::vector<double>& x = in.x;
        const std::vector<heston::Option>& book = in.book;
        const heston::Draws& d = in.draws;
        const double dt = in.dt;
        const heston::Params<double> p{x[0], x[1], x[2], x[3], x[4], x[5], x[6]};
        const harness::Timer<> timer(o.warmup, o.iters);

        harness::CsvWriter csv(out);
        harness::Record r = harness::env_record();
        r.workload = "heston";
        r.size = o.paths;
        r.library = o.cli.run_column;

        // Setup of the column (untimed) -> its timed body.
        const std::string& c = o.cli.run_column;
        auto setup = [&]() -> std::function<void()> {
            if (c == "plain") {
                r.library_version = std::string("double, ") + adb::versions::cxx_flags;
                return [&] { consume(heston::price(p, book, d, dt)); };
            }
#if defined(ADB_WITH_XAD)
            if (c == "xad") {
                r.library_version = adb::versions::xad;
                auto g = std::make_shared<heston::XadGradient>(book, d, dt);
                return [&x, g] {
                    consume((*g)(x));
                    tape_note("xad", g->peak_tape_bytes());
                };
            }
#endif
#if defined(ADB_WITH_CPPAD)
            if (c == "cppad") {
                r.library_version = adb::versions::cppad;
                auto g = std::make_shared<heston::CppadGradient>(x, book, d, dt);
                return [&x, g] { consume((*g)(x)); };
            }
#endif
#if defined(ADB_WITH_ADEPT)
            if (c == "adept") {
                r.library_version = adb::versions::adept;
                auto g = std::make_shared<heston::AdeptGradient>(book, d, dt);
                return [&x, g] {
                    consume((*g)(x));
                    tape_note("adept", g->peak_tape_bytes());
                };
            }
#endif
#if defined(ADB_WITH_AUTODIFF)
            if (c == "autodiff") {
                r.library_version = std::string(adb::versions::autodiff) + " (forward dual, 7 passes)";
                return [&] { consume(heston::autodiff_gradient(x, book, d, dt)); };
            }
#endif
            if (const auto* e = harness::find_extra_column<heston::BenchInputs>(c)) {
                r.library_version = e->version;
                return e->setup(in);
            }
            r.library_version = harness::fd_version(fd_step);
            return [&] { consume(heston::fd_gradient(x, book, d, dt, fd_step)); };
        };

        if (o.cli.memory_pass) {
            const std::uint64_t prealloc = harness::extra_static_prealloc<heston::BenchInputs>(c);
            const harness::MemoryUse u = harness::measure_memory([&] { setup()(); }, prealloc);
            r.peak_memory_bytes = u.rss_peak;
            r.peak_memory_growth_bytes = u.rss_growth;
            r.peak_heap_reserved_bytes = u.heap_reserved;
        } else {
            if (c != "plain" && !(o.cli.plain_median > 0.0))
                throw std::runtime_error("no --plain-median for column " + c);
            const std::function<void()> body = setup();
            const harness::TimingStats t = timer.run(body);
            const double plain_median = c == "plain" ? t.median : o.cli.plain_median;
            r.median_s = t.median;
            r.iqr_s = t.iqr;
            r.plain_median_s = plain_median;
            r.ratio = t.median / plain_median;
            std::cerr << c << ": " << r.ratio << "x plain\n";
        }
        csv.row(r);
        csv_out.finish();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "bench_heston: " << e.what() << '\n';
        return 1;
    }
}
