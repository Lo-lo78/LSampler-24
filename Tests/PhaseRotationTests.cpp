// TEST189: zero-latency first-order all-pass phase rotation.
#include "../Source/PhaseRotation.h"
#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>

namespace {
constexpr double pi = 3.14159265358979323846;

std::complex<double> response(const lsampler::PhaseRotation& r, double hz, double sr) {
    const double w = 2.0 * pi * hz / sr;
    const std::complex<double> z = std::polar(1.0, -w);
    return std::complex<double>(r.sign, 0.0) * (r.coefficient + z)
        / (1.0 + r.coefficient * z);
}

double degrees(double radians) { return radians * 180.0 / pi; }
}

int main() {
    using namespace lsampler;
    constexpr double sr = 48000.0;
    constexpr double reference = 1000.0;

    const auto bypass = phaseRotation(0.0, sr);
    assert(!bypass.active);
    assert(bypass.coefficient == 1.0 && bypass.sign == 1.0);

    for (const double requested : {-170.0, -90.0, -45.0, -30.0, 30.0, 45.0, 90.0, 170.0}) {
        const auto r = phaseRotation(requested, sr);
        assert(r.active);
        const auto h = response(r, reference, sr);
        assert(std::abs(std::abs(h) - 1.0) < 1.0e-12);
        const double actual = degrees(std::arg(h));
        assert(std::abs(actual - requested) < 1.0e-6);
        // Magnitude remains unity away from the reference frequency too.
        for (const double hz : {50.0, 500.0, 5000.0, 15000.0})
            assert(std::abs(std::abs(response(r, hz, sr)) - 1.0) < 1.0e-12);
    }

    const auto positive180 = phaseRotation(180.0, sr);
    assert(positive180.active && positive180.sign == -1.0);
    assert(std::abs(positive180.coefficient - 1.0) < 1.0e-12);
    assert(std::abs(degrees(std::arg(response(positive180, reference, sr))) - 180.0) < 1.0e-9);

    const auto negative180 = phaseRotation(-180.0, sr);
    assert(negative180.active && negative180.sign == 1.0);
    assert(std::abs(negative180.coefficient + 1.0) < 1.0e-6);
    assert(std::abs(degrees(std::arg(response(negative180, reference, sr))) + 180.0) < 1.0e-5);

    // Difference equation: a = 1 is bypass, a = -1 is polarity inversion.
    double x1 = 0.0, y1 = 0.0;
    assert(std::abs(phaseAllPass(0.5, x1, y1, 1.0) - 0.5) < 1.0e-15);
    assert(std::abs(phaseAllPass(-0.25, x1, y1, 1.0) + 0.25) < 1.0e-15);
    x1 = y1 = 0.0;
    assert(std::abs(phaseAllPass(0.5, x1, y1, -1.0) + 0.5) < 1.0e-15);
    assert(std::abs(phaseAllPass(-0.25, x1, y1, -1.0) - 0.25) < 1.0e-15);

    std::cout << "PASS TEST189: Phase -180..+180, unity magnitude, 1 kHz reference, exact polarity"
              << std::endl;
}
