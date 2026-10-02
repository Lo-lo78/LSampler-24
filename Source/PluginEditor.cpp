#include "PluginEditor.h"

namespace
{
constexpr int rowsPerColumn = 8;
constexpr int columns = 3;

juce::String luaNoteName(double value)
{
    const int note = juce::jlimit(0, 127, juce::roundToInt(value));
    static constexpr const char* names[] =
        { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };
    const int octave = (note / 12) - 1;
    return juce::String(note) + " " + names[note % 12] + " " + juce::String(octave);
}

void setupNoteSlider(juce::Slider& slider, const juce::String& title)
{
    slider.setRange(0, 127, 1);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 150, 24);
    slider.setTitle(title);
    slider.setAccessible(true);
    slider.setWantsKeyboardFocus(true);
    slider.textFromValueFunction = [](double v) { return luaNoteName(v); };
    slider.valueFromTextFunction = [](const juce::String& text)
    {
        return (double) juce::jlimit(0, 127, text.getIntValue());
    };
}
}

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(760, 520);
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    for (int i = 0; i < LSampler24AudioProcessor::slotCount; ++i)
    {
        auto button = std::make_unique<juce::TextButton>();
        button->setAccessible(true);
        button->setWantsKeyboardFocus(false);
        addKeyTarget(*button);
        const int slot = i + 1;
        button->onClick = [this, slot]
        {
            setCurrentSlot(slot, false);
            showParametersView();
        };
        addAndMakeVisible(*button);
        slotButtons[(size_t)i] = std::move(button);
    }

    auto setupButton = [this](juce::TextButton& button)
    {
        button.setWantsKeyboardFocus(true);
        button.setAccessible(true);
        addKeyTarget(button);
        addAndMakeVisible(button);
    };
    setupButton(loadSample);
    setupButton(loadSlot);
    setupButton(saveSlot);
    setupButton(loadBank);
    setupButton(saveBank);

    status.setAccessible(true);
    status.setTitle("Sample status");
    addAndMakeVisible(status);

    voiceMode.addItem("Mono", 1);
    voiceMode.addItem("Poly", 2);
    voiceMode.setTitle("Voice Mode");
    voiceMode.setAccessible(true);
    voiceMode.setWantsKeyboardFocus(true);
    addKeyTarget(voiceMode);
    addAndMakeVisible(voiceMode);

    monoMode.addItem("Trigger", 1);
    monoMode.addItem("Legato", 2);
    monoMode.setTitle("Mono Mode");
    monoMode.setAccessible(true);
    monoMode.setWantsKeyboardFocus(true);
    addKeyTarget(monoMode);
    addAndMakeVisible(monoMode);

    setupNoteSlider(lowKey, "Low Key");
    addKeyTarget(lowKey);
    addAndMakeVisible(lowKey);

    setupNoteSlider(highKey, "High Key");
    addKeyTarget(highKey);
    addAndMakeVisible(highKey);

    setupNoteSlider(originalNote, "Original Pitch");
    addKeyTarget(originalNote);
    addAndMakeVisible(originalNote);

    // Keep keyboard focus order identical to the slot parameter grid.
    lowKey.setExplicitFocusOrder(1);
    highKey.setExplicitFocusOrder(2);
    originalNote.setExplicitFocusOrder(3);
    voiceMode.setExplicitFocusOrder(4);
    monoMode.setExplicitFocusOrder(5);

    volume.setRange(0.0, 1.0, 0.01);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    volume.setTitle("Volume");
    volume.setAccessible(true);
    volume.setWantsKeyboardFocus(true);
    volume.setExplicitFocusOrder(6);
    addKeyTarget(volume);
    addAndMakeVisible(volume);

    loadSample.onClick = [this] { chooseSample(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };

    voiceMode.onChange = [this]
    {
        processor.setVoiceMode(voiceMode.getSelectedId() == 1
            ? LSampler24AudioProcessor::VoiceMode::Mono
            : LSampler24AudioProcessor::VoiceMode::Poly);
    };
    monoMode.onChange = [this]
    {
        processor.setMonoMode(monoMode.getSelectedId() == 2
            ? LSampler24AudioProcessor::MonoMode::Legato
            : LSampler24AudioProcessor::MonoMode::Trigger);
    };
    lowKey.onValueChange = [this] { processor.setLowKey((int) lowKey.getValue()); };
    highKey.onValueChange = [this] { processor.setHighKey((int) highKey.getValue()); };
    originalNote.onValueChange = [this] { processor.setOriginalNote((int) originalNote.getValue()); };
    volume.onValueChange = [this] { processor.setVolume((float) volume.getValue()); };

    refreshSlotButtons();
    showSlotsView(false);
    setCurrentSlot(processor.getCurrentSlot(), false);
    startTimerHz(5);
}

void LSampler24AudioProcessorEditor::addKeyTarget(juce::Component& component)
{
    component.addKeyListener(this);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(20.0f);
    const auto title = parametersView
        ? "LSampler-24 TEST4 - Slot " + juce::String(processor.getCurrentSlot()) + " Parameters"
        : "LSampler-24 TEST4 - Slots";
    g.drawText(title, 16, 12, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void LSampler24AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(44);

    if (!parametersView)
    {
        auto grid = area.removeFromTop(8 * 38);
        const int cellW = (grid.getWidth() - 16) / 3;
        for (int col = 0; col < columns; ++col)
            for (int row = 0; row < rowsPerColumn; ++row)
            {
                const int index = col * rowsPerColumn + row;
                slotButtons[(size_t)index]->setBounds(grid.getX() + col * (cellW + 8),
                                                      grid.getY() + row * 38,
                                                      cellW, 32);
            }

        area.removeFromTop(12);
        auto buttons = area.removeFromTop(36);
        constexpr int gap = 6;
        const int w = (buttons.getWidth() - gap * 4) / 5;
        loadSample.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        loadBank.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
        saveBank.setBounds(buttons);
        area.removeFromTop(10);
        status.setBounds(area.removeFromTop(34));
    }
    else
    {
        auto row = area.removeFromTop(48); lowKey.setBounds(row.reduced(0, 5));
        row = area.removeFromTop(48); highKey.setBounds(row.reduced(0, 5));
        row = area.removeFromTop(48); originalNote.setBounds(row.reduced(0, 5));
        row = area.removeFromTop(48); voiceMode.setBounds(row.reduced(0, 5));
        row = area.removeFromTop(48); monoMode.setBounds(row.reduced(0, 5));
        row = area.removeFromTop(48); volume.setBounds(row.reduced(0, 5));
        area.removeFromTop(12);
        status.setBounds(area.removeFromTop(34));
    }
}

void LSampler24AudioProcessorEditor::refreshSlotButtons()
{
    for (int i = 0; i < LSampler24AudioProcessor::slotCount; ++i)
    {
        auto text = processor.getSlotLabel(i + 1);
        slotButtons[(size_t)i]->setButtonText(text);
        slotButtons[(size_t)i]->setTitle(text);
    }
}

void LSampler24AudioProcessorEditor::refreshParameterControls()
{
    const auto state = processor.getSlotState(processor.getCurrentSlot());
    lowKey.setValue(state.lowKey, juce::dontSendNotification);
    highKey.setValue(state.highKey, juce::dontSendNotification);
    originalNote.setValue(state.originalNote, juce::dontSendNotification);
    voiceMode.setSelectedId(state.voiceMode == LSampler24AudioProcessor::VoiceMode::Mono ? 1 : 2, juce::dontSendNotification);
    monoMode.setSelectedId(state.monoMode == LSampler24AudioProcessor::MonoMode::Legato ? 2 : 1, juce::dontSendNotification);
    volume.setValue(state.volume, juce::dontSendNotification);
    status.setText(state.status, juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::setCurrentSlot(int slot, bool focusIt)
{
    slot = juce::jlimit(1, LSampler24AudioProcessor::slotCount, slot);
    const int old = processor.getCurrentSlot();
    if (old >= 1 && old <= LSampler24AudioProcessor::slotCount)
        slotButtons[(size_t)(old - 1)]->setWantsKeyboardFocus(false);
    processor.setCurrentSlot(slot);
    auto& target = *slotButtons[(size_t)(slot - 1)];
    target.setWantsKeyboardFocus(true);
    refreshParameterControls();
    if (focusIt)
        target.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::moveSlot(int rowDelta, int colDelta)
{
    const int slot = processor.getCurrentSlot();
    const int index = slot - 1;
    const int col = index / rowsPerColumn;
    const int row = index % rowsPerColumn;
    const int newCol = juce::jlimit(0, columns - 1, col + colDelta);
    const int newRow = juce::jlimit(0, rowsPerColumn - 1, row + rowDelta);
    const int newSlot = newCol * rowsPerColumn + newRow + 1;
    if (newSlot != slot)
        setCurrentSlot(newSlot, true); // silent borders: no focus change at an edge.
}

void LSampler24AudioProcessorEditor::showSlotsView(bool focusGrid)
{
    parametersView = false;
    for (auto& b : slotButtons) b->setVisible(true);
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
    lowKey.setVisible(false); highKey.setVisible(false); originalNote.setVisible(false);
    voiceMode.setVisible(false); monoMode.setVisible(false); volume.setVisible(false);
    status.setVisible(true);
    resized(); repaint();
    if (focusGrid) setCurrentSlot(processor.getCurrentSlot(), true);
}

void LSampler24AudioProcessorEditor::showParametersView()
{
    parametersView = true;
    for (auto& b : slotButtons) b->setVisible(false);
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    lowKey.setVisible(true); highKey.setVisible(true); originalNote.setVisible(true);
    voiceMode.setVisible(true); monoMode.setVisible(true); volume.setVisible(true);
    refreshParameterControls();
    resized(); repaint();
    focusParameter(0);
}

void LSampler24AudioProcessorEditor::focusParameter(int index)
{
    parameterFocus = juce::jlimit(0, 5, index);
    std::array<juce::Component*, 6> items { &lowKey, &highKey, &originalNote, &voiceMode, &monoMode, &volume };
    items[(size_t)parameterFocus]->grabKeyboardFocus();
}

bool LSampler24AudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (mods.isAltDown() && !mods.isCtrlDown())
    {
        if (ch == 'l') { showSlotsView(true); return true; }
        if (ch == 'o' && !mods.isShiftDown()) { chooseSample(); return true; }
        if (ch == 's') { if (mods.isShiftDown()) chooseSaveSlot(); else chooseLoadSlot(); return true; }
        if (ch == 'b') { if (mods.isShiftDown()) chooseSaveBank(); else chooseLoadBank(); return true; }
    }

    if (parametersView)
    {
        if (key == juce::KeyPress::escapeKey) { showSlotsView(true); return true; }
        if (key == juce::KeyPress::upKey) { focusParameter(parameterFocus - 1); return true; }
        if (key == juce::KeyPress::downKey) { focusParameter(parameterFocus + 1); return true; }
        if (key == juce::KeyPress::homeKey) { focusParameter(0); return true; }
        if (key == juce::KeyPress::endKey) { focusParameter(5); return true; }
        return false; // Left/Right remain native value editing.
    }

    if (key == juce::KeyPress::upKey) { moveSlot(-1, 0); return true; }
    if (key == juce::KeyPress::downKey) { moveSlot(1, 0); return true; }
    if (key == juce::KeyPress::leftKey) { moveSlot(0, -1); return true; }
    if (key == juce::KeyPress::rightKey) { moveSlot(0, 1); return true; }

    const int slot = processor.getCurrentSlot();
    const int index = slot - 1;
    const int col = index / rowsPerColumn;
    const int row = index % rowsPerColumn;
    if (key == juce::KeyPress::homeKey && mods.isCtrlDown()) { setCurrentSlot(1, true); return true; }
    if (key == juce::KeyPress::endKey && mods.isCtrlDown()) { setCurrentSlot(24, true); return true; }
    if (key == juce::KeyPress::homeKey) { setCurrentSlot(col * rowsPerColumn + 1, true); return true; }
    if (key == juce::KeyPress::endKey) { setCurrentSlot(col * rowsPerColumn + 8, true); return true; }
    if (key == juce::KeyPress::pageUpKey) { setCurrentSlot(col * rowsPerColumn + juce::jlimit(0, 7, row - 4) + 1, true); return true; }
    if (key == juce::KeyPress::pageDownKey) { setCurrentSlot(col * rowsPerColumn + juce::jlimit(0, 7, row + 4) + 1, true); return true; }

    return false;
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    refreshSlotButtons();
    if (parametersView) refreshParameterControls();
    else status.setText(processor.getSampleStatus(), juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotButtons();
    refreshParameterControls();
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
                setCurrentSlot(processor.getCurrentSlot(), false);
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
