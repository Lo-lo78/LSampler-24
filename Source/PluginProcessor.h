#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include "SamplerVoice.h"
#include "LibraryManager.h"
#include <array>
#include <memory>

class LSampler24AudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int slotCount = 24;

    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        int lowKey = 0;
        int highKey = 127;
        int originalPitch = 60;
        int voiceMode = 0; // 0 Poly, 1 Mono
        int monoMode = 0;  // 0 Trigger, 1 Legato
        float volume = 1.0f;
        juce::String status = "empty";
    };

    LSampler24AudioProcessor();
    ~LSampler24AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int getCurrentSlot() const noexcept { return currentSlot.load(); }
    void setCurrentSlot(int slot);
    SlotState getSlotState(int slot) const;
    juce::String getSlotLabel(int slot) const;

    bool loadSample(const juce::File& file, juce::String& error);
    bool saveSlotPreset(const juce::File& presetFile, juce::String& error);
    bool loadSlotPreset(const juce::File& presetFile, juce::String& error);
    bool saveBankPreset(const juce::File& presetFile, juce::String& error);
    bool loadBankPreset(const juce::File& presetFile, juce::String& error);

    void setLowKey(int value);
    void setHighKey(int value);
    void setOriginalPitch(int value);
    void setVoiceMode(int value);
    void setMonoMode(int value);
    void setVolume(float value);

    juce::String getSampleStatus() const;
    LibraryManager& getLibrary() noexcept { return library; }

    static juce::String noteName(int midiNote);

private:
    juce::ValueTree makeSlotState(int slot, const juce::String& type) const;
    bool restoreSlotState(int slot, const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseSlotSample(int slot, juce::String& error);
    void publishRuntimeSnapshot();
    std::shared_ptr<const SamplerRuntimeState> buildRuntimeSnapshot() const;

    mutable juce::CriticalSection stateLock;
    std::array<SlotState, slotCount> slots {};
    std::atomic<int> currentSlot { 0 };
    std::shared_ptr<const SamplerRuntimeState> runtimeSnapshot;
    VoiceBank voiceBank;
    LibraryManager library;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessor)
};
