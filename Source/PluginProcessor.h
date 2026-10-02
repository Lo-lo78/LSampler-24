#pragma once
#include <JuceHeader.h>
#include "SamplePool.h"
#include "SamplerVoice.h"
#include "LibraryManager.h"

class LSampler24AudioProcessor : public juce::AudioProcessor
{
public:
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

    juce::File getCurrentSampleFile() const;
    juce::String getSampleStatus() const;
    int getRootNote() const noexcept { return rootNote; }
    void setRootNote(int note);
    float getVolume() const noexcept { return volume; }
    void setVolume(float newVolume);

    LibraryManager& getLibrary() noexcept { return library; }

private:
    juce::ValueTree makeSlotState(const juce::String& type) const;
    bool restoreSlotState(const juce::ValueTree& tree, juce::String& error);
    bool writePreset(const juce::File& file, const juce::ValueTree& tree, juce::String& error) const;
    juce::ValueTree readPreset(const juce::File& file, juce::String& error) const;
    bool materialiseCurrentSample(juce::String& error);

    mutable juce::CriticalSection stateLock;
    std::shared_ptr<SharedSample> currentSample;
    juce::File currentSampleFile;
    VoiceBank voiceBank;
    LibraryManager library;
    int rootNote = 60;
    float volume = 1.0f;
    juce::String status = "No sample loaded";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessor)
};
