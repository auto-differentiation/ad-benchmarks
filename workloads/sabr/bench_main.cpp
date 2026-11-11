// bench_sabr: SABR smile calibration benchmark, shared CSV on stdout or --out.
// Options: --warmup N (100), --iterations N (2000), --out FILE.
//
// bench_sabr [opts]               every column (plain, xad, cppad, adept,
//                                 autodiff, fd), each in its
//                                 own processes through harness::run_columns
//                                 (untimed memory pass with the heap hooks,
//                                 then a timed pass; plain is timed once and
//                                 its median shared by every row);
// bench_sabr --column C [opts]    only column C, the same way;
// bench_sabr --run-column C [--memory-pass] [--plain-median S] [opts]
//                                 the child side of harness::run_columns
//                                 (sabr::run_column).
//
// Columns registered for sabr::BenchInputs from outside this file
// (harness/extra_columns.hpp) are accepted too and run after the built-ins;
// their memory pass is their setup plus one call of their body.

#include "harness/bench_env.hpp"
#include "harness/columns.hpp"
#include "harness/extra_columns.hpp"
#include "workloads/sabr/bench_inputs.hpp"
#include "workloads/sabr/bench.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int usage()
{
    std::cerr << "usage: bench_sabr [--column C] [--warmup N] [--iterations N] [--out FILE]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    sabr::ColumnOptions opts;
    harness::BenchCli cli;

    const auto size_option = [&](const std::string& a, int argc, char** argv, int& i) {
        if (a != "--warmup" && a != "--iterations") return false;
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
        const std::string v = argv[++i];
        const std::size_t n = harness::count_of(a, v);
        (a == "--warmup" ? opts.warmup : opts.iterations) = n;
        cli.forward.push_back(a);
        cli.forward.push_back(v);
        return true;
    };
    try {
        harness::parse_cli(argc, argv, cli, size_option);
    } catch (const std::exception& e) {
        std::cerr << "bench_sabr: " << e.what() << '\n';
        return usage();
    }
    if (opts.iterations == 0) {
        std::cerr << "bench_sabr: --iterations must be positive\n";
        return 2;
    }
    // Built-in columns, then the registered extra ones.
    std::vector<std::string> names = sabr::column_names();
    for (const auto& e : harness::extra_columns<sabr::BenchInputs>()) names.push_back(e.name);
    for (const std::string& c : {cli.column, cli.run_column}) {
        if (!c.empty() && std::find(names.begin(), names.end(), c) == names.end()) {
            std::cerr << "bench_sabr: unknown column '" << c << "'\n";
            return 2;
        }
    }

    try {
        harness::CsvOut out(cli.out);
        if (!cli.run_column.empty()) {
            opts.memory_pass = cli.memory_pass;
            opts.plain_median_s = cli.plain_median;
            sabr::run_column(cli.run_column, opts, out.stream());
            out.finish();
            return 0;
        }
        harness::run_columns(harness::self_exe(), ADB_HEAP_PEAK_LIB,
                             cli.column.empty() ? names : std::vector<std::string>{cli.column},
                             cli.forward, out.stream(), "plain", cli.plain_median);
        out.finish();
    } catch (const std::exception& e) {
        std::cerr << "bench_sabr: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
