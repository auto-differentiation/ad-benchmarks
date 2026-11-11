// W3 CVA gradient with XAD adjoint mode, pathwise.

#pragma once

#include "workloads/cva/model.hpp"

#include <XAD/XAD.hpp>

#include <cstddef>
#include <vector>

namespace cva {

// d CVA / d inputs in the flat layout of to_vector, with the usual pathwise
// tape pattern: register the inputs and record the values shared by all
// paths (make_shared) once; then for each path record path_cva after that
// position, propagate its adjoint back to the position, and reset the tape
// there, so the shared adjoints accumulate over paths and the tape holds one
// path at a time. A final sweep takes the shared adjoints to the inputs.
// *value (if given) receives the CVA computed on the tape. The tape is owned
// and reused across calls; c, pl and z must outlive this object.
class XadGradient {
public:
    XadGradient(const Config& c, const Plan& pl, const std::vector<double>& z) : c_(c), pl_(pl), z_(z) {}

    std::vector<double> operator()(const Inputs<double>& in, double* value = nullptr)
    {
        tape_.clearAll();
        const std::vector<double> flat = to_vector(in);
        std::vector<AD> x(flat.begin(), flat.end());
        tape_.registerInputs(x);
        tape_.newRecording();

        const Shared<AD> sh = make_shared(from_vector(x, c_), c_, pl_);
        const auto pos = tape_.getPosition();
        const std::size_t KF = pl_.K * pl_.F;
        double v = 0.0;
        PathWork<AD> w(pl_);
        for (std::size_t p = 0; p < c_.n_paths; ++p) {
            w.acquire(pl_);
            {
                AD y = path_cva(sh, pl_, c_, &z_[p * KF], w);
                tape_.registerOutput(y);
                derivative(y) = 1.0;
                tape_.computeAdjointsTo(pos);
                v += xad::value(y);
            }
            w.release();
            tape_.clearDerivativesAfter(pos);
            tape_.resetTo(pos);
        }
        tape_.computeAdjoints();

        if (value) *value = v;
        std::vector<double> g(x.size());
        for (std::size_t i = 0; i < x.size(); ++i) g[i] = derivative(x[i]);
        return g;
    }

private:
    using mode = xad::adj<double>;
    using AD = mode::active_type;

    const Config& c_;
    const Plan& pl_;
    const std::vector<double>& z_;
    mode::tape_type tape_;
};

// One-shot form of XadGradient.
inline std::vector<double> gradient_xad(const Inputs<double>& in, const Config& c, const Plan& pl,
                                        const std::vector<double>& z, double* value = nullptr)
{
    return XadGradient(c, pl, z)(in, value);
}

inline std::vector<double> gradient_xad(const Inputs<double>& in, const Config& c,
                                        const std::vector<double>& z, double* value = nullptr)
{
    return gradient_xad(in, c, make_plan(c), z, value);
}

}  // namespace cva
