#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>

class LSampler24AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::KeyListener
{
public:
    explicit LSampler24AudioProcessorEditor(LSampler24AudioProcessor&);
    ~LSampler24AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

private:
    void timerCallback() override;
    void chooseSample();
    void chooseLoadSlot();
    void chooseSaveSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);
    void selectSlot(int slotIndex, bool moveKeyboardFocus);
    void refreshSlotButtons();
    void enterSlotParameters();
    void leaveSlotParameters();
    void refreshParameterValues();
    static juce::String midiNoteText(int note);
    bool handleKeyPress(const juce::KeyPress& key, juce::Component* source);
    bool isActionButton(const juce::Component* component) const;
    bool isSlotButton(const juce::Component* component) const;

    LSampler24AudioProcessor& processor;
    std::array<juce::TextButton, LSampler24AudioProcessor::slotCount> slotButtons;
    juce::TextButton loadSample { "Load Sample" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };
    juce::Label status;
    juce::Label lowKeyLabel;
    juce::Slider lowKey;
    juce::Label highKeyLabel;
    juce::Slider highKey;
    juce::Label rootLabel;
    juce::Slider rootNote;
    juce::Label volumeLabel;
    juce::Slider volume;
    std::unique_ptr<juce::FileChooser> chooser;
    bool parameterPage = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
