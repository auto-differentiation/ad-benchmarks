// A dummy extra column for the LMM bench, registered from outside the bench
// source like tests/extra_fixture/dummy_column.cpp does for Heston: a static
// harness::RegisterExtraColumn for lmm::BenchInputs. Its untimed setup gets
// the bench inputs (the setup, the model inputs, x = L0 then lambda, the point
// every gradient column differentiates at, and the normals z) and returns the
// timed body (here: one plain price at x).
// It declares a static preallocation of 64 MiB (fake: nothing is
// allocated), which the memory pass adds to its peak_heap_reserved_bytes.
// A second column, dummy-leak, registered first, stands in for a library that
// keeps a global allocation after its column ran (e.g. a global tape): its
// set-up allocates and touches 96 MiB that it never frees.

#include "harness/extra_columns.hpp"
#include "workloads/lmm/bench_inputs.hpp"
#include "workloads/lmm/model.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace {

volatile double sink = 0.0;

const harness::RegisterExtraColumn<lmm::BenchInputs> leak{
    {"dummy-leak", "dummy 0.1", [](const lmm::BenchInputs&) -> std::function<void()> {
         static std::vector<char> kept(std::size_t{96} << 20);
         std::memset(kept.data(), 1, kept.size());
         return [] { sink = sink + kept[0]; };
     }}};

const harness::RegisterExtraColumn<lmm::BenchInputs> dummy{
    {"dummy-extra", "dummy 0.1", [](const lmm::BenchInputs& in) -> std::function<void()> {
         const std::size_t n = static_cast<std::size_t>(in.setup.N);
         lmm::Inputs<double> p;
         p.L0.assign(in.x.begin(), in.x.begin() + static_cast<std::ptrdiff_t>(n));
         p.lambda.assign(in.x.begin() + static_cast<std::ptrdiff_t>(n), in.x.end());
         return [&in, p] { sink = lmm::price(in.setup, p, in.z); };
     },
     [] { return std::uint64_t{64} << 20; }}};

}  // namespace
