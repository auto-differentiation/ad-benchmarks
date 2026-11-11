// Build check for the pinned AD libraries: includes each library's main
// header, differentiates f(x) = x * x at x = 3 and prints its version.
// Not a test; built only to show that every dependency compiles and links.

#include "versions.hpp"

#include <cstdio>

#ifdef ADB_WITH_XAD
#include <XAD/XAD.hpp>
#endif
#if defined(ADB_WITH_CPPAD)
#include <cppad/cppad.hpp>
#endif
#ifdef ADB_WITH_ADEPT
#include <adept.h>
#endif
#ifdef ADB_WITH_AUTODIFF
#include <autodiff/reverse/var.hpp>
#endif

#include <vector>

int main()
{
    std::printf("arch flags: %s\n", adb::versions::arch_flags);
    const double x0 = 3.0;
#ifdef ADB_WITH_XAD
    {
        using mode = xad::adj<double>;
        mode::tape_type tape;
        mode::active_type x = x0;
        tape.registerInput(x);
        tape.newRecording();
        mode::active_type y = x * x;
        tape.registerOutput(y);
        derivative(y) = 1.0;
        tape.computeAdjoints();
        std::printf("XAD       %-22s dy/dx = %g\n", adb::versions::xad, derivative(x));
    }
#endif
#ifdef ADB_WITH_CPPAD
    {
        std::vector<CppAD::AD<double>> x(1, x0), y(1);
        CppAD::Independent(x);
        y[0] = x[0] * x[0];
        CppAD::ADFun<double> f(x, y);
        const std::vector<double> g = f.Jacobian(std::vector<double>(1, x0));
        std::printf("CppAD     %-22s dy/dx = %g\n", adb::versions::cppad, g[0]);
    }
#endif
#ifdef ADB_WITH_ADEPT
    {
        adept::Stack stack;
        adept::adouble x = x0;
        stack.new_recording();
        adept::adouble y = x * x;
        y.set_gradient(1.0);
        stack.compute_adjoint();
        std::printf("Adept     %-22s dy/dx = %g\n", adb::versions::adept, x.get_gradient());
    }
#endif
#ifdef ADB_WITH_AUTODIFF
    {
        autodiff::var x = x0;
        autodiff::var y = x * x;
        const auto [dydx] = autodiff::derivatives(y, autodiff::wrt(x));
        std::printf("autodiff  %-22s dy/dx = %g (Eigen %s)\n", adb::versions::autodiff,
                    static_cast<double>(dydx), adb::versions::eigen);
    }
#endif
    return 0;
}
