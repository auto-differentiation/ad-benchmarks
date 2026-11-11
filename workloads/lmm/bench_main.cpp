// bench_lmm: LMM swaption portfolio benchmark, CSV on stdout.
//
// Default case (paper size): N = 80 forwards, Nmat = 40 steps, delta 0.25,
// L0 = 0.05, lambda = 0.2, 15 payer swaptions (lengths 4, 8, 20, 28, 40
// periods x strikes 4.5%, 5%, 5.5%).
// --small: the test case of tests/lmm/lmm_case.hpp (N = 16, Nmat = 8, lengths
// 2, 4, 6, 8 x strikes 4.5%, 5%, 5.5%, L0[i] = 0.05 + 0.0005 i,
// lambda[k] = 0.2 - 0.002 k).
// Options: --paths P (10000) --iters I (10) --warmup W (2) --seed S (42),
// --out FILE (CSV to FILE instead of stdout).
//
// bench_lmm [opts]               every column, each through
//                                harness::run_columns (own processes);
// bench_lmm --column C [opts]    only column C, the same way;
// bench_lmm --run-column C [--memory-pass] [--plain-median S] [opts]
//                                the child side of harness::run_columns.

#include "harness/bench_env.hpp"
#include "harness/columns.hpp"
#include "workloads/lmm/bench.hpp"

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
    std::cerr << "usage: bench_lmm [--column C] [--small] [--paths P] [--iters I] [--warmup W] "
                 "[--seed S] [--out FILE]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    bool small = false;
    std::size_t paths = 10000, iters = 10, warmup = 2;
    unsigned long long seed = 42;
    harness::BenchCli cli;

    const auto size_option = [&](const std::string& a, int argc, char** argv, int& i) {
        if (a == "--small") {
            small = true;
            cli.forward.push_back(a);
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
        std::cerr << "bench_lmm: " << e.what() << '\n';
        return usage();
    }
    if (paths == 0 || iters == 0) {
        std::cerr << "bench_lmm: --paths and --iters must be positive\n";
        return 2;
    }
    const std::vector<std::string> names = lmm::column_names();
    for (const std::string& c : {cli.column, cli.run_column}) {
        if (!c.empty() && std::find(names.begin(), names.end(), c) == names.end()) {
            std::cerr << "bench_lmm: unknown column '" << c << "'\n";
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

        lmm::BenchConfig cfg;
        lmm::Setup& s = cfg.setup;
        s.delta = 0.25;
        if (small) {
            s.N = 16;
            s.Nmat = 8;
            for (int len : {2, 4, 6, 8}) {
                for (double k : {0.045, 0.05, 0.055}) {
                    s.length.push_back(len);
                    s.strike.push_back(k);
                }
            }
            for (int i = 0; i < 16; ++i) cfg.inputs.L0.push_back(0.05 + 0.0005 * i);
            for (int k = 0; k < 15; ++k) cfg.inputs.lambda.push_back(0.2 - 0.002 * k);
        } else {
            s.N = 80;
            s.Nmat = 40;
            for (int len : {4, 8, 20, 28, 40}) {
                for (double k : {0.045, 0.05, 0.055}) {
                    s.length.push_back(len);
                    s.strike.push_back(k);
                }
            }
            cfg.inputs.L0.assign(80, 0.05);
            cfg.inputs.lambda.assign(79, 0.2);
        }
        s.paths = paths;
        cfg.seed = seed;
        cfg.warmup = warmup;
        cfg.iters = iters;
        cfg.plain_median_s = cli.plain_median;
        lmm::run_bench(cfg, out.stream(), cli.run_column, cli.memory_pass);
        out.finish();
    } catch (const std::exception& e) {
        std::cerr << "bench_lmm: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
