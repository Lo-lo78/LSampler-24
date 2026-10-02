#include "PluginEditor.h"
#include "ScreenReaderAnnouncer.h"
#include <cmath>

namespace
{
constexpr std::array<int, 5> stepWidths { 1, 5, 10, 15, 20 };
constexpr int valuePageStep = 40;
}

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(760, 520);

    for (int i = 0; i < static_cast<int>(slotButtons.size()); ++i)
    {
        auto& b = slotButtons[static_cast<size_t>(i)];
        b.setButtonText(processor.getSlotLabel(i));
        b.setTitle(processor.getSlotLabel(i));
        b.setWantsKeyboardFocus(i == processor.getCurrentSlot());
        b.onClick = [this, i] { selectSlot(i, false); };
        b.addKeyListener(this);
        addAndMakeVisible(b);
    }

    auto addButton = [this](juce::TextButton& b)
    {
        b.setWantsKeyboardFocus(true);
        b.addKeyListener(this);
        addAndMakeVisible(b);
    };
    addButton(loadSample);
    addButton(loadSlot);
    addButton(saveSlot);
    addButton(loadBank);
    addButton(saveBank);

    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    status.setAccessible(true);
    status.setTitle("Sample status");
    addAndMakeVisible(status);

    for (int i = 0; i < static_cast<int>(parameterButtons.size()); ++i)
    {
        auto& b = parameterButtons[static_cast<size_t>(i)];
        b.setWantsKeyboardFocus(i == 0);
        b.addKeyListener(this);
        b.onClick = [this, i] { selectParameter(i, false); };
        addAndMakeVisible(b);
    }

    parameterValue.setSliderStyle(juce::Slider::LinearHorizontal);
    parameterValue.setTextBoxStyle(juce::Slider::TextBoxRight, false, 170, 28);
    parameterValue.setWantsKeyboardFocus(true);
    parameterValue.setTitle("Value");
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

    refreshSlotButtons();
    leaveSlotParameters();
    startTimerHz(5);
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
                slotButtons[static_cast<size_t>(index)].setBounds(colArea.removeFromTop(32));
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
        auto valueArea = area.removeFromBottom(42);
        parameterValue.setBounds(valueArea);
        area.removeFromBottom(8);

        // Parameter grid: currently four parameters in one column.  The same
        // navigation code is ready to grow into more columns later.
        for (auto& b : parameterButtons)
        {
            b.setBounds(area.removeFromTop(38));
            area.removeFromTop(4);
        }
    }
}

void LSampler24AudioProcessorEditor::selectSlot(int slotIndex, bool moveKeyboardFocus)
{
    slotIndex = juce::jlimit(0, LSampler24AudioProcessor::slotCount - 1, slotIndex);
    processor.setCurrentSlot(slotIndex);
    refreshSlotButtons();
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    if (moveKeyboardFocus)
        slotButtons[static_cast<size_t>(slotIndex)].grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::refreshSlotButtons()
{
    const int selected = processor.getCurrentSlot();
    for (int i = 0; i < static_cast<int>(slotButtons.size()); ++i)
    {
        auto& b = slotButtons[static_cast<size_t>(i)];
        const auto label = processor.getSlotLabel(i);
        b.setButtonText(label);
        b.setTitle(label);
        b.setWantsKeyboardFocus(!parameterPage && i == selected);
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
    for (int i = 0; i < static_cast<int>(parameterButtons.size()); ++i)
    {
        auto& b = parameterButtons[static_cast<size_t>(i)];
        const auto text = parameterCellText(i);
        b.setButtonText(text);
        b.setTitle(text);
        b.setWantsKeyboardFocus(parameterPage && i == selectedParameter);
    }
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
    parameterValue.setName("Value");
    parameterValue.setTitle("Value. " + selectedParameterName());
    parameterValue.setValue(getSelectedParameterValue(), juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::enterSlotParameters()
{
    parameterPage = true;
    for (auto& b : slotButtons) { b.setVisible(false); b.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    for (auto& b : parameterButtons) b.setVisible(true);
    parameterValue.setVisible(true);
    selectedParameter = juce::jlimit(0, 3, selectedParameter);
    refreshParameterGrid();
    resized();
    parameterButtons[static_cast<size_t>(selectedParameter)].grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveSlotParameters()
{
    parameterPage = false;
    for (auto& b : parameterButtons) { b.setVisible(false); b.setWantsKeyboardFocus(false); }
    parameterValue.setVisible(false);
    for (auto& b : slotButtons) b.setVisible(true);
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
    refreshSlotButtons();
    resized();
}

void LSampler24AudioProcessorEditor::selectParameter(int index, bool moveKeyboardFocus)
{
    selectedParameter = juce::jlimit(0, static_cast<int>(parameterButtons.size()) - 1, index);
    refreshParameterGrid();
    if (moveKeyboardFocus)
        parameterButtons[static_cast<size_t>(selectedParameter)].grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::focusValue()
{
    configureValueForSelectedParameter();
    parameterValue.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::focusParameterGrid()
{
    refreshParameterGrid();
    parameterButtons[static_cast<size_t>(selectedParameter)].grabKeyboardFocus();
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

int LSampler24AudioProcessorEditor::parameterButtonIndex(const juce::Component* component) const
{
    for (int i = 0; i < static_cast<int>(parameterButtons.size()); ++i)
        if (component == &parameterButtons[static_cast<size_t>(i)]) return i;
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

    bool sourceIsSlot = false;
    for (const auto& button : slotButtons)
        if (source == &button) { sourceIsSlot = true; break; }

    if (code == juce::KeyPress::spaceKey && (parameterPage || sourceIsSlot))
    {
        processor.requestPreviewToggle();
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'l' && !mods.isShiftDown())
        {
            if (parameterPage) leaveSlotParameters();
            selectSlot(processor.getCurrentSlot(), true);
            return true;
        }
        if (ch == 'o' && !mods.isShiftDown()) { chooseSample(); return true; }
        if (ch == 's') { mods.isShiftDown() ? chooseSaveSlot() : chooseLoadSlot(); return true; }
        if (ch == 'b') { mods.isShiftDown() ? chooseSaveBank() : chooseLoadBank(); return true; }
        if (ch == 'v' && parameterPage) { focusValue(); return true; }

        // LJuno-style editing while focus remains in the parameter grid.
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

        const int p = parameterButtonIndex(source);
        if (p >= 0)
        {
            selectedParameter = p;
            if (code == juce::KeyPress::returnKey) { focusValue(); return true; }
            if (code == juce::KeyPress::upKey)
            {
                if (selectedParameter > 0) selectParameter(selectedParameter - 1, true);
                return true;
            }
            if (code == juce::KeyPress::downKey)
            {
                if (selectedParameter + 1 < static_cast<int>(parameterButtons.size()))
                    selectParameter(selectedParameter + 1, true);
                return true;
            }
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
                || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
                return true;
        }
        return false;
    }

    if (isActionButton(source))
    {
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
            || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
            return true;
        return false;
    }

    int slotIndex = -1;
    for (int i = 0; i < static_cast<int>(slotButtons.size()); ++i)
        if (source == &slotButtons[static_cast<size_t>(i)]) { slotIndex = i; break; }
    if (slotIndex < 0) return false;

    if (code == juce::KeyPress::returnKey) { enterSlotParameters(); return true; }

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
    refreshSlotButtons();
    if (parameterPage)
        refreshParameterGrid();
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotButtons();
    if (parameterPage) refreshParameterGrid();
}

void LSampler24AudioProcessorEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = processor.loadSample(file, error);
                showResult(ok, error, processor.getSampleStatus());
            }
        });
}

void LSampler24AudioProcessorEditor::chooseLoadSlot()
{
    chooser = std::make_unique<juce::FileChooser>("Load Slot", processor.getLibrary().slots(), "*.lsampler-24-s");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = processor.loadSlotPreset(file, error);
                showResult(ok, error, "Slot loaded: " + file.getFileNameWithoutExtension());
            }
        });
}

void LSampler24AudioProcessorEditor::chooseSaveSlot()
{
    auto initial = processor.getLibrary().slots().getChildFile(LibraryManager::defaultName("Slot") + LibraryManager::slotExtension);
    chooser = std::make_unique<juce::FileChooser>("Save Slot", initial, "*.lsampler-24-s");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.getFullPathName().isNotEmpty())
            {
                if (!file.hasFileExtension(LibraryManager::slotExtension)) file = file.withFileExtension(LibraryManager::slotExtension);
                juce::String error;
                const bool ok = processor.saveSlotPreset(file, error);
                showResult(ok, error, "Slot saved: " + file.getFileNameWithoutExtension());
            }
        });
}

void LSampler24AudioProcessorEditor::chooseLoadBank()
{
    chooser = std::make_unique<juce::FileChooser>("Load Bank", processor.getLibrary().banks(), "*.lsampler-24-b");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                juce::String error;
                const bool ok = processor.loadBankPreset(file, error);
                showResult(ok, error, "Bank loaded: " + file.getFileNameWithoutExtension());
                selectSlot(processor.getCurrentSlot(), false);
            }
        });
}

void LSampler24AudioProcessorEditor::chooseSaveBank()
{
    auto initial = processor.getLibrary().banks().getChildFile(LibraryManager::defaultName("Bank") + LibraryManager::bankExtension);
    chooser = std::make_unique<juce::FileChooser>("Save Bank", initial, "*.lsampler-24-b");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.getFullPathName().isNotEmpty())
            {
                if (!file.hasFileExtension(LibraryManager::bankExtension)) file = file.withFileExtension(LibraryManager::bankExtension);
                juce::String error;
                const bool ok = processor.saveBankPreset(file, error);
                showResult(ok, error, "Bank saved: " + file.getFileNameWithoutExtension());
            }
        });
}
