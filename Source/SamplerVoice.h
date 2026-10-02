#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include <array>
#include <memory>

struct SlotRuntimeState
{
    std::shared_ptr<SharedSample> sample;
    int lowKey = 0;
    int highKey = 127;
    int originalPitch = 60;
    float volume = 1.0f;
    bool mono = false;
    bool legato = false;
};

struct SamplerRuntimeState
{
    static constexpr int slotCount = 24;
    std::array<SlotRuntimeState, slotCount> slots {};
};

class VoiceBank
{
public:
    static constexpr int voiceCount = 96;

    void prepare(double sampleRate);
    void render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi,
                const SamplerRuntimeState& runtime);
    void allNotesOff();

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        int slot = -1;
        double position = 0.0;
        double increment = 1.0;
        float gain = 1.0f;
        uint64_t age = 0;
        std::shared_ptr<SharedSample> sample;
    };

    Voice& chooseVoice();
    Voice* findMonoVoice(int slot);
    void startVoice(Voice& voice, int slot, int note, float velocity,
                    const SlotRuntimeState& state, bool restart);
    void noteOn(int note, float velocity, const SamplerRuntimeState& runtime);
    void noteOff(int note, const SamplerRuntimeState& runtime);
    int newestHeldNoteForSlot(int slot, const SlotRuntimeState& state) const;

    std::array<Voice, voiceCount> voices {};
    std::array<std::array<uint64_t, 128>, SamplerRuntimeState::slotCount> heldNoteOrder {};
    double hostSampleRate = 44100.0;
    uint64_t ageCounter = 0;
    uint64_t noteOrderCounter = 0;
};
