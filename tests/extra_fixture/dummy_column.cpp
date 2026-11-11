// A dummy extra column, registered from outside the bench source the way an
// ADB_EXTRA_DIR registers its columns: a static harness::RegisterExtraColumn
// for the bench's input type. Its untimed setup gets the bench inputs (the
// point x every gradient column differentiates at, the book, the draws and
// dt) and returns the timed body (here: one plain price at x). It declares
// a static preallocation of 64 MiB (fake: nothing is allocated), which the
// memory pass adds to its peak_heap_reserved_bytes.

#include "harness/extra_columns.hpp"
#include "workloads/heston/bench_inputs.hpp"
#include "workloads/heston/model.hpp"

#include <cstdint>
#include <functional>

namespace {

volatile double sink = 0.0;

const harness::RegisterExtraColumn<heston::BenchInputs> dummy{
    {"dummy-extra", "dummy 0.1", [](const heston::BenchInputs& in) -> std::function<void()> {
         const heston::Params<double> p{in.x.at(0), in.x.at(1), in.x.at(2), in.x.at(3),
                                        in.x.at(4), in.x.at(5), in.x.at(6)};
         return [&in, p] { sink = heston::price(p, in.book, in.draws, in.dt); };
     },
     [] { return std::uint64_t{64} << 20; }}};

}  // namespace
