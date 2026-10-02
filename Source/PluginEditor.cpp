#include "PluginEditor.h"
#include <cmath>

static void configureNoteSlider(juce::Slider& slider, const juce::String& title)
{
    slider.setRange(0, 127, 1);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 130, 24);
    slider.setTitle(title);
    slider.setWantsKeyboardFocus(true);
    slider.textFromValueFunction = [](double v)
    {
        const int n = juce::jlimit(0, 127, static_cast<int>(std::lround(v)));
        static const char* names[] = { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };
        const int octave = (n / 12) - 1;
        return juce::String(n) + " " + names[n % 12] + " " + juce::String(octave);
    };
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

    lowKeyLabel.setText("Low Key", juce::dontSendNotification);
    addAndMakeVisible(lowKeyLabel);
    configureNoteSlider(lowKey, "Low Key");
    lowKey.setValue(processor.getLowKey(), juce::dontSendNotification);
    lowKey.addKeyListener(this);
    addAndMakeVisible(lowKey);

    highKeyLabel.setText("High Key", juce::dontSendNotification);
    addAndMakeVisible(highKeyLabel);
    configureNoteSlider(highKey, "High Key");
    highKey.setValue(processor.getHighKey(), juce::dontSendNotification);
    highKey.addKeyListener(this);
    addAndMakeVisible(highKey);

    rootLabel.setText("Original Pitch", juce::dontSendNotification);
    addAndMakeVisible(rootLabel);
    configureNoteSlider(rootNote, "Original Pitch");
    rootNote.setValue(processor.getRootNote(), juce::dontSendNotification);
    rootNote.addKeyListener(this);
    addAndMakeVisible(rootNote);

    volumeLabel.setText("Volume", juce::dontSendNotification);
    addAndMakeVisible(volumeLabel);
    volume.setRange(0.0, 1.0, 0.01);
    volume.setValue(processor.getVolume(), juce::dontSendNotification);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
    volume.setTitle("Volume");
    volume.setWantsKeyboardFocus(true);
    volume.addKeyListener(this);
    addAndMakeVisible(volume);

    loadSample.onClick = [this] { chooseSample(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };
    lowKey.onValueChange = [this] { processor.setLowKey(static_cast<int>(lowKey.getValue())); };
    highKey.onValueChange = [this] { processor.setHighKey(static_cast<int>(highKey.getValue())); };
    rootNote.onValueChange = [this] { processor.setRootNote(static_cast<int>(rootNote.getValue())); };
    volume.onValueChange = [this] { processor.setVolume(static_cast<float>(volume.getValue())); };

    refreshSlotButtons();
    leaveSlotParameters();
    startTimerHz(5);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(20.0f);
    g.drawText("LSampler-24 TEST2 - 24 Slots Only", 16, 12, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void LSampler24AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(42);

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
    area.removeFromTop(6);

    auto row = area.removeFromTop(32);
    lowKeyLabel.setBounds(row.removeFromLeft(110)); lowKey.setBounds(row);
    area.removeFromTop(2);
    row = area.removeFromTop(32);
    highKeyLabel.setBounds(row.removeFromLeft(110)); highKey.setBounds(row);
    area.removeFromTop(2);
    row = area.removeFromTop(32);
    rootLabel.setBounds(row.removeFromLeft(110)); rootNote.setBounds(row);
    area.removeFromTop(2);
    row = area.removeFromTop(32);
    volumeLabel.setBounds(row.removeFromLeft(110)); volume.setBounds(row);
}

void LSampler24AudioProcessorEditor::selectSlot(int slotIndex, bool moveKeyboardFocus)
{
    slotIndex = juce::jlimit(0, LSampler24AudioProcessor::slotCount - 1, slotIndex);
    processor.setCurrentSlot(slotIndex);
    refreshSlotButtons();
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    refreshParameterValues();
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
        b.setWantsKeyboardFocus(i == selected);
    }
}

juce::String LSampler24AudioProcessorEditor::midiNoteText(int note)
{
    note = juce::jlimit(0, 127, note);
    static const char* names[] = { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };
    return juce::String(note) + " " + names[note % 12] + " " + juce::String((note / 12) - 1);
}

void LSampler24AudioProcessorEditor::refreshParameterValues()
{
    lowKey.setValue(processor.getLowKey(), juce::dontSendNotification);
    highKey.setValue(processor.getHighKey(), juce::dontSendNotification);
    rootNote.setValue(processor.getRootNote(), juce::dontSendNotification);
    volume.setValue(processor.getVolume(), juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::enterSlotParameters()
{
    parameterPage = true;
    for (auto& b : slotButtons) b.setWantsKeyboardFocus(false);
    loadSample.setWantsKeyboardFocus(false);
    loadSlot.setWantsKeyboardFocus(false);
    saveSlot.setWantsKeyboardFocus(false);
    loadBank.setWantsKeyboardFocus(false);
    saveBank.setWantsKeyboardFocus(false);
    refreshParameterValues();
    lowKeyLabel.setVisible(true); lowKey.setVisible(true);
    highKeyLabel.setVisible(true); highKey.setVisible(true);
    rootLabel.setVisible(true); rootNote.setVisible(true);
    volumeLabel.setVisible(true); volume.setVisible(true);
    lowKey.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveSlotParameters()
{
    parameterPage = false;
    loadSample.setWantsKeyboardFocus(true);
    loadSlot.setWantsKeyboardFocus(true);
    saveSlot.setWantsKeyboardFocus(true);
    loadBank.setWantsKeyboardFocus(true);
    saveBank.setWantsKeyboardFocus(true);
    refreshSlotButtons();
    lowKeyLabel.setVisible(false); lowKey.setVisible(false);
    highKeyLabel.setVisible(false); highKey.setVisible(false);
    rootLabel.setVisible(false); rootNote.setVisible(false);
    volumeLabel.setVisible(false); volume.setVisible(false);
}

bool LSampler24AudioProcessorEditor::isActionButton(const juce::Component* component) const
{
    return component == &loadSample || component == &loadSlot || component == &saveSlot
        || component == &loadBank || component == &saveBank;
}

bool LSampler24AudioProcessorEditor::isSlotButton(const juce::Component* component) const
{
    for (const auto& b : slotButtons)
        if (component == &b)
            return true;
    return false;
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
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    // Global accessible shortcuts. Load uses the plain Alt letter; Save uses
    // the same letter with Shift so Slot and Bank are symmetrical.
    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'l' && !mods.isShiftDown())
        {
            if (parameterPage) leaveSlotParameters();
            selectSlot(processor.getCurrentSlot(), true);
            return true;
        }
        if (ch == 'o' && !mods.isShiftDown())
        {
            chooseSample();
            return true;
        }
        if (ch == 's')
        {
            if (mods.isShiftDown()) chooseSaveSlot();
            else                    chooseLoadSlot();
            return true;
        }
        if (ch == 'b')
        {
            if (mods.isShiftDown()) chooseSaveBank();
            else                    chooseLoadBank();
            return true;
        }
    }

    if (parameterPage)
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            leaveSlotParameters();
            selectSlot(processor.getCurrentSlot(), true);
            return true;
        }
        // Parameter controls keep their native arrow/value behaviour.
        return false;
    }

    // The five action buttons must not hand arrow keys to JUCE's focus
    // traversal or to the host. At any edge, an arrow is simply silent.
    if (isActionButton(source))
    {
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
            || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
            return true;
        return false;
    }

    if (!isSlotButton(source))
        return false;

    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        enterSlotParameters();
        return true;
    }

    const int slot = processor.getCurrentSlot();
    const int row = slot % 8;
    const int col = slot / 8;

    // The slot grid owns all navigation keys, including at its borders.
    if (key.getKeyCode() == juce::KeyPress::upKey)
    {
        if (row > 0) selectSlot(slot - 1, true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        if (row < 7) selectSlot(slot + 1, true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        if (col > 0) selectSlot(slot - 8, true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        if (col < 2) selectSlot(slot + 8, true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::homeKey)
    {
        selectSlot(col * 8, true);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::endKey)
    {
        selectSlot(col * 8 + 7, true);
        return true;
    }

    return false;
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    refreshSlotButtons();
    if (parameterPage)
    {
        if (!lowKey.isMouseButtonDown()) lowKey.setValue(processor.getLowKey(), juce::dontSendNotification);
        if (!highKey.isMouseButtonDown()) highKey.setValue(processor.getHighKey(), juce::dontSendNotification);
        if (!rootNote.isMouseButtonDown()) rootNote.setValue(processor.getRootNote(), juce::dontSendNotification);
        if (!volume.isMouseButtonDown()) volume.setValue(processor.getVolume(), juce::dontSendNotification);
    }
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
