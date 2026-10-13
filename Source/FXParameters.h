#pragma once
#include "Parameters.h"
#include <array>

namespace lsampler::fx {
// Stable FX IDs. Append only: REAPER envelopes rely on persistent VST3 IDs.
enum P : int {
    master = 0, chorusOn, chorusMix, chorusRate, chorusWidth,
    delayOn, delayMix, delayTime, delaySpeedL, delaySpeedR,
    delayFeedbackL, delayFeedbackR, delayToneL, delayToneR,
    delaySpread, delayDrive, delayGlide,
    reverbType, reverbMix, reverbSize, reverbDecay, reverbDamping,
    reverbWidth, reverbPredelay, reverbEarly, reverbTone,
    delayGain, reverbXOver, reverbBass, reverbGain,
    delaySync, delayLevelL, delayLevelR, delayResonanceL, delayResonanceR,
    reverbGateThreshold, reverbGateDecay,
    count
};
inline constexpr std::array<Descriptor, count> parameters {{
    {"master", "FX Master", "FX", Kind::enumeration, 0, 0, 1, 1, 1, "", 0, "Off|On"},
    {"chorus_on", "Chorus", "Chorus", Kind::enumeration, 0, 0, 1, 1, 1, "", 0, "Off|On"},
    {"chorus_mix", "Chorus Send", "Chorus", Kind::continuous, 30, 0, 100, 1, 1, "%", 0, ""},
    {"chorus_rate", "Chorus Rate", "Chorus", Kind::continuous, .3, 0, 1, .01, .01, "", 2, ""},
    {"chorus_width", "Chorus Width", "Chorus", Kind::continuous, 1, 0, 1, .01, .01, "", 2, ""},
    {"delay_on", "Dual Delay", "Dual Delay", Kind::enumeration, 0, 0, 1, 1, 1, "", 0, "Off|On"},
    {"delay_mix", "Delay Send", "Dual Delay", Kind::continuous, 30, 0, 100, 1, 1, "%", 0, ""},
    {"delay_time", "Delay Time", "Dual Delay", Kind::continuous, 250, 20, 1800, 1, 1, "ms", 0, ""},
    {"delay_speed_l", "Delay Speed L", "Dual Delay", Kind::continuous, 1, .0625, 4, .0078125, .0078125, "x", 3, ""},
    {"delay_speed_r", "Delay Speed R", "Dual Delay", Kind::continuous, 1.5, .0625, 4, .0078125, .0078125, "x", 3, ""},
    {"delay_feedback_l", "Delay Feedback L", "Dual Delay", Kind::continuous, 1, 0, 2, .01, .01, "", 2, ""},
    {"delay_feedback_r", "Delay Feedback R", "Dual Delay", Kind::continuous, 1, 0, 2, .01, .01, "", 2, ""},
    {"delay_tone_l", "Delay Tone L", "Dual Delay", Kind::continuous, .5, 0, 2, .01, .01, "", 2, ""},
    {"delay_tone_r", "Delay Tone R", "Dual Delay", Kind::continuous, .5, 0, 2, .01, .01, "", 2, ""},
    {"delay_spread", "Delay Stereo Spread", "Dual Delay", Kind::continuous, .75, -1, 1, .01, .01, "", 2, ""},
    {"delay_drive", "Delay Tape Drive", "Dual Delay", Kind::continuous, 20, 0, 100, 1, 1, "%", 0, ""},
    {"delay_glide", "Delay Tape Glide", "Dual Delay", Kind::continuous, 180, 1, 2000, 1, 1, "ms", 0, ""},
    {"reverb_type", "Reverb Type", "Reverb", Kind::enumeration, 0, 0, 4, 1, 1, "", 0, "Off|Studio Room|Concert Hall|Plate|Cathedral"},
    {"reverb_mix", "Reverb Send", "Reverb", Kind::continuous, 24, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_size", "Reverb Size", "Reverb", Kind::continuous, 80, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_decay", "Reverb Decay", "Reverb", Kind::continuous, 3000, 0, 20000, 1, 1, "ms", 0, ""},
    {"reverb_damping", "Reverb Damping", "Reverb", Kind::continuous, 10, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_width", "Reverb Width", "Reverb", Kind::continuous, 100, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_predelay", "Reverb Predelay", "Reverb", Kind::continuous, 35, 0, 180, 1, 1, "ms", 0, ""},
    {"reverb_early", "Early Reflections", "Reverb", Kind::continuous, 28, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_tone", "Reverb Tail Tone", "Reverb", Kind::continuous, 55, 0, 100, 1, 1, "%", 0, ""},
    {"delay_gain", "Delay Gain", "Dual Delay", Kind::continuous, 0, -24, 24, 1, 1, "dB", 0, ""},
    {"reverb_xover", "Reverb XOver / Open Sky", "Reverb", Kind::continuous, 360, 50, 1000, 1, 1, "Hz", 0, ""},
    {"reverb_bass", "Reverb Bass / Warmth", "Reverb", Kind::continuous, 1.3, .5, 2, .1, .1, "x", 1, ""},
    {"reverb_gain", "Reverb Gain", "Reverb", Kind::continuous, 0, -24, 24, 1, 1, "dB", 0, ""},
    {"delay_sync", "Delay Sync", "Dual Delay", Kind::enumeration, 0, 0, 7, 1, 1, "", 0, "Off|1/32|1/24|1/16|1/12|1/8|1/6|1/4"},
    {"delay_level_l", "Delay Level L", "Dual Delay", Kind::continuous, 0, -96, 0, 1, 1, "dB", 0, ""},
    {"delay_level_r", "Delay Level R", "Dual Delay", Kind::continuous, 0, -96, 0, 1, 1, "dB", 0, ""},
    {"delay_resonance_l", "Delay Resonance L", "Dual Delay", Kind::continuous, 0, 0, 100, 1, 1, "%", 0, ""},
    {"delay_resonance_r", "Delay Resonance R", "Dual Delay", Kind::continuous, 0, 0, 100, 1, 1, "%", 0, ""},
    {"reverb_gate_threshold", "Reverb Gate Threshold", "Reverb", Kind::continuous, -80, -80, 0, 1, 1, "dB", 0, ""},
    {"reverb_gate_decay", "Reverb Gate Decay", "Reverb", Kind::continuous, 300, 0, 5000, 1, 1, "ms", 0, ""}
}};
}
