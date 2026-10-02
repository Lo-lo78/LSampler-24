#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePool.h"
#include "SamplerVoice.h"
#include "LibraryManager.h"
#include <array>

class LSampler24AudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int slotCount = 24;

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

    bool loadSample(const juce::File& file, juce::String& error);
    bool saveSlotPreset(const juce::File& presetFile, juce::String& error);
    bool loadSlotPreset(const juce::File& presetFile, juce::String& error);
    bool saveBankPreset(const juce::File& presetFile, juce::String& error);
    bool loadBankPreset(const juce::File& presetFile, juce::String& error);

    int getCurrentSlot() const noexcept { return currentSlot; }
    void setCurrentSlot(int slotIndex);
    juce::String getSlotLabel(int slotIndex) const;

    juce::File getCurrentSampleFile() const;
    juce::String getSampleStatus() const;
    int getLowKey() const noexcept;
    void setLowKey(int note);
    int getHighKey() const noexcept;
    void setHighKey(int note);
    int getRootNote() const noexcept;
    void setRootNote(int note);
    float getVolume() const noexcept;
    void setVolume(float newVolume);
    void requestPreviewToggle() noexcept { previewToggleRequested.store(true); }

    LibraryManager& getLibrary() noexcept { return library; }

private:
    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        int lowKey = 0;
        int highKey = 127;
        int rootNote = 60;
        float volume = 1.0f;
        juce::String status = "No sample loaded";
    };

    juce::ValueTree makeSlotState(int slotIndex, const juce::String& type) const;
    bool restoreSlotState(int slotIndex, const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseSlotSample(int slotIndex, juce::String& error);
    void applyCurrentSlotToVoiceBank();

    mutable juce::CriticalSection stateLock;
    std::array<SlotState, slotCount> slots;
    int currentSlot = 0;
    std::atomic<int> activeLowKey { 0 };
    std::atomic<int> activeHighKey { 127 };
    std::atomic<bool> previewToggleRequested { false };
    bool previewPlaying = false;

    // Intentionally unchanged TEST2 audio engine for this diagnostic stage.
    VoiceBank voiceBank;
    LibraryManager library;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessor)
};
