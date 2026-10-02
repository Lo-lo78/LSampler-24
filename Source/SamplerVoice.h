#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include <array>
#include <memory>

class GlobalVoicePool
{
public:
    static constexpr int voiceCount = 96;

    void prepare(double sampleRate);
    void noteOn(int slotIndex, int note, float velocity,
                std::shared_ptr<SharedSample> sample, int rootNote,
                float gain, bool preview = false);
    void noteOff(int note);
    void allNotesOff();
    void stopSlotVoices(int slotIndex);
    void stopPreviewVoices(int slotIndex = -1);
    bool hasPreviewVoices(int slotIndex = -1) const noexcept;
    void render(juce::AudioBuffer<float>& output);

private:
    struct Voice
    {
        bool active = false;
        bool preview = false;
        int slotIndex = -1;
        int note = -1;
        double position = 0.0;
        double increment = 1.0;
        float gain = 1.0f;
        uint64_t age = 0;
        std::shared_ptr<SharedSample> sample;
    };

    Voice& chooseVoice();

    std::array<Voice, voiceCount> voices {};
    double hostSampleRate = 44100.0;
    uint64_t ageCounter = 0;
};
