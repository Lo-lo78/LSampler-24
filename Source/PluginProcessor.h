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
    enum class VoiceMode { Mono = 0, Poly = 1 };
    enum class MonoMode { Trigger = 0, Legato = 1 };

    struct SlotState
    {
        std::shared_ptr<SharedSample> sample;
        juce::File sampleFile;
        int lowKey = 0;
        int highKey = 127;
        int originalNote = 60;
        float volume = 1.0f;
        VoiceMode voiceMode = VoiceMode::Poly;
        MonoMode monoMode = MonoMode::Trigger;
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

    int getCurrentSlot() const noexcept { return currentSlot; }
    void setCurrentSlot(int oneBasedSlot);
    SlotState getSlotState(int oneBasedSlot) const;
    juce::String getSlotLabel(int oneBasedSlot) const;

    bool loadSample(const juce::File& file, juce::String& error);
    bool saveSlotPreset(const juce::File& presetFile, juce::String& error);
    bool loadSlotPreset(const juce::File& presetFile, juce::String& error);
    bool saveBankPreset(const juce::File& presetFile, juce::String& error);
    bool loadBankPreset(const juce::File& presetFile, juce::String& error);

    int getLowKey() const;
    void setLowKey(int note);
    int getHighKey() const;
    void setHighKey(int note);
    int getOriginalNote() const;
    void setOriginalNote(int note);
    float getVolume() const;
    void setVolume(float newVolume);
    VoiceMode getVoiceMode() const;
    void setVoiceMode(VoiceMode mode);
    MonoMode getMonoMode() const;
    void setMonoMode(MonoMode mode);
    juce::String getSampleStatus() const;

    LibraryManager& getLibrary() noexcept { return library; }

private:
    juce::ValueTree makeSlotStateTree(int slotIndex, const juce::String& type = "Slot") const;
    bool restoreSlotStateTree(int slotIndex, const juce::ValueTree& tree, juce::String& error);
    juce::ValueTree makeBankState(const juce::String& type) const;
    bool restoreBankState(const juce::ValueTree& tree, juce::String& error);
    bool materialiseSlotSample(int slotIndex, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    void syncVoiceBank();

    mutable juce::CriticalSection stateLock;
    std::array<SlotState, slotCount> slots {};
    int currentSlot = 1;
    VoiceBank voiceBank;
    LibraryManager library;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessor)
};
