#include "PluginEditor.h"
#include "ScreenReaderAnnouncer.h"
#include <cmath>
#include <utility>

namespace
{
constexpr std::array<int, 5> stepWidths { 1, 5, 10, 15, 20 };
constexpr int valuePageStep = 40;

using ValueEditorShortcut = std::function<bool(const juce::KeyPress&, juce::Component*)>;

class ShortcutValueTextEditor final : public juce::TextEditor
{
public:
    ShortcutValueTextEditor(const juce::String& name, ValueEditorShortcut shortcutToUse)
        : juce::TextEditor(name), shortcut(std::move(shortcutToUse)) {}

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getModifiers().isAltDown() && shortcut)
        {
            const auto code = key.getKeyCode();
            const bool valueNavigation = code == juce::KeyPress::upKey
                                      || code == juce::KeyPress::downKey
                                      || code == juce::KeyPress::leftKey
                                      || code == juce::KeyPress::rightKey
                                      || code == juce::KeyPress::pageUpKey
                                      || code == juce::KeyPress::pageDownKey
                                      || code == juce::KeyPress::homeKey
                                      || code == juce::KeyPress::endKey;
            const bool updatesValue = valueNavigation
                                   && code != juce::KeyPress::leftKey
                                   && code != juce::KeyPress::rightKey;
            const juce::Component::SafePointer<ShortcutValueTextEditor> safeThis(this);
            if (shortcut(key, this))
            {
                if (updatesValue && safeThis != nullptr)
                    if (auto* slider = safeThis->findParentComponentOfClass<juce::Slider>())
                    {
                        safeThis->setText(slider->getTextFromValue(slider->getValue()), false);
                        safeThis->selectAll();
                    }
                return true;
            }
        }

        if ((key.getKeyCode() == juce::KeyPress::returnKey
             || key.getKeyCode() == juce::KeyPress::escapeKey
             || key.getKeyCode() == juce::KeyPress::tabKey)
            && shortcut)
        {
            if (shortcut(key, this))
                return true;
        }

        return juce::TextEditor::keyPressed(key);
    }

private:
    ValueEditorShortcut shortcut;
};

class ShortcutSliderLabel final : public juce::Label
{
public:
    explicit ShortcutSliderLabel(ValueEditorShortcut shortcutToUse)
        : shortcut(std::move(shortcutToUse)) {}

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return createIgnoredAccessibilityHandler(*this);
    }

protected:
    juce::TextEditor* createEditorComponent() override
    {
        auto* editor = new ShortcutValueTextEditor(getName(), shortcut);
        editor->setInputRestrictions(0, "0123456789.");
        editor->applyFontToAllText(getLookAndFeel().getLabelFont(*this));
        return editor;
    }

private:
    ValueEditorShortcut shortcut;
};

class ValueLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit ValueLookAndFeel(ValueEditorShortcut shortcutToUse)
        : shortcut(std::move(shortcutToUse)) {}

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = new ShortcutSliderLabel(shortcut);
        label->setJustificationType(juce::Justification::centred);
        label->setKeyboardType(juce::TextInputTarget::decimalKeyboard);
        label->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
        label->setColour(juce::Label::backgroundColourId, slider.findColour(juce::Slider::textBoxBackgroundColourId));
        label->setColour(juce::Label::outlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
        return label;
    }

private:
    ValueEditorShortcut shortcut;
};
}

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(760, 520);

    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[static_cast<size_t>(i)];
        cell.setAccessible(true);
        cell.setEditable(false, false, false);
        cell.setJustificationType(juce::Justification::centredLeft);
        cell.setWantsKeyboardFocus(i == processor.getCurrentSlot());
        cell.setExplicitFocusOrder(1);
        cell.addKeyListener(this);
        cell.onActivate = [this, i] { selectSlot(i, true); };
        addAndMakeVisible(cell);
    }

    auto addButton = [this](juce::TextButton& b, int order)
    {
        b.setWantsKeyboardFocus(true);
        b.setExplicitFocusOrder(order);
        b.addKeyListener(this);
        addAndMakeVisible(b);
    };
    addButton(loadSample, 2);
    addButton(loadSlot, 3);
    addButton(saveSlot, 4);
    addButton(loadBank, 5);
    addButton(saveBank, 6);

    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    status.setAccessible(true);
    status.setTitle("Sample status");
    status.setWantsKeyboardFocus(false);
    addAndMakeVisible(status);

    parameterSelector.setWantsKeyboardFocus(true);
    parameterSelector.setExplicitFocusOrder(1);
    parameterSelector.addKeyListener(this);
    for (int i = 0; i < static_cast<int>(SlotParameter::count); ++i)
        parameterSelector.addItem(parameterCellText(i), i + 1);
    parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
    parameterSelector.onChange = [this]
    {
        const int index = parameterSelector.getSelectedItemIndex();
        if (index >= 0)
        {
            selectedParameter = index;
            configureValueForSelectedParameter();
        }
    };
    addAndMakeVisible(parameterSelector);

    valueLookAndFeel = std::make_unique<ValueLookAndFeel>(
        [this](const juce::KeyPress& key, juce::Component* source)
        {
            return handleKeyPress(key, source);
        });

    parameterValue.setSliderStyle(juce::Slider::LinearHorizontal);
    parameterValue.setTextBoxStyle(juce::Slider::TextBoxRight, false, 190, 28);
    parameterValue.setLookAndFeel(valueLookAndFeel.get());
    parameterValue.setWantsKeyboardFocus(true);
    parameterValue.setExplicitFocusOrder(2);
    parameterValue.addKeyListener(this);
    parameterValue.onValueChange = [this]
    {
        if (!parameterPage) return;
        setSelectedParameterValue(parameterValue.getValue());
        refreshParameterGrid();
        announceSelectedValue();
    };
    addAndMakeVisible(parameterValue);

    loadSample.onClick = [this] { chooseSample(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };

    refreshSlotCells();
    leaveSlotParameters();
    startTimerHz(5);
}

LSampler24AudioProcessorEditor::~LSampler24AudioProcessorEditor()
{
    parameterValue.setLookAndFeel(nullptr);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(20.0f);
    g.drawText("LSampler-24 - 24 Slots", 16, 12, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void LSampler24AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(42);

    if (!parameterPage)
    {
        auto grid = area.removeFromTop(8 * 34);
        const int gap = 4;
        const int colWidth = (grid.getWidth() - gap * 2) / 3;
        for (int col = 0; col < 3; ++col)
        {
            auto colArea = grid.removeFromLeft(colWidth);
            if (col < 2) grid.removeFromLeft(gap);
            for (int row = 0; row < 8; ++row)
            {
                const int index = col * 8 + row;
                slotCells[static_cast<size_t>(index)].setBounds(colArea.removeFromTop(32));
                colArea.removeFromTop(2);
            }
        }

        area.removeFromTop(10);
        auto buttons = area.removeFromTop(36);
        const int w = (buttons.getWidth() - gap * 4) / 5;
        loadSample.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadBank.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveBank.setBounds(buttons);

        area.removeFromTop(10);
        status.setBounds(area.removeFromTop(28));
    }
    else
    {
        status.setBounds(area.removeFromBottom(28));
        parameterSelector.setBounds(area.removeFromTop(42));
        area.removeFromTop(10);
        parameterValue.setBounds(area.removeFromTop(42));
    }
}

void LSampler24AudioProcessorEditor::selectSlot(int slotIndex, bool moveKeyboardFocus)
{
    slotIndex = juce::jlimit(0, LSampler24AudioProcessor::slotCount - 1, slotIndex);
    processor.setCurrentSlot(slotIndex);
    refreshSlotCells();
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    if (moveKeyboardFocus)
        slotCells[static_cast<size_t>(slotIndex)].grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::refreshSlotCells()
{
    const int selected = processor.getCurrentSlot();
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[static_cast<size_t>(i)];
        const auto label = processor.getSlotLabel(i);
        cell.setText(label, juce::dontSendNotification);
        cell.setTitle({});
        cell.setDescription({});
        cell.setWantsKeyboardFocus(!parameterPage && i == selected);
    }
}

juce::String LSampler24AudioProcessorEditor::midiNoteText(int note)
{
    note = juce::jlimit(0, 127, note);
    static const char* names[] = { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };
    return juce::String(note) + " " + names[note % 12] + " " + juce::String((note / 12) - 1);
}

juce::String LSampler24AudioProcessorEditor::selectedParameterName() const
{
    switch (static_cast<SlotParameter>(selectedParameter))
    {
        case SlotParameter::lowKey:        return "Low Key";
        case SlotParameter::highKey:       return "High Key";
        case SlotParameter::originalPitch: return "Original Pitch";
        case SlotParameter::volume:        return "Volume";
        default:                           return "Parameter";
    }
}

double LSampler24AudioProcessorEditor::getSelectedParameterValue() const
{
    switch (static_cast<SlotParameter>(selectedParameter))
    {
        case SlotParameter::lowKey:        return processor.getLowKey();
        case SlotParameter::highKey:       return processor.getHighKey();
        case SlotParameter::originalPitch: return processor.getRootNote();
        case SlotParameter::volume:        return processor.getVolume();
        default:                           return 0.0;
    }
}

void LSampler24AudioProcessorEditor::setSelectedParameterValue(double value)
{
    switch (static_cast<SlotParameter>(selectedParameter))
    {
        case SlotParameter::lowKey:        processor.setLowKey(juce::roundToInt(value)); break;
        case SlotParameter::highKey:       processor.setHighKey(juce::roundToInt(value)); break;
        case SlotParameter::originalPitch: processor.setRootNote(juce::roundToInt(value)); break;
        case SlotParameter::volume:        processor.setVolume(static_cast<float>(value)); break;
        default: break;
    }
}

juce::String LSampler24AudioProcessorEditor::selectedParameterValueText() const
{
    if (selectedParameter == static_cast<int>(SlotParameter::volume))
        return juce::String(processor.getVolume(), 2);
    return midiNoteText(juce::roundToInt(getSelectedParameterValue()));
}

juce::String LSampler24AudioProcessorEditor::parameterCellText(int index) const
{
    const auto old = selectedParameter;
    const_cast<LSampler24AudioProcessorEditor*>(this)->selectedParameter = juce::jlimit(0, 3, index);
    const auto text = selectedParameterName() + ", " + selectedParameterValueText();
    const_cast<LSampler24AudioProcessorEditor*>(this)->selectedParameter = old;
    return text;
}

void LSampler24AudioProcessorEditor::refreshParameterGrid()
{
    for (int i = 0; i < static_cast<int>(SlotParameter::count); ++i)
        parameterSelector.changeItemText(i + 1, parameterCellText(i));

    parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
    configureValueForSelectedParameter();
}

void LSampler24AudioProcessorEditor::configureValueForSelectedParameter()
{
    const bool volume = selectedParameter == static_cast<int>(SlotParameter::volume);
    if (volume)
    {
        parameterValue.setRange(0.0, 1.0, 0.0);
        parameterValue.textFromValueFunction = [](double v) { return juce::String(v, 2); };
    }
    else
    {
        parameterValue.setRange(0.0, 127.0, 1.0);
        parameterValue.textFromValueFunction = [](double v) { return midiNoteText(juce::roundToInt(v)); };
    }

    parameterValue.setParameterAccessibilityName(selectedParameterName());
    parameterValue.setValue(getSelectedParameterValue(), juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::enterSlotParameters()
{
    parameterPage = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    parameterSelector.setVisible(true);
    parameterValue.setVisible(true);
    selectedParameter = juce::jlimit(0, 3, selectedParameter);
    refreshParameterGrid();
    resized();
    parameterSelector.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveSlotParameters()
{
    parameterPage = false;
    parameterSelector.setVisible(false);
    parameterValue.setVisible(false);
    for (auto& cell : slotCells) cell.setVisible(true);
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
    refreshSlotCells();
    resized();
}

void LSampler24AudioProcessorEditor::selectParameter(int index, bool announce)
{
    selectedParameter = juce::jlimit(0, static_cast<int>(SlotParameter::count) - 1, index);
    parameterSelector.setSelectedItemIndex(selectedParameter,
        announce ? juce::sendNotificationSync : juce::dontSendNotification);
    configureValueForSelectedParameter();
}

void LSampler24AudioProcessorEditor::focusValue()
{
    configureValueForSelectedParameter();
    parameterValue.setEntryAccessibility();
    parameterValue.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::focusParameterGrid()
{
    refreshParameterGrid();
    parameterSelector.setEntryAccessibility();
    parameterSelector.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::announceSelectedValue()
{
    auto* source = juce::Component::getCurrentlyFocusedComponent();
    if (source == nullptr)
        source = &status;
    lsampler::announceToActiveScreenReader(*source, selectedParameterValueText());
}

void LSampler24AudioProcessorEditor::changeStepWidth(int direction)
{
    const auto next = juce::jlimit(0, static_cast<int>(stepWidths.size()) - 1, stepWidthIndex + direction);
    if (next == stepWidthIndex)
        return;
    stepWidthIndex = next;

    auto* source = juce::Component::getCurrentlyFocusedComponent();
    if (source == nullptr)
        source = &status;
    lsampler::announceToActiveScreenReader(*source, "Step " + juce::String(stepWidths[static_cast<size_t>(stepWidthIndex)]));
}

void LSampler24AudioProcessorEditor::changeSelectedParameterValue(int direction, bool coarse)
{
    const double baseStep = selectedParameter == static_cast<int>(SlotParameter::volume) ? 0.01 : 1.0;
    const double multiplier = static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)])
                            * (coarse ? static_cast<double>(valuePageStep) : 1.0);
    const double step = baseStep * multiplier;
    const double maximum = selectedParameter == static_cast<int>(SlotParameter::volume) ? 1.0 : 127.0;
    const auto current = getSelectedParameterValue();
    const auto next = juce::jlimit(0.0, maximum, current + direction * step);
    if (std::abs(next - current) < 1.0e-9)
        return;
    setSelectedParameterValue(next);
    refreshParameterGrid();
    announceSelectedValue();
}

void LSampler24AudioProcessorEditor::setSelectedParameterBoundary(bool maximum)
{
    const double value = maximum
        ? (selectedParameter == static_cast<int>(SlotParameter::volume) ? 1.0 : 127.0)
        : 0.0;
    if (std::abs(value - getSelectedParameterValue()) < 1.0e-9)
        return;
    setSelectedParameterValue(value);
    refreshParameterGrid();
    announceSelectedValue();
}

bool LSampler24AudioProcessorEditor::isActionButton(const juce::Component* component) const
{
    return component == &loadSample || component == &loadSlot || component == &saveSlot
        || component == &loadBank || component == &saveBank;
}

int LSampler24AudioProcessorEditor::slotCellIndex(const juce::Component* component) const
{
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
        if (component == &slotCells[static_cast<size_t>(i)]) return i;
    return -1;
}

bool LSampler24AudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    return handleKeyPress(key, juce::Component::getCurrentlyFocusedComponent());
}

bool LSampler24AudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    return handleKeyPress(key, originatingComponent);
}

bool LSampler24AudioProcessorEditor::handleKeyPress(const juce::KeyPress& key, juce::Component* source)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    const int sourceSlot = slotCellIndex(source);
    const bool sourceIsSlot = sourceSlot >= 0;
    const bool sourceIsValueEditor = dynamic_cast<juce::TextEditor*>(source) != nullptr
                                  && parameterValue.isParentOf(source);

    if (code == juce::KeyPress::spaceKey && (parameterPage || sourceIsSlot))
    {
        processor.requestPreviewToggle();
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'l' && !mods.isShiftDown())
        {
            if (!parameterPage)
                enterSlotParameters();
            focusParameterGrid();
            return true;
        }
        if (ch == 'o' && !mods.isShiftDown()) { chooseSample(); return true; }
        if (ch == 's') { mods.isShiftDown() ? chooseSaveSlot() : chooseLoadSlot(); return true; }
        if (ch == 'b') { mods.isShiftDown() ? chooseSaveBank() : chooseLoadBank(); return true; }
        if (ch == 'v' && parameterPage) { focusValue(); return true; }

        if (parameterPage && source != &parameterValue)
        {
            if (code == juce::KeyPress::upKey)       { changeSelectedParameterValue(1, false); return true; }
            if (code == juce::KeyPress::downKey)     { changeSelectedParameterValue(-1, false); return true; }
            if (code == juce::KeyPress::leftKey)     { changeStepWidth(-1); return true; }
            if (code == juce::KeyPress::rightKey)    { changeStepWidth(1); return true; }
            if (code == juce::KeyPress::pageUpKey)   { changeSelectedParameterValue(1, true); return true; }
            if (code == juce::KeyPress::pageDownKey) { changeSelectedParameterValue(-1, true); return true; }
            if (code == juce::KeyPress::homeKey)     { setSelectedParameterBoundary(true); return true; }
            if (code == juce::KeyPress::endKey)      { setSelectedParameterBoundary(false); return true; }
        }
    }

    if (sourceIsValueEditor)
    {
        if (code == juce::KeyPress::returnKey) { focusParameterGrid(); return true; }
        if (code == juce::KeyPress::escapeKey) { focusValue(); return true; }
        if (code == juce::KeyPress::tabKey && !mods.isShiftDown()) { focusParameterGrid(); return true; }
    }

    if (parameterPage)
    {
        if (code == juce::KeyPress::escapeKey)
        {
            leaveSlotParameters();
            selectSlot(processor.getCurrentSlot(), true);
            return true;
        }

        if (source == &parameterValue)
        {
            if (code == juce::KeyPress::tabKey && !mods.isShiftDown())
            {
                parameterValue.showTextBox();
                return true;
            }
            if (code == juce::KeyPress::returnKey) { focusParameterGrid(); return true; }
            if (code == juce::KeyPress::leftKey)   { changeStepWidth(-1); return true; }
            if (code == juce::KeyPress::rightKey)  { changeStepWidth(1); return true; }
            if (code == juce::KeyPress::upKey)     { changeSelectedParameterValue(1, false); return true; }
            if (code == juce::KeyPress::downKey)   { changeSelectedParameterValue(-1, false); return true; }
            if (code == juce::KeyPress::pageUpKey) { changeSelectedParameterValue(1, true); return true; }
            if (code == juce::KeyPress::pageDownKey) { changeSelectedParameterValue(-1, true); return true; }
            if (code == juce::KeyPress::homeKey)   { setSelectedParameterBoundary(true); return true; }
            if (code == juce::KeyPress::endKey)    { setSelectedParameterBoundary(false); return true; }
            return false;
        }

        if (source == &parameterSelector)
        {
            parameterSelector.setLineReadingMode();

            if (code == juce::KeyPress::returnKey) { focusValue(); return true; }
            if (mods.isCtrlDown() && code == juce::KeyPress::homeKey) { selectParameter(0, true); return true; }
            if (mods.isCtrlDown() && code == juce::KeyPress::endKey)
            {
                selectParameter(static_cast<int>(SlotParameter::count) - 1, true);
                return true;
            }
            if (code == juce::KeyPress::homeKey) { selectParameter(0, true); return true; }
            if (code == juce::KeyPress::endKey)
            {
                selectParameter(static_cast<int>(SlotParameter::count) - 1, true);
                return true;
            }
            if (code == juce::KeyPress::pageUpKey) { selectParameter(juce::jmax(0, selectedParameter - 3), true); return true; }
            if (code == juce::KeyPress::pageDownKey)
            {
                selectParameter(juce::jmin(static_cast<int>(SlotParameter::count) - 1, selectedParameter + 3), true);
                return true;
            }
            if (code == juce::KeyPress::upKey)
            {
                if (selectedParameter > 0) selectParameter(selectedParameter - 1, true);
                return true;
            }
            if (code == juce::KeyPress::downKey)
            {
                if (selectedParameter + 1 < static_cast<int>(SlotParameter::count))
                    selectParameter(selectedParameter + 1, true);
                return true;
            }
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
                return true;
        }
        return false;
    }

    if (isActionButton(source))
    {
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
            || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
            || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
            return true;
        return false;
    }

    if (!sourceIsSlot) return false;

    if (code == juce::KeyPress::returnKey) { enterSlotParameters(); return true; }
    if (mods.isCtrlDown() && code == juce::KeyPress::homeKey) { selectSlot(0, true); return true; }
    if (mods.isCtrlDown() && code == juce::KeyPress::endKey)
    {
        selectSlot(LSampler24AudioProcessor::slotCount - 1, true);
        return true;
    }

    const int slot = processor.getCurrentSlot();
    const int row = slot % 8;
    const int col = slot / 8;
    if (code == juce::KeyPress::upKey)    { if (row > 0) selectSlot(slot - 1, true); return true; }
    if (code == juce::KeyPress::downKey)  { if (row < 7) selectSlot(slot + 1, true); return true; }
    if (code == juce::KeyPress::leftKey)  { if (col > 0) selectSlot(slot - 8, true); return true; }
    if (code == juce::KeyPress::rightKey) { if (col < 2) selectSlot(slot + 8, true); return true; }
    if (code == juce::KeyPress::homeKey)  { selectSlot(col * 8, true); return true; }
    if (code == juce::KeyPress::endKey)   { selectSlot(col * 8 + 7, true); return true; }
    return false;
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    refreshSlotCells();
    if (parameterPage)
        refreshParameterGrid();
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotCells();
    if (parameterPage) refreshParameterGrid();
}

void LSampler24AudioProcessorEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = safeThis->processor.loadSample(file, error);
                safeThis->showResult(ok, error, safeThis->processor.getSampleStatus());
                if (ok)
                {
                    const int slot = safeThis->processor.getCurrentSlot();
                    juce::Timer::callAfterDelay(80,
                        [safeThis, slot]
                        {
                            if (safeThis != nullptr)
                                safeThis->selectSlot(slot, true);
                        });
                }
            }
        });
}

void LSampler24AudioProcessorEditor::chooseLoadSlot()
{
    chooser = std::make_unique<juce::FileChooser>("Load Slot", processor.getLibrary().slots(), "*.lsampler-24-s");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = safeThis->processor.loadSlotPreset(file, error);
                safeThis->showResult(ok, error, "Slot loaded: " + file.getFileNameWithoutExtension());
                if (ok)
                    safeThis->selectSlot(safeThis->processor.getCurrentSlot(), true);
            }
        });
}

void LSampler24AudioProcessorEditor::chooseSaveSlot()
{
    auto initial = processor.getLibrary().slots().getChildFile(LibraryManager::defaultName("Slot") + LibraryManager::slotExtension);
    chooser = std::make_unique<juce::FileChooser>("Save Slot", initial, "*.lsampler-24-s");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.getFullPathName().isNotEmpty())
            {
                if (!file.hasFileExtension(LibraryManager::slotExtension)) file = file.withFileExtension(LibraryManager::slotExtension);
                juce::String error;
                const bool ok = safeThis->processor.saveSlotPreset(file, error);
                safeThis->showResult(ok, error, "Slot saved: " + file.getFileNameWithoutExtension());
            }
        });
}

void LSampler24AudioProcessorEditor::chooseLoadBank()
{
    chooser = std::make_unique<juce::FileChooser>("Load Bank", processor.getLibrary().banks(), "*.lsampler-24-b");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = safeThis->processor.loadBankPreset(file, error);
                safeThis->showResult(ok, error, "Bank loaded: " + file.getFileNameWithoutExtension());
                safeThis->selectSlot(safeThis->processor.getCurrentSlot(), true);
            }
        });
}

void LSampler24AudioProcessorEditor::chooseSaveBank()
{
    auto initial = processor.getLibrary().banks().getChildFile(LibraryManager::defaultName("Bank") + LibraryManager::bankExtension);
    chooser = std::make_unique<juce::FileChooser>("Save Bank", initial, "*.lsampler-24-b");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.getFullPathName().isNotEmpty())
            {
                if (!file.hasFileExtension(LibraryManager::bankExtension)) file = file.withFileExtension(LibraryManager::bankExtension);
                juce::String error;
                const bool ok = safeThis->processor.saveBankPreset(file, error);
                safeThis->showResult(ok, error, "Bank saved: " + file.getFileNameWithoutExtension());
            }
        });
}
