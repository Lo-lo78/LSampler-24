#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>

class LSampler24AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit LSampler24AudioProcessorEditor(LSampler24AudioProcessor&);
    ~LSampler24AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class SlotButton : public juce::TextButton
    {
    public:
        explicit SlotButton(int index) : slotIndex(index) {}
        std::function<bool(int, const juce::KeyPress&)> onGridKey;
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (onGridKey && onGridKey(slotIndex, key)) return true;
            return juce::TextButton::keyPressed(key);
        }
        int slotIndex = 0;
    };

    void timerCallback() override;
    void chooseSample();
    void chooseLoadSlot();
    void chooseSaveSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);

    void selectSlot(int index, bool takeFocus, bool enterParameters = false);
    bool handleGridKey(int slot, const juce::KeyPress& key);
    void showSlotsPage(bool takeFocus);
    void showParametersPage();
    void refreshSlotButtons();
    void refreshParameterControls();
    void updateParameterVisibility();

    LSampler24AudioProcessor& processor;
    std::array<std::unique_ptr<SlotButton>, LSampler24AudioProcessor::slotCount> slotButtons;

    juce::TextButton loadSample { "Load Sample" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };
    juce::Label status;

    juce::Label lowKeyLabel, highKeyLabel, originalPitchLabel, voiceModeLabel, monoModeLabel, volumeLabel;
    juce::Slider lowKey, highKey, originalPitch, volume;
    juce::ComboBox voiceMode, monoMode;

    bool parametersPage = false;
    bool updatingControls = false;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
