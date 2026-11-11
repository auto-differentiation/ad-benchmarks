// Adept-2 wiring of the Heston book: reverse mode on an adept::Stack, path by
// path. Adept has no partial reverse sweep to a tape position, so each path is
// its own recording (new_recording, the shared path_setup recomputed on it,
// the path payoff, reverse) and the input adjoints are accumulated over paths;
// the stack never holds more than one path.

#pragma once

#include "workloads/heston/model.hpp"

#include <adept.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace heston {

// Gradient d price / d x for x = {s0, v0, kappa, theta, xi, rho, r} (Params
// order). Owns the Stack, which stays alive across calls; with
// ADEPT_STACK_THREAD_UNSAFE Adept allows one active Stack per process, so only
// one object may exist at a time. book and d must outlive this object.
class AdeptGradient {
public:
    AdeptGradient(const std::vector<Option>& book, const Draws& d, double dt)
        : book_(book), d_(d), dt_(dt), sch_(make_schedule(book, d.steps, dt))
    {
    }

    std::vector<double> operator()(const std::vector<double>& x)
    {
        AD* inputs[] = {&p_.s0, &p_.v0, &p_.kappa, &p_.theta, &p_.xi, &p_.rho, &p_.r};
        for (std::size_t j = 0; j < 7; ++j) inputs[j]->set_value(x[j]);
        std::vector<double> g(7, 0.0);
        const double w = 1.0 / static_cast<double>(d_.paths);
        for (std::size_t i = 0; i < d_.paths; ++i) {
            stack_.new_recording();
            path_setup(p_, book_, s_);
            AD y = path_payoff(p_, s_, book_, sch_, &d_.z[2 * i * d_.steps], dt_);
            y.set_gradient(w);
            if (i == 0) peak_tape_bytes_ = std::max(peak_tape_bytes_, stack_.memory());
            stack_.reverse();
            for (std::size_t j = 0; j < 7; ++j) g[j] += inputs[j]->get_gradient();
        }
        return g;
    }

    // Stack size with one path recorded.
    std::size_t peak_tape_bytes() const { return peak_tape_bytes_; }

private:
    using AD = adept::adouble;

    const std::vector<Option>& book_;
    const Draws& d_;
    double dt_;
    Schedule sch_;
    adept::Stack stack_;  // before p_: the inputs live on this stack
    Params<AD> p_{};
    PathSetup<AD> s_{};  // recomputed per path, storage reused
    std::size_t peak_tape_bytes_ = 0;
};

// One-shot form of AdeptGradient.
inline std::vector<double> adept_gradient(const std::vector<double>& x, const std::vector<Option>& book,
                                          const Draws& d, double dt)
{
    return AdeptGradient(book, d, dt)(x);
}

}  // namespace heston
