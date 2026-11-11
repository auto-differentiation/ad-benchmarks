// A dummy extra column for the CVA bench, registered from outside the bench
// source like tests/extra_fixture/dummy_column.cpp does for Heston: a static
// harness::RegisterExtraColumn for cva::BenchInputs. Its untimed setup gets
// the bench inputs (the configuration, the model inputs, x = to_vector of
// them, the point every gradient column differentiates at, and the shocks z)
// and returns the timed body (here: one plain CVA at x).
// It declares a static preallocation of 64 MiB (fake: nothing is
// allocated), which the memory pass adds to its peak_heap_reserved_bytes.
// A second column, dummy-noop, allocates nothing in its set-up or body, so its
// memory pass shows only what the bench itself adds for an extra column.

#include "harness/extra_columns.hpp"
#include "workloads/cva/bench_inputs.hpp"
#include "workloads/cva/model.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace {

volatile double sink = 0.0;

const harness::RegisterExtraColumn<cva::BenchInputs> dummy{
    {"dummy-extra", "dummy 0.1", [](const cva::BenchInputs& in) -> std::function<void()> {
         const cva::Inputs<double> p = cva::from_vector(in.x, in.config);
         const auto plan = std::make_shared<const cva::Plan>(cva::make_plan(in.config));
         return [&in, p, plan] { sink = cva::cva(p, in.config, *plan, in.z); };
     },
     [] { return std::uint64_t{64} << 20; }}};

const harness::RegisterExtraColumn<cva::BenchInputs> noop{
    {"dummy-noop", "dummy 0.1", [](const cva::BenchInputs&) -> std::function<void()> { return [] {}; }}};

}  // namespace
