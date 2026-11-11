// A dummy extra column for the SABR bench, registered from outside the bench
// source like tests/extra_fixture/dummy_column.cpp does for Heston: a static
// harness::RegisterExtraColumn for sabr::BenchInputs. Its untimed setup gets
// the bench inputs (the market and x, the parameters every Jacobian column
// differentiates at) and returns the timed body (here: one plain residuals
// evaluation at x). It declares no static preallocation, like the real SABR
// extra columns.

#include "harness/extra_columns.hpp"
#include "workloads/sabr/bench_inputs.hpp"
#include "workloads/sabr/model.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace {

volatile double sink = 0.0;

const harness::RegisterExtraColumn<sabr::BenchInputs> dummy{
    {"dummy-extra", "dummy 0.1", [](const sabr::BenchInputs& in) -> std::function<void()> {
         const auto terms = std::make_shared<const std::vector<sabr::QuoteTerms>>(sabr::quote_terms(in.market));
         return [&in, terms] { sink = sabr::residuals(in.market, *terms, in.x)[0]; };
     }}};

}  // namespace
