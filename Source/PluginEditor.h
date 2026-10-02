#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>

class LSamplerSlotCell final : public juce::Label
{
public:
    std::function<void()> onActivate;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onActivate)
            onActivate();
        juce::Label::mouseDown(e);
    }
};

class LSamplerParameterComboBox final : public juce::ComboBox
{
public:
    void setLineReadingMode()
    {
        setTitle({});
        setDescription({});
    }

    void setEntryAccessibility()
    {
        setTitle("Grid");
        setDescription("Alt+L");
    }

    void focusGained(FocusChangeType cause) override
    {
        setEntryAccessibility();
        juce::ComboBox::focusGained(cause);
        juce::Timer::callAfterDelay(150,
            [safeThis = juce::Component::SafePointer<LSamplerParameterComboBox>(this)]
            {
                if (safeThis != nullptr)
                    safeThis->setLineReadingMode();
            });
    }
};

class LSamplerValueSlider final : public juce::Slider
{
public:
    void setParameterAccessibilityName(const juce::String& newName)
    {
        parameterName = newName;
        setLineReadingMode();
    }

    void setLineReadingMode()
    {
        const auto compactName = parameterName.isNotEmpty() ? parameterName : juce::String("Parameter value");
        setTitle(compactName);
        setName(compactName);
        setDescription({});
    }

    void setEntryAccessibility()
    {
        const auto compactName = parameterName.isNotEmpty() ? parameterName : juce::String("Parameter value");
        const auto entryName = "Value. " + compactName;
        setTitle(entryName);
        setName(entryName);
        setDescription("Alt+V");
    }

    void focusGained(FocusChangeType cause) override
    {
        setEntryAccessibility();
        juce::Slider::focusGained(cause);
        juce::Timer::callAfterDelay(150,
            [safeThis = juce::Component::SafePointer<LSamplerValueSlider>(this)]
            {
                if (safeThis != nullptr)
                    safeThis->setLineReadingMode();
            });
    }

private:
    juce::String parameterName;
};

class LSampler24AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::KeyListener
{
public:
    explicit LSampler24AudioProcessorEditor(LSampler24AudioProcessor&);
    ~LSampler24AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

private:
    enum class SlotParameter { lowKey = 0, highKey, originalPitch, volume, count };

    void timerCallback() override;
    void chooseSample();
    void chooseLoadSlot();
    void chooseSaveSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);
    void selectSlot(int slotIndex, bool moveKeyboardFocus);
    void refreshSlotCells();

    void enterSlotParameters();
    void leaveSlotParameters();
    void selectParameter(int index, bool announce);
    void refreshParameterGrid();
    void configureValueForSelectedParameter();
    void focusValue();
    void focusParameterGrid();
    void changeSelectedParameterValue(int direction, bool coarse);
    void setSelectedParameterBoundary(bool maximum);
    void changeStepWidth(int direction);
    void announceSelectedValue();
    double getSelectedParameterValue() const;
    void setSelectedParameterValue(double value);
    juce::String selectedParameterName() const;
    juce::String selectedParameterValueText() const;
    juce::String parameterCellText(int index) const;

    static juce::String midiNoteText(int note);
    bool handleKeyPress(const juce::KeyPress& key, juce::Component* source);
    bool isActionButton(const juce::Component* component) const;
    int slotCellIndex(const juce::Component* component) const;

    LSampler24AudioProcessor& processor;
    std::array<LSamplerSlotCell, LSampler24AudioProcessor::slotCount> slotCells;
    juce::TextButton loadSample { "Load Sample" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };
    juce::Label status;

    LSamplerParameterComboBox parameterSelector;
    LSamplerValueSlider parameterValue;
    std::unique_ptr<juce::LookAndFeel_V4> valueLookAndFeel;

    std::unique_ptr<juce::FileChooser> chooser;
    bool parameterPage = false;
    int selectedParameter = 0;
    int stepWidthIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
