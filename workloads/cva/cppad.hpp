// W3 CVA gradient with CppAD: shared part and one path recorded once.

#pragma once

#include "workloads/cppad_hold_memory.hpp"
#include "workloads/cva/model.hpp"

#include <cppad/cppad.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cva {

namespace detail {

// The recorded tape must stay right when a path's portfolio value changes
// sign, so the positive part is a conditional expression, not a branch.
template <>
inline CppAD::AD<double> positive_part<CppAD::AD<double>>(const CppAD::AD<double>& v)
{
    const CppAD::AD<double> zero(0.0);
    return CppAD::CondExpGt(v, zero, v, zero);
}

}  // namespace detail

// The kernel recorded once as two optimized tapes, with the same split as
// model.hpp: f_shared (flat inputs -> shared values, shared_to_vector) and
// f_path (shared values -> path_cva, with one path's shocks as dynamic
// parameters). gradient() runs f_shared forward once, then per path sets the
// shocks (new_dynamic), runs f_path Forward(0) and Reverse(1) and accumulates
// d CVA / d shared, and finally one Reverse(1) of f_shared with those
// weights. It never re-records. Inputs and shocks must keep the sizes of the
// recording.
class CppadCva {
public:
    CppadCva(const Inputs<double>& in, const Config& c, const std::vector<double>& z)
        : config_(c), plan_(make_plan(c)), nz_(plan_.K * plan_.F)
    {
        using AD = CppAD::AD<double>;
        const std::vector<double> flat = to_vector(in);
        std::vector<AD> x(flat.begin(), flat.end());
        CppAD::Independent(x);
        std::vector<AD> s = shared_to_vector(make_shared(from_vector(x, config_), config_, plan_), plan_);
        f_shared_.Dependent(x, s);
        f_shared_.optimize();

        const std::vector<double> s0 = shared_to_vector(make_shared(in, config_, plan_), plan_);
        std::vector<AD> as(s0.begin(), s0.end());
        std::vector<AD> shocks(z.begin(), z.begin() + static_cast<std::ptrdiff_t>(nz_));
        CppAD::Independent(as, shocks);
        std::vector<AD> y{path_cva(shared_from_vector(as, plan_, config_), plan_, config_, shocks.data())};
        f_path_.Dependent(as, y);
        f_path_.optimize();
    }

    std::vector<double> gradient(const Inputs<double>& in, const std::vector<double>& z,
                                 double* value = nullptr)
    {
        const std::vector<double> s = f_shared_.Forward(0, to_vector(in));
        std::vector<double> acc(s.size(), 0.0);
        std::vector<double> zp(nz_);
        const std::vector<double> w{1.0};
        double v = 0.0;
        for (std::size_t p = 0; p < config_.n_paths; ++p) {
            std::copy(z.begin() + static_cast<std::ptrdiff_t>(p * nz_),
                      z.begin() + static_cast<std::ptrdiff_t>((p + 1) * nz_), zp.begin());
            f_path_.new_dynamic(zp);
            v += f_path_.Forward(0, s)[0];
            const std::vector<double> d = f_path_.Reverse(1, w);
            for (std::size_t i = 0; i < acc.size(); ++i) acc[i] += d[i];
        }
        if (value) *value = v;
        return f_shared_.Reverse(1, acc);
    }

private:
    Config config_;
    Plan plan_;
    std::size_t nz_;
    CppAD::ADFun<double> f_shared_;
    CppAD::ADFun<double> f_path_;
};

}  // namespace cva
