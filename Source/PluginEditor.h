#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>

class LSampler24AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::KeyListener
{
public:
    explicit LSampler24AudioProcessorEditor(LSampler24AudioProcessor&);
    ~LSampler24AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;

    void setCurrentSlot(int slot, bool focusIt = true);
    void moveSlot(int rowDelta, int colDelta);
    void showSlotsView(bool focusGrid = true);
    void showParametersView();
    void refreshSlotButtons();
    void refreshParameterControls();
    void focusParameter(int index);

    void chooseSample();
    void chooseLoadSlot();
    void chooseSaveSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);
    void addKeyTarget(juce::Component& component);

    LSampler24AudioProcessor& processor;
    std::array<std::unique_ptr<juce::TextButton>, LSampler24AudioProcessor::slotCount> slotButtons;
    juce::TextButton loadSample { "Load Sample" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };

    juce::Label status;
    juce::Slider lowKey;
    juce::Slider highKey;
    juce::Slider originalNote;
    juce::ComboBox voiceMode;
    juce::ComboBox monoMode;
    juce::Slider volume;

    bool parametersView = false;
    int parameterFocus = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
