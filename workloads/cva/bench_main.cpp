// bench_cva: CVA swap-portfolio benchmark, shared CSV on stdout or --out.
//
// Cases (3 factors, quarterly exposure dates ending at the last swap
// maturity, so every curve pillar up to it is live):
//   --size small   6 swaps up to 5y, 200 paths;
//   --size medium  20 swaps up to 10y, 500 paths (default).
// Options: --paths P, --iters I, --warmup W, --seed S, --out FILE.
//
// bench_cva [opts]                every column (plain, xad, cppad, adept,
//                                 autodiff, fd), each in its
//                                 own processes through harness::run_columns;
// bench_cva --column C [opts]     only column C, the same way;
// bench_cva --run-column C [--memory-pass] [opts]
//                                 the child side of harness::run_columns.
//
// Timed pass: one gradient (plain: one CVA) per call with harness::Timer, the
// CppAD recording done before timing. harness::run_columns times
// plain once and passes its median to the other children (--plain-median),
// so every row has the same plain_median_s and ratio = median_s / it.
// Memory pass (heap hooks preloaded, untimed): one run_column from scratch
// (recording included) inside harness::measure_memory:
// peak_memory_bytes = peak resident set, peak_memory_growth_bytes = its growth
// over the resident set before, peak_heap_reserved_bytes = heap allocated
// during the run plus the column's static preallocation (an extra column's
// own declaration).
//
// Columns registered for cva::BenchInputs from outside this file
// (harness/extra_columns.hpp) are accepted too and run after the built-ins;
// their memory pass is their setup plus one call of their body.

#include "harness/grad_note.hpp"
#include "harness/bench_env.hpp"
#include "workloads/cva/columns.hpp"

#include "harness/columns.hpp"
#include "harness/csv.hpp"
#include "harness/extra_columns.hpp"
#include "harness/fd.hpp"
#include "harness/rng.hpp"
#include "harness/timer.hpp"
#include "versions.hpp"
#include "workloads/cva/bench_inputs.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

volatile double sink = 0.0;  // keeps timed results alive

// Swaps cycle through starts 0, 0.25, 0.5, 0.75, tenors and payer/receiver;
// fixed rates around the forward. Dates are quarterly to the last maturity.
// x and z are left empty.
cva::BenchInputs make_case(const std::string& size, std::size_t paths)
{
    const bool small = size == "small";
    const std::size_t n_swaps = small ? 6 : 20;
    const int max_tenor = small ? 4 : 9;
    cva::BenchInputs s;
    cva::Config& c = s.config;
    cva::Inputs<double>& in = s.inputs;
    c.mean_reversions = {0.05, 0.3, 1.0};
    in.vols = {0.010, 0.008, 0.006};
    in.correlations = {-0.6, 0.3, -0.2};
    c.curve_times = {0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 15.0};
    in.zero_rates = {0.020, 0.021, 0.023, 0.025, 0.028, 0.030, 0.032, 0.034};
    c.hazard_times = {1.0, 3.0, 5.0, 10.0};
    in.hazard_rates = {0.010, 0.012, 0.015, 0.020};
    in.lgd = 0.6;
    double last = 0.0;
    for (std::size_t k = 0; k < n_swaps; ++k) {
        cva::Swap w;
        w.notional = 100.0;
        w.start = 0.25 * double(k % 4);
        w.maturity = w.start + 1.0 + double(k % std::size_t(max_tenor));
        w.period = k % 2 ? 0.5 : 1.0;
        w.payer = k % 3 != 0;
        c.swaps.push_back(w);
        in.fixed_rates.push_back(0.025 + 0.0005 * double(k));
        last = std::max(last, w.maturity);
    }
    for (int q = 1; 0.25 * q <= last + 1e-12; ++q) c.dates.push_back(0.25 * q);
    c.n_paths = paths;
    return s;
}

std::string version_of(const std::string& name)
{
    if (name == "plain") return std::string("double, ") + adb::versions::cxx_flags;
    if (name == "xad") return adb::versions::xad;
    if (name == "cppad") return adb::versions::cppad;
    if (name == "adept") return adb::versions::adept;
    if (name == "autodiff") return adb::versions::autodiff;
    if (name == "fd") return harness::fd_version(cva::fd_step);
    if (const auto* e = harness::find_extra_column<cva::BenchInputs>(name)) return e->version;
    return "n/a";
}

// Untimed setup of a column -> its timed body.
std::function<void()> make_body(const std::string& name, const cva::BenchInputs& s, const std::vector<double>& z)
{
    const cva::Inputs<double>& in = s.inputs;
    const cva::Config& c = s.config;
    if (const auto* e = harness::find_extra_column<cva::BenchInputs>(name)) return e->setup(s);
    // The configuration-only Plan is part of the setup, for every built-in column.
    const auto pl = std::make_shared<const cva::Plan>(cva::make_plan(c));
    if (name == "plain") return [&, pl] { sink = sink + cva::cva(in, c, *pl, z); };
#if defined(ADB_WITH_XAD)
    if (name == "xad") {
        auto g = std::make_shared<cva::XadGradient>(c, *pl, z);
        return [&in, pl, g] { sink = sink + adbg::note((*g)(in))[0]; };
    }
#endif
#if defined(ADB_WITH_CPPAD)
    if (name == "cppad") {
        auto f = std::make_shared<cva::CppadCva>(in, c, z);
        return [f, &in, &z] { sink = sink + adbg::note(f->gradient(in, z))[0]; };
    }
#endif
#if defined(ADB_WITH_ADEPT)
    if (name == "adept") return [&, pl] { sink = sink + adbg::note(cva::gradient_adept(in, c, *pl, z))[0]; };
#endif
#if defined(ADB_WITH_AUTODIFF)
    if (name == "autodiff") return [&, pl] { sink = sink + adbg::note(cva::gradient_autodiff(in, c, *pl, z))[0]; };
#endif
    if (name == "fd") return [&, pl] { sink = sink + adbg::note(cva::gradient_fd(in, c, *pl, z, cva::fd_step))[0]; };
    throw std::invalid_argument("unknown column '" + name + "'");
}

int usage()
{
    std::cerr << "usage: bench_cva [--column C] [--size small|medium] [--paths P] [--iters I] "
                 "[--warmup W] [--seed S] [--out FILE]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    std::string size = "medium";
    std::size_t paths = 0, iters = 10, warmup = 2;
    unsigned long long seed = 42;
    harness::BenchCli cli;

    const auto size_option = [&](const std::string& a, int argc, char** argv, int& i) {
        if (a == "--size") {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
            size = argv[++i];
            if (size != "small" && size != "medium") throw std::invalid_argument("--size must be small or medium");
            cli.forward.push_back(a);
            cli.forward.push_back(size);
            return true;
        }
        if (a != "--paths" && a != "--iters" && a != "--warmup" && a != "--seed") return false;
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
        const std::string v = argv[++i];
        const unsigned long long n = harness::count_of(a, v);
        if (a == "--paths") paths = n;
        if (a == "--iters") iters = n;
        if (a == "--warmup") warmup = n;
        if (a == "--seed") seed = n;
        cli.forward.push_back(a);
        cli.forward.push_back(v);
        return true;
    };
    try {
        harness::parse_cli(argc, argv, cli, size_option);
    } catch (const std::exception& e) {
        std::cerr << "bench_cva: " << e.what() << '\n';
        return usage();
    }
    if (paths == 0) paths = size == "small" ? 200 : 500;
    if (iters == 0) {
        std::cerr << "bench_cva: --iters must be positive\n";
        return 2;
    }
    // Built-in columns, then the registered extra ones.
    std::vector<std::string> names = cva::column_names();
    for (const auto& e : harness::extra_columns<cva::BenchInputs>()) names.push_back(e.name);
    for (const std::string& c : {cli.column, cli.run_column}) {
        if (!c.empty() && std::find(names.begin(), names.end(), c) == names.end()) {
            std::cerr << "bench_cva: unknown column '" << c << "'\n";
            return 2;
        }
    }

    try {
        harness::CsvOut out(cli.out);
        if (cli.run_column.empty()) {
            harness::run_columns(harness::self_exe(), ADB_HEAP_PEAK_LIB,
                                 cli.column.empty() ? names : std::vector<std::string>{cli.column},
                                 cli.forward, out.stream(), "plain", cli.plain_median);
            out.finish();
            return 0;
        }

        cva::BenchInputs s = make_case(size, paths);
        harness::Rng rng(seed);
        s.z = rng.normals(cva::n_normals(s.config));
        s.x = cva::to_vector(s.inputs);
        const std::vector<double>& z = s.z;

        harness::Record r = harness::env_record();
        r.library = cli.run_column;
        r.workload = "cva";
        r.size = paths;
        r.library_version = version_of(cli.run_column);

        if (cli.memory_pass) {
            const bool extra = harness::find_extra_column<cva::BenchInputs>(cli.run_column) != nullptr;
            const std::uint64_t prealloc = harness::extra_static_prealloc<cva::BenchInputs>(cli.run_column);
            const harness::MemoryUse u = harness::measure_memory(
                [&] {
                    if (extra) make_body(cli.run_column, s, z)();
                    else cva::run_column(cli.run_column, s.inputs, s.config, z);
                },
                prealloc);
            r.peak_memory_bytes = u.rss_peak;
            r.peak_memory_growth_bytes = u.rss_growth;
            r.peak_heap_reserved_bytes = u.heap_reserved;
        } else {
            if (cli.run_column != "plain" && !(cli.plain_median > 0.0))
                throw std::runtime_error("no --plain-median for column " + cli.run_column);
            const harness::Timer<> timer(warmup, iters);
            const harness::TimingStats t = timer.run(make_body(cli.run_column, s, z));
            const double plain_median = cli.run_column == "plain" ? t.median : cli.plain_median;
            r.median_s = t.median;
            r.iqr_s = t.iqr;
            r.plain_median_s = plain_median;
            r.ratio = t.median / plain_median;
        }
        harness::CsvWriter(out.stream()).row(r);
        out.finish();
    } catch (const std::exception& e) {
        std::cerr << "bench_cva: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
