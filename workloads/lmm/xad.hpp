// W1 wiring for XAD: adjoint mode (xad::adj<double>), pathwise. The inputs
// are registered once; each Monte Carlo path of lmm::path_value is recorded,
// swept back to the position after the inputs (computeAdjointsTo), and the
// tape is reset there, so the input adjoints accumulate over paths and the
// tape never holds more than one path. Gradient = accumulated adjoints /
// paths, the derivative of lmm::price.

#pragma once

#include "workloads/lmm/model.hpp"

#include <XAD/XAD.hpp>

#include <cstddef>
#include <vector>

namespace lmm {

// Gradient layout: [dV/dL0[0..N-1], dV/dlambda[0..N-2]]. The tape is owned
// and reused across calls; s and z must outlive this object.
class XadGradient {
public:
    XadGradient(const Setup& s, const std::vector<double>& z) : s_(s), z_(z) {}

    std::vector<double> operator()(const Inputs<double>& x)
    {
        tape_.clearAll();
        Inputs<AD> in;
        in.L0.assign(x.L0.begin(), x.L0.end());
        in.lambda.assign(x.lambda.begin(), x.lambda.end());
        tape_.registerInputs(in.L0);
        tape_.registerInputs(in.lambda);
        tape_.newRecording();
        const auto start = tape_.getPosition();

        const std::size_t nmat = static_cast<std::size_t>(s_.Nmat);
        PathWork<AD> w(s_.N);
        for (std::size_t p = 0; p < s_.paths; ++p) {
            w.acquire(s_.N);
            {
                AD v = path_value(s_, in, z_.data() + p * nmat, w);
                tape_.registerOutput(v);
                xad::derivative(v) = 1.0;
                tape_.computeAdjointsTo(start);
            }
            w.release();
            tape_.clearDerivativesAfter(start);
            tape_.resetTo(start);
        }

        const double inv = 1.0 / static_cast<double>(s_.paths);
        std::vector<double> g;
        g.reserve(in.L0.size() + in.lambda.size());
        for (const AD& a : in.L0) g.push_back(xad::derivative(a) * inv);
        for (const AD& a : in.lambda) g.push_back(xad::derivative(a) * inv);
        return g;
    }

private:
    using mode = xad::adj<double>;
    using AD = mode::active_type;

    const Setup& s_;
    const std::vector<double>& z_;
    mode::tape_type tape_;
};

// One-shot form of XadGradient.
inline std::vector<double> xad_gradient(const Setup& s, const Inputs<double>& x,
                                        const std::vector<double>& z)
{
    return XadGradient(s, z)(x);
}

}  // namespace lmm
