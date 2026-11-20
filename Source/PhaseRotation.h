#pragma once
#include <algorithm>
#include <cmath>

namespace lsampler {
// TEST189: zero-latency first-order all-pass phase rotator.
// The requested angle is exact at referenceHz; the phase response necessarily
// varies with frequency because a causal all-pass cannot shift every frequency
// by the same amount without adding latency. Magnitude is always unity.
struct PhaseRotation {
    double coefficient = 1.0; // 1 = bypass, -1 = polarity inversion
    double sign = 1.0;        // -1 for positive angles, so -180..+180 is continuous
    double smoothing = 1.0;   // 10 ms coefficient/sign ramp for host automation
    bool active = false;
};

inline PhaseRotation phaseRotation(double degrees, double sampleRate,
                                   double referenceHz = 1000.0) noexcept {
    PhaseRotation r;
    if (!std::isfinite(degrees) || std::abs(degrees) < 1.0e-9
        || !std::isfinite(sampleRate) || sampleRate <= 0.0
        || !std::isfinite(referenceHz) || referenceHz <= 0.0)
        return r;

    constexpr double pi = 3.14159265358979323846;
    const bool invert = degrees > 0.0;
    // Positive angles use a negative rotation followed by polarity inversion:
    // +theta = -1 * allpass(-(180-theta)). This keeps one coefficient range.
    const double phi = invert ? degrees - 180.0 : degrees;
    const double target = std::clamp(-phi, 0.0, 179.999999);
    const double t = std::tan(target * pi / 360.0);
    const double k = std::tan(pi * referenceHz / sampleRate);
    r.coefficient = (k - t) / (k + t);
    r.sign = invert ? -1.0 : 1.0;
    r.smoothing = 1.0 - std::exp(-1.0 / (0.010 * sampleRate));
    r.active = true;
    return r;
}

// H(z) = (a + z^-1) / (1 + a z^-1); unity magnitude, phase -180..0 degrees.
inline double phaseAllPass(double x, double& previousInput, double& previousOutput,
                           double coefficient) noexcept {
    const double y = coefficient * (x - previousOutput) + previousInput;
    previousInput = x;
    previousOutput = y;
    return y;
}
} // namespace lsampler
