#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include <array>
#include <memory>

struct SlotPlaybackState
{
    std::shared_ptr<SharedSample> sample;
    int lowKey = 0;
    int highKey = 127;
    int originalNote = 60;
    float gain = 1.0f;
    bool mono = false;
    bool monoLegato = false;
};

class VoiceBank
{
public:
    static constexpr int voiceCount = 96;
    static constexpr int slotCount = 24;

    void prepare(double sampleRate);
    void setSlots(const std::array<SlotPlaybackState, slotCount>& newSlots);
    void render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi);
    void allNotesOff();

private:
    struct Voice
    {
        bool active = false;
        int slot = -1;
        int note = -1;
        double position = 0.0;
        double increment = 1.0;
        float velocity = 1.0f;
        uint64_t age = 0;
        std::shared_ptr<SharedSample> sample;
    };

    Voice& chooseVoice();
    Voice* findMonoVoice(int slotIndex);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOffUnlocked();

    std::array<Voice, voiceCount> voices {};
    std::array<SlotPlaybackState, slotCount> slots {};
    double hostSampleRate = 44100.0;
    uint64_t ageCounter = 0;
    juce::SpinLock stateLock;
};
