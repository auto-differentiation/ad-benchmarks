#include "harness/grad_note.hpp"
#include "harness/bench_env.hpp"
#include "workloads/sabr/bench.hpp"

#include "harness/builtin_columns.hpp"
#include "harness/columns.hpp"
#include "harness/csv.hpp"
#include "harness/extra_columns.hpp"
#include "harness/fd.hpp"
#include "harness/timer.hpp"
#include "versions.hpp"
#if defined(ADB_WITH_ADEPT)
#include "workloads/sabr/adept.hpp"
#endif
#if defined(ADB_WITH_AUTODIFF)
#include "workloads/sabr/autodiff.hpp"
#endif
#include "workloads/sabr/bench_inputs.hpp"
#if defined(ADB_WITH_CPPAD)
#include "workloads/sabr/cppad.hpp"
#endif
#include "workloads/sabr/fd.hpp"
#include "workloads/sabr/model.hpp"
#if defined(ADB_WITH_XAD)
#include "workloads/sabr/xad.hpp"
#endif

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace sabr {
namespace {

volatile double sink = 0.0;  // keeps timed results alive
constexpr double fd_step = 1e-6;  // the fd column's central-difference step

// Untimed set-up of a column -> its timed body (one evaluation at x).
std::function<void()> make_body(const std::string& name, const BenchInputs& in, std::string& version)
{
    const Market& m = in.market;
    const std::vector<double>& x = in.x;
    version = "n/a";
    auto jacobian = [&x](auto j) -> std::function<void()> {
        return [j, &x] { sink = sink + adbg::note((*j)(x))[1]; };
    };
    if (name == "plain") {
        version = std::string("double, ") + adb::versions::cxx_flags;
        auto t = std::make_shared<const std::vector<QuoteTerms>>(quote_terms(m));
        return [t, &m, &x] { sink = sink + residuals(m, *t, x)[0]; };
    }
#if defined(ADB_WITH_XAD)
    if (name == "xad") {
        version = adb::versions::xad;
        return jacobian(std::make_shared<XadJacobian>(m));
    }
#endif
#if defined(ADB_WITH_CPPAD)
    if (name == "cppad") {
        version = adb::versions::cppad;
        return jacobian(std::make_shared<CppadJacobian>(m));
    }
#endif
#if defined(ADB_WITH_ADEPT)
    if (name == "adept") {
        version = adb::versions::adept;
        return jacobian(std::make_shared<AdeptJacobian>(m));
    }
#endif
#if defined(ADB_WITH_AUTODIFF)
    if (name == "autodiff") {
        version = adb::versions::autodiff;
        return jacobian(std::make_shared<AutodiffJacobian>(m));
    }
#endif
    if (name == "fd") {
        version = harness::fd_version(fd_step);
        return jacobian(std::make_shared<FdJacobian>(m, fd_step));
    }
    if (const auto* e = harness::find_extra_column<BenchInputs>(name)) {
        version = e->version;
        return e->setup(in);
    }
    throw std::invalid_argument("unknown column '" + name + "'");
}

}  // namespace

const std::vector<std::string>& column_names()
{
    static const std::vector<std::string> names = harness::builtin_column_names();
    return names;
}

void run_column(const std::string& column, const ColumnOptions& opts, std::ostream& csv)
{
    const BenchInputs in{standard_market(), standard_params()};
    const Market& m = in.market;

    harness::Record r = harness::env_record();
    r.library = column;
    r.workload = "sabr";
    r.size = m.K.size();

    if (opts.memory_pass) {
        const harness::MemoryUse u = harness::measure_memory([&] { make_body(column, in, r.library_version)(); },
                                                             harness::extra_static_prealloc<BenchInputs>(column));
        r.peak_memory_bytes = u.rss_peak;
        r.peak_memory_growth_bytes = u.rss_growth;
        r.peak_heap_reserved_bytes = u.heap_reserved;
    } else {
        if (column != "plain" && !(opts.plain_median_s > 0.0))
            throw std::runtime_error("no --plain-median for column " + column);
        const harness::Timer<> timer(opts.warmup, opts.iterations);
        const harness::TimingStats t = timer.run(make_body(column, in, r.library_version));
        const double plain = column == "plain" ? t.median : opts.plain_median_s;
        r.median_s = t.median;
        r.iqr_s = t.iqr;
        r.plain_median_s = plain;
        r.ratio = t.median / plain;
    }
    harness::CsvWriter(csv).row(r);
}

}  // namespace sabr
