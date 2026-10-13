#pragma once
#include "FXParameters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// TEST145: Eight-line stereo feedback delay network (FDN).
// An orthonormal Hadamard feedback matrix permits a calibrated RT60 of
// 0..20000 ms. All storage is allocated by prepare(), never in process().
// Unlike JUCE Freeverb, RT60, room size and high-frequency damping are
// independent controls. Early reflections are implemented in FXEngine.
class LSamplerFXReverb final {
public:
    void prepare(double rate) {
        sampleRate = std::max(8000.0, rate);
        const auto capacity = size_t(std::ceil(sampleRate * .37)) + 8;
        for (auto& line : lines) line.assign(capacity, 0.0f);
        reset();
    }

    void reset() noexcept {
        for (auto& line : lines) std::fill(line.begin(), line.end(), 0.0f);
        writePos = 0;
        damped.fill(0.0f);
        configured.fill(-1.0f);
        readLength.fill(0.0f);
        targetLength.fill(0.0f);
        feedback.fill(0.0f);
    }

    void configure(const std::array<float, lsampler::fx::count>& p, int mode) noexcept {
        using namespace lsampler::fx;
        const std::array<float, 6> signature {float(mode), p[reverbSize], p[reverbDecay], p[reverbDamping], p[reverbXOver], p[reverbBass]};
        if (signature == configured) return;
        if (configured[0] >= 0.0f && configured[0] != float(mode)) reset();
        configured = signature;

        const float size = std::clamp(p[reverbSize] * .01f, 0.0f, 1.0f);
        // Size changes the physical modal spacing by nearly a factor of 5;
        // each mode additionally has its own characteristic scale.
        const float modeSize = mode == 1 ? .65f : mode == 2 ? 1.05f
                             : mode == 3 ? .75f : 1.38f;
        constexpr std::array<float, 8> baseMs {27.7f, 31.3f, 36.7f, 42.1f,
                                                47.9f, 53.9f, 61.1f, 68.3f};
        const double rt60 = std::max(0.0, double(p[reverbDecay]) * .001);
        for (int i=0; i<8; ++i) {
            targetLength[size_t(i)] = std::clamp(
                float(baseMs[size_t(i)] * modeSize * (.45f + 1.80f * size)
                      * sampleRate * .001), 2.0f, float(lines[size_t(i)].size()-2));
            if (readLength[size_t(i)] <= 0.0f)
                readLength[size_t(i)] = targetLength[size_t(i)];
            // An individual round trip lasts delayLength/sampleRate seconds.
            // -60 dB = amplitude * 0.001 after RT60 seconds.
            feedback[size_t(i)] = rt60 < .001 ? 0.0f : float(std::exp(
                -6.907755278982137 * double(targetLength[size_t(i)]) / (sampleRate * rt60)));
        }
        const float damper = std::clamp(p[reverbDamping] * .01f, 0.0f, 1.0f);
        // 0% = open / bright; 100% = deliberately very dark.
        // LJuno-inspired warmth: lower damping cutoff with higher bass multiplier.
        const double warmth = std::clamp(double(p[reverbBass]), .5, 2.0);
        const double hz = 17000.0 * std::pow(240.0 / 17000.0, double(damper))
                          * std::pow(1.3 / warmth, 0.65);
        // XOver / Open Sky controls the amount of high-frequency diffusion
        // retained in the late feedback network.
        const double openSky = std::clamp((double(p[reverbXOver]) - 50.0) / 950.0, 0.0, 1.0);
        diffusion = float(.56 + .44 * openSky);
        dampAlpha = float(1.0 - std::exp(-6.283185307179586 * hz / sampleRate));
        lengthGlide = float(1.0 - std::exp(-1.0 / (.060 * sampleRate)));
    }

    void process(float inL, float inR, float& wetL, float& wetR) noexcept {
        // The input injection and Hadamard transform are energy-normalised;
        // no uncontrolled resonant feedback at the 20-second setting.
        constexpr float norm = .3535533905932738f; // 1 / sqrt(8)
        std::array<float, 8> taps {};
        for (int i=0; i<8; ++i) {
            const size_t k = size_t(i);
            auto& line = lines[k];
            readLength[k] += (targetLength[k] - readLength[k]) * lengthGlide;
            float pos = float(writePos) - readLength[k];
            if (pos < 0.0f) pos += float(line.size());
            const int index = int(pos);
            const float f = pos - float(index);
            const float read = line[size_t(index)]
                + f * (line[size_t((index+1) % int(line.size()))] - line[size_t(index)]);
            damped[k] += dampAlpha * (read - damped[k]);
            taps[k] = damped[k] * diffusion + read * (1.0f - diffusion);
        }
        wetL = norm * (taps[0] + taps[1] - taps[2] - taps[3]
                     + taps[4] + taps[5] - taps[6] - taps[7]);
        wetR = norm * (taps[0] - taps[1] + taps[2] - taps[3]
                     - taps[4] + taps[5] - taps[6] + taps[7]);

        std::array<float, 8> mixed = taps;
        for (int stride = 1; stride < 8; stride *= 2)
            for (int block = 0; block < 8; block += 2*stride)
                for (int j=0; j<stride; ++j) {
                    const float a = mixed[size_t(block+j)];
                    const float b = mixed[size_t(block+j+stride)];
                    mixed[size_t(block+j)] = a+b;
                    mixed[size_t(block+j+stride)] = a-b;
                }
        for (int i=0; i<8; ++i) {
            const float injected = i & 1 ? inR : inL;
            const float next = .24f * injected + norm * feedback[size_t(i)] * mixed[size_t(i)];
            lines[size_t(i)][size_t(writePos)] = std::clamp(next, -8.0f, 8.0f);
        }
        if (++writePos >= int(lines[0].size())) writePos = 0;
    }
private:
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 8> lines;
    std::array<float, 8> damped {}, readLength {}, targetLength {}, feedback {};
    std::array<float, 6> configured {-1,-1,-1,-1,-1,-1};
    float dampAlpha = 1.0f, lengthGlide = .001f, diffusion = 1.0f;
    int writePos = 0;
};
