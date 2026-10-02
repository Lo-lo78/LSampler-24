#include "PluginEditor.h"

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(720, 300);

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

    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    status.setAccessible(true);
    status.setTitle("Sample status");
    addAndMakeVisible(status);

    rootLabel.setText("Root Note", juce::dontSendNotification);
    addAndMakeVisible(rootLabel);
    rootNote.setRange(0, 127, 1);
    rootNote.setValue(processor.getRootNote(), juce::dontSendNotification);
    rootNote.setSliderStyle(juce::Slider::LinearHorizontal);
    rootNote.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
    rootNote.setTitle("Root Note");
    rootNote.setWantsKeyboardFocus(true);
    addAndMakeVisible(rootNote);

    volumeLabel.setText("Volume", juce::dontSendNotification);
    addAndMakeVisible(volumeLabel);
    volume.setRange(0.0, 1.0, 0.01);
    volume.setValue(processor.getVolume(), juce::dontSendNotification);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
    volume.setTitle("Volume");
    volume.setWantsKeyboardFocus(true);
    addAndMakeVisible(volume);

    loadSample.onClick = [this] { chooseSample(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };
    rootNote.onValueChange = [this] { processor.setRootNote(static_cast<int>(rootNote.getValue())); };
    volume.onValueChange = [this] { processor.setVolume(static_cast<float>(volume.getValue())); };

    startTimerHz(5);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(20.0f);
    g.drawText("LSampler-24 TEST2", 16, 12, getWidth() - 32, 28, juce::Justification::centredLeft);
}

void LSampler24AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(42);
    auto buttons = area.removeFromTop(36);
    constexpr int gap = 6;
    const int w = (buttons.getWidth() - gap * 4) / 5;
    loadSample.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
    loadSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
    saveSlot.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
    loadBank.setBounds(buttons.removeFromLeft(w)); buttons.removeFromLeft(gap);
    saveBank.setBounds(buttons);

    area.removeFromTop(14);
    status.setBounds(area.removeFromTop(34));
    area.removeFromTop(10);

    auto row = area.removeFromTop(40);
    rootLabel.setBounds(row.removeFromLeft(110));
    rootNote.setBounds(row);
    area.removeFromTop(8);
    row = area.removeFromTop(40);
    volumeLabel.setBounds(row.removeFromLeft(110));
    volume.setBounds(row);
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    if (!rootNote.isMouseButtonDown())
        rootNote.setValue(processor.getRootNote(), juce::dontSendNotification);
    if (!volume.isMouseButtonDown())
        volume.setValue(processor.getVolume(), juce::dontSendNotification);
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
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
