#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "PluginProcessor.h"
#include "SliceEditor.h"
#include <array>
#include <functional>
#include <vector>

class LSamplerSlotCell final : public juce::Component
{
public:
    std::function<void()> onActivate;

    void setSlotText(const juce::String& newText)
    {
        slotText = newText;
        setTitle(slotText);
        setName({});
        setDescription({});
        repaint();
    }

    const juce::String& getSlotText() const noexcept { return slotText; }

    void paint(juce::Graphics& g) override
    {
        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(15.0f);
        g.drawText(slotText, getLocalBounds().reduced(4, 0), juce::Justification::centredLeft, true);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onActivate)
            onActivate();
        grabKeyboardFocus();
    }

private:
    juce::String slotText;
};

class LSamplerParameterComboBox final : public juce::ComboBox
{
public:
    using Shortcut = std::function<bool(const juce::KeyPress&, juce::Component*)>;
    void setShortcutHandler(Shortcut handler) { shortcut = std::move(handler); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        const bool dedicated = code==juce::KeyPress::F6Key || (mods.isAltDown() && ch=='e') || (mods.isCtrlDown() && !mods.isAltDown()
                                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                                    || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
                            || (mods.isAltDown() && !mods.isCtrlDown() && ch == 'l');
        if (dedicated && shortcut && shortcut(key, this))
            return true;
        return juce::ComboBox::keyPressed(key);
    }

    void setLineReadingMode()
    {
        setTitle({});
        setDescription({});
    }

    void setEntryAccessibility()
    {
        setTitle("Grid");
        setDescription({});
    }

    void focusGained(FocusChangeType cause) override
    {
        setEntryAccessibility();
        juce::ComboBox::focusGained(cause);
        juce::Timer::callAfterDelay(1,
            [safeThis = juce::Component::SafePointer<LSamplerParameterComboBox>(this)]
            {
                if (safeThis != nullptr && safeThis->hasKeyboardFocus(true))
                    if (auto* handler = safeThis->getAccessibilityHandler())
                        handler->grabFocus();
            });
        juce::Timer::callAfterDelay(220,
            [safeThis = juce::Component::SafePointer<LSamplerParameterComboBox>(this)]
            {
                if (safeThis != nullptr)
                    safeThis->setLineReadingMode();
            });
    }


private:
    Shortcut shortcut;
};

class LSamplerValueSlider final : public juce::Slider
{
public:
    using Shortcut = std::function<bool(const juce::KeyPress&, juce::Component*)>;
    void setShortcutHandler(Shortcut handler) { shortcut = std::move(handler); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        const bool dedicated = code==juce::KeyPress::F6Key || (mods.isAltDown() && ch=='e') || (mods.isCtrlDown() && !mods.isAltDown()
                                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                                    || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
                            || (mods.isAltDown() && !mods.isCtrlDown() && ch == 'l');
        if (dedicated && shortcut && shortcut(key, this))
            return true;
        return juce::Slider::keyPressed(key);
    }

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
        juce::Timer::callAfterDelay(1,
            [safeThis = juce::Component::SafePointer<LSamplerValueSlider>(this)]
            {
                if (safeThis != nullptr && safeThis->hasKeyboardFocus(true))
                    if (auto* handler = safeThis->getAccessibilityHandler())
                        handler->grabFocus();
            });
        juce::Timer::callAfterDelay(220,
            [safeThis = juce::Component::SafePointer<LSamplerValueSlider>(this)]
            {
                if (safeThis != nullptr)
                    safeThis->setLineReadingMode();
            });
    }

private:
    juce::String parameterName;
    Shortcut shortcut;
};


class LSamplerImportBrowserCell final : public juce::Component
{
public:
    void setBrowserText(const juce::String& t) { text = t; setTitle(text); setName({}); setDescription({}); repaint(); }
    const juce::String& getBrowserText() const noexcept { return text; }
    void paint(juce::Graphics& g) override
    {
        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(16.0f);
        g.drawText(text, getLocalBounds().reduced(6), juce::Justification::centredLeft, true);
    }
private:
    juce::String text;
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
    bool keyStateChanged(bool) override { return sliceEditor != nullptr; }
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

private:
    std::unique_ptr<SliceEditor> sliceEditor;
    juce::Component::SafePointer<juce::Component> sliceReturnFocus;
    void openSliceEditor(bool sequencer);
    void closeSliceEditor();
    void openValueEditor();
    const lsampler::GridEntry& selectedEntry() const;
    bool selectedRateIsSynced() const;
    juce::String formatParameter(int index, double value) const;
    int categoryBegin(int index) const;
    int categoryEnd(int index) const;

    void timerCallback() override;
    void chooseSample();
    void enterImportBrowser();
    void leaveImportBrowser(bool announceSlot, bool resetPreviewPosition = false);
    void refreshImportEntries();
    void selectImportEntry(int index, bool announce);
    void cycleImportEntryByInitial(juce::juce_wchar initial);
    void moveImportDestinationSlot(int direction);
    void shiftSelectImportEntry(int direction);
    void moveToImportFileWithSlice(int direction);
    void enterImportRecentPaths();
    void leaveImportRecentPaths();
    void removeCurrentImportRecentPath();
    void addImportRecentPath(const juce::File& directory);
    void loadImportSettings();
    void saveImportSettings(bool resetPreviewPosition = false);
    void saveImportPreviewPreferenceOnly();
    void saveSlotLibraryNavigationState();
    juce::File importSettingsFile() const;
    void updateImportPreviewForSelection();
    void announceImportEntry();
    void prepareImportPreviewForCurrent();
    void seekImportPreview(double deltaSeconds);
    void markImportSliceStart();
    void markImportSliceEnd();
    void toggleImportSliceEndMode();
    void toggleImportFileLoop();
    void navigateImportSlice(int direction);
    void seekOutsideImportSlice(int direction);
    void markImportPlayStart();
    void toggleImportPreviewPlayPause();
    void applyImportSliceLoop(int planIndex, bool startPlayback);
    void toggleImportFileSelection();
    void deleteImportPlanItemAtCursor();
    void commitImportPlan();
    int nextImportFreeSlot() const;
    bool importSlotReserved(int slot) const;
    void chooseImportFiles();
    void chooseImportFolder();
    void chooseExportLibrary();
    void chooseImportLibrary();
    void enterSlotLibraryBrowser();
    void leaveSlotLibraryBrowser(bool announceSlot);
    void refreshSlotLibraryEntries();
    void selectSlotLibraryEntry(int index, bool announce);
    void cycleSlotLibraryEntryByInitial(juce::juce_wchar initial);
    void announceSlotLibraryEntry();
    void updateSlotLibraryPreviewForSelection();
    void toggleSlotLibrarySelection();
    void shiftSelectSlotLibraryEntry(int direction);
    void moveSlotLibraryDestination(int direction);
    void commitSlotLibrarySelection();
    void copySlotLibraryEntry(bool cut);
    void pasteSlotLibraryEntry();
    int nextSlotLibraryFreeSlot(int from) const;
    bool slotLibraryDestinationReserved(int slot) const;
    void chooseLoadSlot();
    void chooseSaveSlot();
    void renameCurrentSlot();
    void chooseLoadBank();
    void chooseSaveBank();
    void showResult(bool ok, const juce::String& error, const juce::String& okMessage);
    void selectSlot(int slotIndex, bool moveKeyboardFocus);
    void refreshSlotCells();
    void returnToCurrentSlotAndAnnounce();

    void enterSlotParameters();
    void leaveSlotParameters();
    void openGlobal();
    void closeGlobal(bool accept);
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
    juce::ComboBox importSourceCombo;
    juce::TextButton exportLibraryButton { "Export Library" };
    juce::TextButton importLibraryButton { "Import Library" };
    juce::TextButton loadSlot { "Load Slot" };
    juce::TextButton saveSlot { "Save Slot" };
    juce::TextButton loadBank { "Load Bank" };
    juce::TextButton saveBank { "Save Bank" };
    juce::Label status;

    LSamplerParameterComboBox parameterSelector;
    LSamplerValueSlider parameterValue;
    std::unique_ptr<juce::LookAndFeel_V4> valueLookAndFeel;

    std::unique_ptr<juce::FileChooser> chooser;

    struct ImportEntry { juce::File file; bool directory = false; };
    struct ImportPlanItem { juce::File file; int slot = -1; bool slice = false; double start = 0.0, end = 0.0; };
    LSamplerImportBrowserCell importBrowserCell;
    juce::File importDirectory;
    std::vector<ImportEntry> importEntries;
    std::vector<ImportPlanItem> importPlan;
    int importEntryIndex = 0;
    int importStartSlot = 0;
    juce::File importPreviewFile;
    bool importSlicePending = false;
    juce::File importSliceFile;
    double importSliceStart = 0.0;
    int importLastSlicePlanIndex = -1;
    int importCurrentSlicePlanIndex = -1;
    bool importSliceEndRestarts = true;
    bool importFileLoopEnabled = false;
    juce::File importPlayStartFile;
    double importPlayStartSeconds = 0.0;
    bool importBrowserActive = false;
    bool importPreviewEnabled = false;
    bool importDriveList = false;
    bool importRecentPathsMode = false;
    std::vector<juce::File> importRecentPaths;
    int importRecentIndex = 0;
    bool importShiftSelectionActive = false;
    juce::juce_wchar importLastInitial = 0;
    juce::String importRememberedEntryPath;
    double importRememberedPosition = 0.0;
    std::map<juce::String, juce::String> importDirectorySelectionMemory;


    struct SlotLibraryEntry { juce::File file; bool directory = false; };
    struct SlotLibrarySelection { juce::File file; int slot = -1; };
    LSamplerImportBrowserCell slotLibraryCell;
    juce::File slotLibraryRoot;
    juce::File slotLibraryDirectory;
    std::vector<SlotLibraryEntry> slotLibraryEntries;
    std::vector<SlotLibrarySelection> slotLibrarySelection;
    int slotLibraryEntryIndex = 0;
    int slotLibraryStartSlot = 0;
    bool slotLibraryActive = false;
    bool slotLibraryPreviewEnabled = false;
    bool slotLibraryShiftSelectionActive = false;
    juce::juce_wchar slotLibraryLastInitial = 0;
    std::map<juce::String, juce::String> slotLibraryDirectorySelectionMemory;
    juce::File slotLibraryPendingPreview;
    int slotLibraryPreviewDelayTicks = 0;
    juce::File slotLibraryClipboardFile;
    bool slotLibraryClipboardCut = false;

    bool parameterPage = false;
    bool globalOpen = false;
    bool globalReturnWasParameterPage = false;
    int globalReturnSelectedParameter = 0;
    int globalGridIndex = 0;
    std::array<double, lsampler::globalParameterCount> globalSnapshot {};
    int selectedParameter = 0;
    int stepWidthIndex = 0;
    int selectedLoop = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LSampler24AudioProcessorEditor)
};
