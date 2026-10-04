#pragma once
#include "Parameters.h"
#include "SamplePool.h"
#include "SliceState.h"

namespace lsampler {
struct EnvelopeSettings {
    double attackStep = 1, decay = 1, sustain = 1, release = 0;
    bool filter = false;
};
struct LfoSettings {
    bool active = false, sync = false, trigger = false, oneShot = false;
    int wave = 0;
    double rate = 0, delay = 0, smoothing = 1;
    double volume = 0, pan = 0, lp = 0, hp = 0, pitch = 0, move = 0, wheel = 0;
};
struct FilterCoefficients {
    double a1 = 1, a2 = 0, a3 = 0, f = 0, q = 1;
};
struct LoopAudioState {
    int memory = 0;
    double start = 0, end = 0, fadeIn = 0, fadeOut = 0;
    int repeats = 0;
    double pitch = 0, lp = 0, hp = 0;
    bool oneShot = false;
};
struct SlotAudioState {
    SlotParameters params;
    SliceAudioState slice;
    SharedSample* sample = nullptr; // ownership lives exclusively in snapshot owners/retirement list
    uint64_t revision = 0;
    double sampleRate = 44100, sourceRatio = 1;
    int start = 0, length = 0, downsampleHold = 1;
    double downsampleScale = 1, level = 1, panL = 1, panR = 1, normalize = 1;
    double fadeIn = 0, fadeOut = 0, edgeFade = 0, delayL = 0, delayR = 0;
    double stretchFactor = 1, grain = 16, portamento = 1, smooth = 1;
    EnvelopeSettings amp, lpEnv, hpEnv;
    std::array<LfoSettings, 2> lfo;
    FilterCoefficients lpStatic, hpStatic;
    double lpHz = 10000, hpHz = 20;
    bool lp = false, hp = false, drive = false, comp = false, gate = false;
    bool transient = false, degrade = false, ring = false, fm = false, machine = false;
    double compAttack = 0, compRelease = 0, compThreshold = 1, compExponent = 0, compMakeup = 1, compMix = 0;
    double gateAttack = 0, gateRelease = 0, gateThreshold = 0, gateClosed = 0, gateHold = 0, gateMix = 0;
    double transFast = 0, transSlow = 0, transAmount = 0, transMix = 0;
    double degradeAmount = 0, degradeScale = 1;
    int degradeHold = 1, degradeJitter = 0;
    std::array<double, 6> character {}; // drive, converter, life, repeat, air, glue
    double characterScale = 1, characterSlew = 1, characterAir = 0, characterRelease = 0;
    std::array<LoopAudioState, loopCount> stages {};
    int stageCount = 0;
    double crossfade = 0;
};
FilterCoefficients filterCoefficients(bool highPass, double hz, double resonance, double sampleRate) noexcept;
struct ThresholdWindow { int start = 0, end = 0; };
ThresholdWindow calculateThresholdWindow(const SlotParameters&, SharedSample*) noexcept;
SlotAudioState prepareSlotAudioState(const SlotParameters&, SharedSample*, double sampleRate, uint64_t revision,
                                    int effectiveStart = -1, int effectiveEnd = -1);
} // namespace lsampler
