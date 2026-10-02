#include "PluginEditor.h"

namespace
{
bool hasAlt(const juce::KeyPress& key) { return key.getModifiers().isAltDown(); }
bool hasShift(const juce::KeyPress& key) { return key.getModifiers().isShiftDown(); }
}

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(760, 470);
    setWantsKeyboardFocus(true);

    for (int i = 0; i < LSampler24AudioProcessor::slotCount; ++i)
    {
        slotButtons[static_cast<size_t>(i)] = std::make_unique<SlotButton>(i);
        auto& b = *slotButtons[static_cast<size_t>(i)];
        b.onGridKey = [this](int slot, const juce::KeyPress& key) { return handleGridKey(slot, key); };
        b.onClick = [this, i] { selectSlot(i, false, true); };
        addAndMakeVisible(b);
    }

    auto addButton = [this](juce::TextButton& b)
    {
        b.setWantsKeyboardFocus(true);
        addAndMakeVisible(b);
    };
    addButton(loadSample);
    addButton(loadSlot);
    addButton(saveSlot);
    addButton(loadBank);
    addButton(saveBank);

    status.setAccessible(true);
    status.setTitle("Sample status");
    addAndMakeVisible(status);

    auto setupNoteSlider = [this](juce::Label& label, juce::Slider& slider, const juce::String& title)
    {
        label.setText(title, juce::dontSendNotification);
        addAndMakeVisible(label);
        slider.setRange(0, 127, 1);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 150, 24);
        slider.setTitle(title);
        slider.setWantsKeyboardFocus(true);
        slider.textFromValueFunction = [](double value)
        {
            return LSampler24AudioProcessor::noteName(static_cast<int>(std::round(value)));
        };
        addAndMakeVisible(slider);
    };

    setupNoteSlider(lowKeyLabel, lowKey, "Low Key");
    setupNoteSlider(highKeyLabel, highKey, "High Key");
    setupNoteSlider(originalPitchLabel, originalPitch, "Original Pitch");

    voiceModeLabel.setText("Voice Mode", juce::dontSendNotification);
    addAndMakeVisible(voiceModeLabel);
    voiceMode.addItem("Poly", 1);
    voiceMode.addItem("Mono", 2);
    voiceMode.setTitle("Voice Mode");
    voiceMode.setWantsKeyboardFocus(true);
    addAndMakeVisible(voiceMode);

    monoModeLabel.setText("Mono Mode", juce::dontSendNotification);
    addAndMakeVisible(monoModeLabel);
    monoMode.addItem("Trigger", 1);
    monoMode.addItem("Legato", 2);
    monoMode.setTitle("Mono Mode");
    monoMode.setWantsKeyboardFocus(true);
    addAndMakeVisible(monoMode);

    volumeLabel.setText("Volume", juce::dontSendNotification);
    addAndMakeVisible(volumeLabel);
    volume.setRange(0.0, 1.0, 0.01);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 24);
    volume.setTitle("Volume");
    volume.setWantsKeyboardFocus(true);
    addAndMakeVisible(volume);

    loadSample.onClick = [this] { chooseSample(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };

    lowKey.onValueChange = [this]
    {
        if (!updatingControls) processor.setLowKey(static_cast<int>(lowKey.getValue()));
    };
    highKey.onValueChange = [this]
    {
        if (!updatingControls) processor.setHighKey(static_cast<int>(highKey.getValue()));
    };
    originalPitch.onValueChange = [this]
    {
        if (!updatingControls) processor.setOriginalPitch(static_cast<int>(originalPitch.getValue()));
    };
    voiceMode.onChange = [this]
    {
        if (!updatingControls) processor.setVoiceMode(voiceMode.getSelectedId() - 1);
    };
    monoMode.onChange = [this]
    {
        if (!updatingControls) processor.setMonoMode(monoMode.getSelectedId() - 1);
    };
    volume.onValueChange = [this]
    {
        if (!updatingControls) processor.setVolume(static_cast<float>(volume.getValue()));
    };

    refreshSlotButtons();
    showSlotsPage(false);
    startTimerHz(5);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(20.0f);
    g.drawText(parametersPage ? "LSampler-24 - Slot Parameters" : "LSampler-24 - Slots",
               16, 12, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void LSampler24AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(42);

    if (!parametersPage)
    {
        auto grid = area.removeFromTop(300);
        constexpr int gap = 6;
        const int colWidth = (grid.getWidth() - gap * 2) / 3;
        const int rowHeight = (grid.getHeight() - gap * 7) / 8;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 8; ++row)
            {
                const int slot = col * 8 + row;
                slotButtons[static_cast<size_t>(slot)]->setBounds(
                    grid.getX() + col * (colWidth + gap),
                    grid.getY() + row * (rowHeight + gap), colWidth, rowHeight);
            }
        }

        area.removeFromTop(10);
        auto buttons = area.removeFromTop(34);
        const int w = (buttons.getWidth() - gap * 4) / 5;
        loadSample.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadBank.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveBank.setBounds(buttons);
        area.removeFromTop(8);
        status.setBounds(area.removeFromTop(30));
    }
    else
    {
        auto setRow = [&area](juce::Label& label, juce::Component& control)
        {
            auto row = area.removeFromTop(44);
            label.setBounds(row.removeFromLeft(150));
            control.setBounds(row);
            area.removeFromTop(6);
        };
        setRow(lowKeyLabel, lowKey);
        setRow(highKeyLabel, highKey);
        setRow(originalPitchLabel, originalPitch);
        setRow(voiceModeLabel, voiceMode);
        setRow(monoModeLabel, monoMode);
        setRow(volumeLabel, volume);
        area.removeFromTop(8);
        status.setBounds(area.removeFromTop(30));
    }
}

void LSampler24AudioProcessorEditor::updateParameterVisibility()
{
    const bool slotsVisible = !parametersPage;
    for (auto& button : slotButtons) button->setVisible(slotsVisible);
    loadSample.setVisible(slotsVisible);
    loadSlot.setVisible(slotsVisible);
    saveSlot.setVisible(slotsVisible);
    loadBank.setVisible(slotsVisible);
    saveBank.setVisible(slotsVisible);

    lowKeyLabel.setVisible(parametersPage); lowKey.setVisible(parametersPage);
    highKeyLabel.setVisible(parametersPage); highKey.setVisible(parametersPage);
    originalPitchLabel.setVisible(parametersPage); originalPitch.setVisible(parametersPage);
    voiceModeLabel.setVisible(parametersPage); voiceMode.setVisible(parametersPage);
    monoModeLabel.setVisible(parametersPage); monoMode.setVisible(parametersPage);
    volumeLabel.setVisible(parametersPage); volume.setVisible(parametersPage);
    resized();
    repaint();
}

void LSampler24AudioProcessorEditor::refreshSlotButtons()
{
    const int selected = processor.getCurrentSlot();
    for (int i = 0; i < LSampler24AudioProcessor::slotCount; ++i)
    {
        auto& button = *slotButtons[static_cast<size_t>(i)];
        const auto label = processor.getSlotLabel(i);
        button.setButtonText(label);
        button.setTitle(label);
        button.setWantsKeyboardFocus(i == selected);
    }
}

void LSampler24AudioProcessorEditor::refreshParameterControls()
{
    const auto s = processor.getSlotState(processor.getCurrentSlot());
    updatingControls = true;
    lowKey.setValue(s.lowKey, juce::dontSendNotification);
    highKey.setValue(s.highKey, juce::dontSendNotification);
    originalPitch.setValue(s.originalPitch, juce::dontSendNotification);
    voiceMode.setSelectedId(s.voiceMode + 1, juce::dontSendNotification);
    monoMode.setSelectedId(s.monoMode + 1, juce::dontSendNotification);
    volume.setValue(s.volume, juce::dontSendNotification);
    updatingControls = false;
}

void LSampler24AudioProcessorEditor::selectSlot(int index, bool takeFocus, bool enterParameters)
{
    processor.setCurrentSlot(index);
    refreshSlotButtons();
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    if (enterParameters)
    {
        showParametersPage();
        return;
    }
    if (takeFocus)
        slotButtons[static_cast<size_t>(processor.getCurrentSlot())]->grabKeyboardFocus();
}

bool LSampler24AudioProcessorEditor::handleGridKey(int slot, const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const int row = slot % 8;
    const int col = slot / 8;
    int target = slot;

    if (code == juce::KeyPress::upKey) target = col * 8 + juce::jmax(0, row - 1);
    else if (code == juce::KeyPress::downKey) target = col * 8 + juce::jmin(7, row + 1);
    else if (code == juce::KeyPress::leftKey) target = juce::jmax(0, col - 1) * 8 + row;
    else if (code == juce::KeyPress::rightKey) target = juce::jmin(2, col + 1) * 8 + row;
    else if (code == juce::KeyPress::homeKey && key.getModifiers().isCtrlDown()) target = 0;
    else if (code == juce::KeyPress::endKey && key.getModifiers().isCtrlDown()) target = 23;
    else if (code == juce::KeyPress::homeKey) target = col * 8;
    else if (code == juce::KeyPress::endKey) target = col * 8 + 7;
    else if (code == juce::KeyPress::pageUpKey) target = col * 8 + juce::jmax(0, row - 4);
    else if (code == juce::KeyPress::pageDownKey) target = col * 8 + juce::jmin(7, row + 4);
    else return keyPressed(key);

    if (target != slot)
        selectSlot(target, true, false);
    return true; // silent border: handled even when target did not change
}

void LSampler24AudioProcessorEditor::showSlotsPage(bool takeFocus)
{
    parametersPage = false;
    updateParameterVisibility();
    refreshSlotButtons();
    if (takeFocus)
        slotButtons[static_cast<size_t>(processor.getCurrentSlot())]->grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::showParametersPage()
{
    parametersPage = true;
    refreshParameterControls();
    updateParameterVisibility();
    lowKey.grabKeyboardFocus();
}

bool LSampler24AudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const auto ch = key.getTextCharacter();

    if (parametersPage && code == juce::KeyPress::escapeKey)
    {
        showSlotsPage(true);
        return true;
    }

    if (hasAlt(key))
    {
        if (!hasShift(key) && (ch == 'l' || ch == 'L')) { showSlotsPage(true); return true; }
        if (!hasShift(key) && (ch == 'o' || ch == 'O')) { chooseSample(); return true; }
        if (!hasShift(key) && (ch == 's' || ch == 'S')) { chooseLoadSlot(); return true; }
        if ( hasShift(key) && (ch == 's' || ch == 'S')) { chooseSaveSlot(); return true; }
        if (!hasShift(key) && (ch == 'b' || ch == 'B')) { chooseLoadBank(); return true; }
        if ( hasShift(key) && (ch == 'b' || ch == 'B')) { chooseSaveBank(); return true; }
    }
    return AudioProcessorEditor::keyPressed(key);
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    refreshSlotButtons();
    if (parametersPage)
        refreshParameterControls();
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotButtons();
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
