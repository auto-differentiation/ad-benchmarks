#include "harness/grad_note.hpp"
#include "harness/bench_env.hpp"
#include "workloads/lmm/bench.hpp"

#include "harness/builtin_columns.hpp"
#include "harness/columns.hpp"
#include "harness/csv.hpp"
#include "harness/extra_columns.hpp"
#include "harness/fd.hpp"
#include "harness/rng.hpp"
#include "harness/timer.hpp"
#include "versions.hpp"
#if defined(ADB_WITH_ADEPT)
#include "workloads/lmm/adept.hpp"
#endif
#if defined(ADB_WITH_AUTODIFF)
#include "workloads/lmm/autodiff.hpp"
#endif
#include "workloads/lmm/bench_inputs.hpp"
#if defined(ADB_WITH_CPPAD)
#include "workloads/lmm/cppad.hpp"
#endif
#if defined(ADB_WITH_XAD)
#include "workloads/lmm/xad.hpp"
#endif

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <functional>
#include <string>
#include <vector>

namespace lmm {
namespace {

volatile double sink = 0.0;  // keeps timed results alive
constexpr double fd_step = 1e-6;  // the fd column's central-difference step

}  // namespace

std::vector<std::string> column_names()
{
    std::vector<std::string> names = harness::builtin_column_names();
    for (const auto& e : harness::extra_columns<BenchInputs>()) names.push_back(e.name);
    return names;
}

void run_bench(const BenchConfig& cfg, std::ostream& csv, const std::string& column,
               bool memory_pass)
{
    BenchInputs bi{cfg.setup, cfg.inputs, {}, {}};
    const Setup& s = bi.setup;
    const Inputs<double>& in = bi.inputs;
    const std::vector<std::string> all = column_names();
    std::vector<std::string> names = all;
    if (!column.empty()) {
        if (std::find(all.begin(), all.end(), column) == all.end())
            throw std::invalid_argument("unknown column '" + column + "'");
        names = {column};
    }

    harness::Rng rng(cfg.seed);
    bi.z = rng.normals(s.paths * static_cast<std::size_t>(s.Nmat));
    const std::vector<double>& z = bi.z;
    const harness::Timer<> timer(cfg.warmup, cfg.iters);

    bi.x = in.L0;
    bi.x.insert(bi.x.end(), in.lambda.begin(), in.lambda.end());
    const std::vector<double>& x = bi.x;
    auto plain_of = [&s, &z](const std::vector<double>& v) {
        Inputs<double> p;
        p.L0.assign(v.begin(), v.begin() + s.N);
        p.lambda.assign(v.begin() + s.N, v.end());
        return price(s, p, z);
    };

    // Setup of a column (untimed) -> its timed body and library version.
    auto make = [&](const std::string& name, std::string& version) -> std::function<void()> {
        version = "n/a";
        if (name == "plain") {
            version = std::string("double, ") + adb::versions::cxx_flags;
            return [&] { sink = sink + price(s, in, z); };
        }
#if defined(ADB_WITH_XAD)
        if (name == "xad") {
            version = adb::versions::xad;
            auto g = std::make_shared<XadGradient>(s, z);
            return [g, &in] { sink = sink + adbg::note((*g)(in))[0]; };
        }
#endif
#if defined(ADB_WITH_CPPAD)
        if (name == "cppad") {
            version = adb::versions::cppad;
            auto g = std::make_shared<CppadGradient>(s, z);
            return [g, &in] { sink = sink + adbg::note((*g)(in))[0]; };
        }
#endif
#if defined(ADB_WITH_ADEPT)
        if (name == "adept") {
            version = adb::versions::adept;
            return [&] { sink = sink + adbg::note(adept_gradient(s, in, z))[0]; };
        }
#endif
#if defined(ADB_WITH_AUTODIFF)
        if (name == "autodiff") {
            version = adb::versions::autodiff;
            return [&] { sink = sink + adbg::note(autodiff_gradient(s, in, z))[0]; };
        }
#endif
        if (const auto* e = harness::find_extra_column<BenchInputs>(name)) {
            version = e->version;
            return e->setup(bi);
        }
        version = harness::fd_version(fd_step);
        return [&] { sink = sink + adbg::note(harness::fd_gradient(plain_of, x, fd_step))[0]; };
    };

    double plain_median = cfg.plain_median_s;

    harness::CsvWriter writer(csv);
    harness::Record base = harness::env_record();
    base.workload = "lmm";
    base.size = static_cast<std::size_t>(s.N);

    for (const std::string& name : names) {
        harness::Record r = base;
        r.library = name;
        if (memory_pass) {
            // Set-up (recording, optimisation, generation and compilation)
            // and one evaluation, from scratch.
            const std::uint64_t prealloc = harness::extra_static_prealloc<BenchInputs>(name);
            const harness::MemoryUse u =
                harness::measure_memory([&] { make(name, r.library_version)(); }, prealloc);
            r.peak_memory_bytes = u.rss_peak;
            r.peak_memory_growth_bytes = u.rss_growth;
            r.peak_heap_reserved_bytes = u.heap_reserved;
            writer.row(r);
            continue;
        }
        if (name != "plain" && !(plain_median > 0.0)) throw std::runtime_error("no --plain-median for column " + name);
        const harness::TimingStats t = timer.run(make(name, r.library_version));  // memory fields stay 0
        if (name == "plain" && cfg.plain_median_s <= 0.0) plain_median = t.median;
        r.median_s = t.median;
        r.iqr_s = t.iqr;
        r.plain_median_s = plain_median;
        r.ratio = t.median / plain_median;
        writer.row(r);
    }
}

}  // namespace lmm
