#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class LSampler24AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit LSampler24AudioProcessorEditor(LSampler24AudioProcessor&);
    ~LSampler24AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseSample();
    void chooseLoadSlot();
    void chooseSaveSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);

    LSampler24AudioProcessor& processor;
    juce::TextButton loadSample { "Load Sample" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };
    juce::Label status;
    juce::Label rootLabel;
    juce::Slider rootNote;
    juce::Label volumeLabel;
    juce::Slider volume;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
