// W3: every column's peak memory is measured with the harness heap counter.
//
// Contract checked here (namespace cva, workloads/cva/columns.hpp):
//   const std::vector<std::string>& column_names();
//     - {"plain", "xad", "cppad", "adept", "autodiff", "fd"}
//   struct ColumnRun { double value; std::vector<double> gradient; };
//   ColumnRun run_column(const std::string& name, const Inputs<double>&, const Config&,
//                        const std::vector<double>& z);
//     - one run of that column from scratch: recording (CppAD) included;
//       "plain" returns an empty gradient.
//   std::uint64_t peak_memory_bytes(const std::string& name, const Inputs<double>&,
//                                   const Config&, const std::vector<double>& z);
//     - harness::peak_heap_bytes of run_column(name, ...); needs the
//       adb_heap_peak preload (untimed pass only).
//
// The taping columns record pathwise (shared values once, then one path at a
// time), so their peak does not grow with the number of paths, and it is
// dominated by the libraries' own chunk sizes; this test therefore checks
// that every column reports a real figure, not a growth law.

#include "tests/cva/portfolio.hpp"
#include "workloads/cva/columns.hpp"
#include "workloads/cva/model.hpp"
#include "workloads/cva/xad.hpp"

#include "harness/heap_peak.hpp"
#include "harness/rng.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

TEST(CvaMemory, EveryColumnReportsPeakMemory)
{
    ASSERT_TRUE(harness::heap_peak_available()) << "run with LD_PRELOAD of adb_heap_peak";
    const cva_test::Case s = cva_test::portfolio_setup(50);
    harness::Rng rng(29);
    const std::vector<double> z = rng.normals(cva::n_normals(s.config));

    const std::vector<std::string> expected = {"plain", "xad", "cppad", "adept", "autodiff", "fd"};
    ASSERT_EQ(cva::column_names(), expected);

    double v_xad = 0.0;
    const std::vector<double> g_xad = cva::gradient_xad(s.inputs, s.config, z, &v_xad);
    std::uint64_t plain = 0;
    for (const std::string& name : expected) {
        const cva::ColumnRun run = cva::run_column(name, s.inputs, s.config, z);
        EXPECT_NEAR(run.value, v_xad, 1e-12 * v_xad) << name;
        EXPECT_EQ(run.gradient.size(), name == "plain" ? 0u : g_xad.size()) << name;

        const std::uint64_t peak = cva::peak_memory_bytes(name, s.inputs, s.config, z);
        EXPECT_GT(peak, 0u) << name;
        if (name == "plain") plain = peak;
        else if (name != "fd") EXPECT_GT(peak, plain) << name << " records more than plain allocates";
    }
}
