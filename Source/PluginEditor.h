#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "PluginProcessor.h"
#include "SliceEditor.h"
#include "RackGui.h"
#include <array>
#include <functional>
#include <vector>

class LSamplerSlotCell final : public juce::Component
{
public:
    std::function<void()> onActivate;

    void setSlotText(const juce::String& newText)
    {
        if (slotText == newText) return;
        slotText = newText;
        setTitle(slotText);
        setName({});
        setDescription({});
        repaint();
    }

    const juce::String& getSlotText() const noexcept { return slotText; }

    void setVisualState(int number, const juce::String& name, bool loaded, bool selected) {
        if (visualNumber == number && visualName == name && visualLoaded == loaded && visualSelected == selected) return;
        visualNumber=number; visualName=name; visualLoaded=loaded; visualSelected=selected; repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r=getLocalBounds().reduced(1);
        g.setColour(visualSelected?juce::Colour(0xff4b4034):rackgui::screen);g.fillRoundedRectangle(r.toFloat(),3);
        g.setColour(visualSelected||hasKeyboardFocus(true)?rackgui::amber:rackgui::muted);g.drawRoundedRectangle(r.toFloat().reduced(.5f),3,hasKeyboardFocus(true)?2.0f:1.0f);
        g.setColour(visualLoaded?rackgui::green:rackgui::muted.withAlpha(.4f));g.fillEllipse(float(r.getRight()-10),float(r.getY()+7),4,4);
        g.setColour(visualSelected?rackgui::amber:rackgui::text);g.setFont(13.0f);
        g.drawText(juce::String(visualNumber).paddedLeft('0',2),r.reduced(6).withHeight(18),juce::Justification::centredLeft);
        g.setColour(rackgui::muted);g.setFont(13.0f);g.drawText(visualName,r.reduced(6).withTrimmedTop(19),juce::Justification::centredLeft,true);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onActivate)
            onActivate();
        grabKeyboardFocus();
    }

private:
    juce::String slotText, visualName;
    int visualNumber=1;
    bool visualLoaded=false,visualSelected=false;
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


class LSamplerPropertiesPanel final : public juce::Component
{
public:
    LSamplerPropertiesPanel()
    {
        // Properties is a real accessible modal surface.  The panel itself is a
        // named container while the two read-only TextEditors are the keyboard
        // focus targets that NVDA can inspect line by line.
        setAccessible(true);
        setTitle("Properties");
        setDescription("Sample and slot configuration properties");
        setWantsKeyboardFocus(false);
        auto setup = [this](juce::TextEditor& editor, const juce::String& name)
        {
            editor.setMultiLine(true, true);
            editor.setReadOnly(true);
            editor.setScrollbarsShown(true);
            editor.setCaretVisible(false);
            editor.setWantsKeyboardFocus(true);
            editor.setName(name);
            editor.setTitle(name);
            editor.setDescription("Read-only properties");
            addAndMakeVisible(editor);
        };
        setup(sampleInfo, "Audio file information");
        setup(configInfo, "Slot configuration information");
    }

    void setInfo(const juce::String& sample, const juce::String& config)
    {
        sampleLines = juce::StringArray::fromLines(sample);
        configLines = juce::StringArray::fromLines(config);
        sampleInfo.setText(sample, false);
        configInfo.setText(config, false);
    }

    int lineCount(bool configColumn) const noexcept
    {
        return configColumn ? configLines.size() : sampleLines.size();
    }

    juce::String lineText(bool configColumn, int row) const
    {
        const auto& lines = configColumn ? configLines : sampleLines;
        if (lines.isEmpty()) return {};
        return lines[juce::jlimit(0, lines.size() - 1, row)];
    }

    void highlightLine(bool configColumn, int row)
    {
        auto& editor = configColumn ? configInfo : sampleInfo;
        const auto& lines = configColumn ? configLines : sampleLines;
        if (lines.isEmpty()) return;
        row = juce::jlimit(0, lines.size() - 1, row);
        int start = 0;
        for (int i = 0; i < row; ++i)
            start += lines[i].length() + 1;
        editor.setHighlightedRegion({ start, start + lines[row].length() });
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(rackgui::chassis);
        rackgui::frame(g, getLocalBounds().reduced(4), "PROPERTIES / Alt+Enter");
        auto r = getLocalBounds().reduced(18).withTrimmedTop(34);
        const int gap = 14;
        const int w = (r.getWidth() - gap) / 2;
        g.setColour(rackgui::amber); g.setFont(15.0f);
        g.drawText("AUDIO FILE", r.getX(), r.getY(), w, 22, juce::Justification::centredLeft);
        g.drawText("SLOT CONFIGURATION", r.getX() + w + gap, r.getY(), w, 22, juce::Justification::centredLeft);
        g.setColour(rackgui::muted); g.setFont(13.0f);
        g.drawText("Up/Down: Property   Left/Right or Tab: Column   Ctrl+Up/Down: Slot   Esc: Close", r.getX(), r.getBottom() - 22, r.getWidth(), 22, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(18).withTrimmedTop(60).withTrimmedBottom(30);
        const int gap = 14;
        const int w = (r.getWidth() - gap) / 2;
        sampleInfo.setBounds(r.removeFromLeft(w));
        r.removeFromLeft(gap);
        configInfo.setBounds(r.removeFromLeft(w));
    }

    juce::TextEditor sampleInfo, configInfo;
    juce::StringArray sampleLines, configLines;
};

class LSamplerAboutTextEditor final : public juce::TextEditor
{
public:
    using KeyHandler = std::function<bool(const juce::KeyPress&)>;

    explicit LSamplerAboutTextEditor(const juce::String& name)
        : juce::TextEditor(name) {}

    void setKeyHandler(KeyHandler handlerToUse) { keyHandler = std::move(handlerToUse); }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (keyHandler && keyHandler(key))
            return true;
        return juce::TextEditor::keyPressed(key);
    }

private:
    KeyHandler keyHandler;
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
    rackgui::Theme rackTheme;
    rackgui::Waveform waveform;
    rackgui::SlotOverview slotOverview;
    rackgui::CurrentParameter currentEdit;
    rackgui::SliceOverview sliceOverview;
    rackgui::MasterOutput masterOutput;
    LSamplerPropertiesPanel propertiesPanel;
    void refreshVisuals();
    std::unique_ptr<SliceEditor> sliceEditor;
    bool propertiesOpen = false;
    bool propertiesConfigColumn = false;
    int propertiesSampleRow = 0;
    int propertiesConfigRow = 0;
    // Runtime-only audition modes. On the slot page, Off is the default and
    // leaves Space to the DAW. In the main parameter grid the inverse default
    // is used: Space auditions the configured slot until Alt+P hands it back.
    bool slotPreviewMode = false;
    bool gridPreviewMode = true;
    bool editorFocusSeen = false;
    bool editorHadKeyboardFocus = false;
    juce::Component::SafePointer<juce::Component> propertiesReturnFocus;
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
    using FileTaskResult = LSampler24AudioProcessor::FileTaskResult;
    void runFileTask(const juce::String&, LSampler24AudioProcessor::FileTask,
                     std::function<void(const FileTaskResult&, bool)> completion = {});
    void setFileUiBusy(bool);
    void refreshMeters();
    bool fileUiBusy = false, fileTaskOwned = false;
    uint64_t lastUiRevision = 0;
    juce::Component::SafePointer<juce::Component> fileReturnFocus;
    std::vector<std::pair<juce::Component::SafePointer<juce::Component>, bool>> fileDisabledComponents;
    void chooseSample();
    void enterSampleSetEditor();
    void leaveSampleSetEditor();
    void refreshSampleSetCell(bool announce = false);
    void moveSampleSetEntry(int direction);
    void moveSampleSetField(int direction);
    void moveSampleSetGridColumn(int direction);
    void setSampleSetBoundary(bool maximum);
    void changeSampleSetValue(int direction, bool coarse = false);
    juce::String sampleSetParameterText() const;
    void previewSampleSetEntry();
    void enterSampleSetBrowser();
    void enterImportBrowser();
    void leaveImportBrowser(bool announceSlot, bool resetPreviewPosition = false);
    void refreshImportEntries();
    void selectImportEntry(int index, bool announce);
    void cycleImportEntryByInitial(juce::juce_wchar initial, int direction = 1);
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
    void enterSlotLibraryBrowser(bool forSampleSet = false, bool forBank = false);
    void leaveSlotLibraryBrowser(bool announceSlot);
    void refreshSlotLibraryEntries();
    void selectSlotLibraryEntry(int index, bool announce);
    void cycleSlotLibraryEntryByInitial(juce::juce_wchar initial, int direction = 1);
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
    void openProperties();
    void closeProperties();

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
    void showHelpLanguageMenu();
    void openHelp(const juce::String& languageCode);
    void openAbout();
    void closeAbout();
    void openProjectPage();
    void openContactEmail();
    bool navigateAboutText(const juce::KeyPress& key);
    void setMainControlsEnabled(bool enabled);
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
    juce::TextButton help { "Help" };
    juce::TextButton aboutButton { "About" };
    LSamplerAboutTextEditor aboutInfo { "About LSampler-24" };
    juce::TextButton aboutProject { "Project" };
    juce::TextButton aboutContact { "Contact" };
    juce::TextButton aboutClose { "Close" };
    bool aboutOpen = false;
    juce::Component::SafePointer<juce::Component> aboutReturnFocus;
    juce::Label status;

    LSamplerParameterComboBox parameterSelector;
    LSamplerValueSlider parameterValue;
    std::unique_ptr<juce::LookAndFeel_V4> valueLookAndFeel;
    std::unique_ptr<juce::LookAndFeel_V4> helpMenuLookAndFeel;

    std::unique_ptr<juce::FileChooser> chooser;

    LSamplerImportBrowserCell sampleSetCell;
    bool sampleSetActive = false;
    bool sampleSetReturnWasParameterPage = false;
    int sampleSetIndex = 0;
    int sampleSetField = 0; // Parameter grid: Velocity Low, Velocity High, Variation Mode
    bool sampleSetGridFocus = false;
    bool sampleSetValueFocus = false;
    juce::Component::SafePointer<juce::Component> sampleSetReturnFocus;
    bool importForSampleSet = false;
    int importSampleSetSlot = 0;
    int importSampleSetIndex = 0;

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
    bool slotLibraryForSampleSet = false;
    bool slotLibraryForBank = false;
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
