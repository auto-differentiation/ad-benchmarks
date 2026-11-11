// Shared CVA test cases (inputs and configuration, no shocks).

#pragma once

#include "workloads/cva/model.hpp"

#include <cstddef>

namespace cva_test {

struct Case {
    cva::Config config;
    cva::Inputs<double> inputs;
};

// A small mixed portfolio for the CVA tests: payer and receiver swaps with
// different notionals, starts, maturities and fixed rates; annual exposure
// dates on the curve pillars, hazard pillars on exposure dates.
inline Case portfolio_setup(std::size_t n_paths)
{
    Case s;
    s.config.mean_reversions = {0.05, 0.3, 1.0};
    s.config.dates = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    s.config.n_paths = n_paths;
    s.config.curve_times = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 10.0};
    s.config.hazard_times = {1.0, 3.0, 6.0};
    const auto swap = [](double notional, double start, double maturity, double period, bool payer) {
        cva::Swap w;
        w.notional = notional;
        w.start = start;
        w.maturity = maturity;
        w.period = period;
        w.payer = payer;
        return w;
    };
    s.config.swaps = {swap(100.0, 0.0, 5.0, 0.5, true), swap(60.0, 1.0, 7.0, 1.0, false),
                      swap(150.0, 0.0, 3.0, 0.25, true), swap(80.0, 2.0, 10.0, 1.0, true)};
    s.inputs.zero_rates = {0.020, 0.022, 0.025, 0.027, 0.029, 0.030, 0.031, 0.033};
    s.inputs.vols = {0.010, 0.008, 0.006};
    s.inputs.correlations = {-0.6, 0.3, -0.2};
    s.inputs.hazard_rates = {0.010, 0.015, 0.020};
    s.inputs.lgd = 0.6;
    s.inputs.fixed_rates = {0.026, 0.028, 0.023, 0.031};
    return s;
}

}  // namespace cva_test
