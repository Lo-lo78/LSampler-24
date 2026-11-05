#pragma once
// TEST170: small, JUCE-independent helpers shared by the audio callback and its CI test.
// No allocations, locks or system-clock input on the real-time thread.
#include <algorithm>
#include <cstdint>
#include <optional>

namespace lsampler {

// Preserve TEST59 bitwise audio output when no DAW Play was observed.
// Only after the host starts transport do Free-mode S&H oscillators use their
// own resettable generator; Triggered oscillators always keep the legacy path.
inline bool useIndependentFreeLfoRandom(bool hostPlaySeen, bool triggered) noexcept {
    return hostPlaySeen && !triggered;
}

inline uint32_t freeLfoSeed(int slot, int lfo, int note) noexcept {
    // Stable across DAW playbacks; distinct slot, LFO and MIDI note streams.
    uint32_t x = 0x9e3779b9u ^ (uint32_t(slot + 1) * 0x85ebca6bu)
        ^ (uint32_t(lfo + 1) * 0xc2b2ae35u) ^ (uint32_t(note + 2) * 0x27d4eb2du);
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x != 0 ? x : 0xa341316cu;
}

inline double nextFreeLfoHold(uint32_t& state) noexcept {
    // Same xorshift distribution as the previous Sample & Hold algorithm.
    uint32_t x = state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    state = x;
    return double(x) * (2.0 / 4294967296.0) - 1.0;
}

class HostPlayRestartDetector {
public:
    void reset() noexcept { previouslyPlaying = false; haveSample = false; lastSample = expectedNext = 0; }

    bool observe(bool playing, std::optional<int64_t> samplePosition,
                 bool looping, int blockSize) noexcept {
        bool restart = playing && !previouslyPlaying;
        // Some hosts do not invoke processBlock while stopped. In that case,
        // detect a fresh non-looping transport position without treating a
        // normal continuing block, or a DAW loop boundary, as a new Play.
        if (playing && previouslyPlaying && haveSample && samplePosition && !looping) {
            const int64_t maxForwardGap = std::max<int64_t>(4096, int64_t(std::max(1, blockSize)) * 8);
            if (*samplePosition <= lastSample || *samplePosition > expectedNext + maxForwardGap)
                restart = true;
        }
        previouslyPlaying = playing;
        haveSample = playing && samplePosition.has_value();
        if (haveSample) {
            lastSample = *samplePosition;
            expectedNext = lastSample + std::max(0, blockSize);
        }
        return restart;
    }
private:
    bool previouslyPlaying = false, haveSample = false;
    int64_t lastSample = 0, expectedNext = 0;
};
} // namespace lsampler
