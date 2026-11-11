// XAD wiring of the Heston book: adjoint mode with the pathwise Monte Carlo
// pattern. The shared part (inputs and path_setup) is recorded once; then per
// path the payoff is recorded, its adjoint swept back to the shared position
// (computeAdjointsTo), and the tape reset to that position, so the tape never
// holds more than one path. A final sweep goes through the shared part.

#pragma once

#include "workloads/heston/model.hpp"

#include <XAD/XAD.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace heston {

// Gradient d price / d x for x = {s0, v0, kappa, theta, xi, rho, r} (Params
// order); the tape is owned and reused across calls. book and d must outlive
// this object.
class XadGradient {
public:
    XadGradient(const std::vector<Option>& book, const Draws& d, double dt)
        : book_(book), d_(d), dt_(dt), sch_(make_schedule(book, d.steps, dt))
    {
    }

    std::vector<double> operator()(const std::vector<double>& x)
    {
        tape_.clearAll();
        Params<AD> p{x[0], x[1], x[2], x[3], x[4], x[5], x[6]};
        AD* inputs[] = {&p.s0, &p.v0, &p.kappa, &p.theta, &p.xi, &p.rho, &p.r};
        for (AD* in : inputs) tape_.registerInput(*in);
        tape_.newRecording();

        const PathSetup<AD> s = path_setup(p, book_);
        const auto shared = tape_.getPosition();
        const double w = 1.0 / static_cast<double>(d_.paths);
        for (std::size_t i = 0; i < d_.paths; ++i) {
            {
                AD y = path_payoff(p, s, book_, sch_, &d_.z[2 * i * d_.steps], dt_);
                tape_.registerOutput(y);
                derivative(y) = w;
                if (i == 0) peak_tape_bytes_ = std::max(peak_tape_bytes_, tape_.getMemory());
                tape_.computeAdjointsTo(shared);
            }
            tape_.clearDerivativesAfter(shared);
            tape_.resetTo(shared);
        }
        tape_.computeAdjoints();

        std::vector<double> g;
        g.reserve(7);
        for (AD* in : inputs) g.push_back(derivative(*in));
        return g;
    }

    // Tape size with the shared part and one path recorded.
    std::size_t peak_tape_bytes() const { return peak_tape_bytes_; }

private:
    using mode = xad::adj<double>;
    using AD = mode::active_type;

    const std::vector<Option>& book_;
    const Draws& d_;
    double dt_;
    Schedule sch_;
    mode::tape_type tape_;
    std::size_t peak_tape_bytes_ = 0;
};

// One-shot form of XadGradient.
inline std::vector<double> xad_gradient(const std::vector<double>& x, const std::vector<Option>& book,
                                        const Draws& d, double dt)
{
    return XadGradient(book, d, dt)(x);
}

}  // namespace heston
