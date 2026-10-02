#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include <array>

class VoiceBank
{
public:
    static constexpr int voiceCount = 16;

    void prepare(double sampleRate);
    void setSample(std::shared_ptr<SharedSample> newSample);
    void setRootNote(int midiNote);
    void setGain(float linearGain);
    void render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi);
    void allNotesOff();
    bool hasActiveVoices() const noexcept;

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        double position = 0.0;
        double increment = 1.0;
        uint64_t age = 0;
    };

    void noteOn(int note, float velocity);
    void noteOff(int note);
    Voice& chooseVoice();

    std::array<Voice, voiceCount> voices {};
    std::shared_ptr<SharedSample> sample;
    double hostSampleRate = 44100.0;
    int rootNote = 60;
    float gain = 1.0f;
    uint64_t ageCounter = 0;
    std::array<float, voiceCount> velocityGain {};
};
