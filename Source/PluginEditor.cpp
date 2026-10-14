#include "PluginEditor.h"
#include "ParameterSearchKey.h"
#include <limits>
#include <algorithm>
#include "ScreenReaderAnnouncer.h"
#include <BinaryData.h>
#include <cmath>
#include <utility>
#include <cstring>
using namespace lsampler;

namespace
{
constexpr std::array<int, 5> stepWidths { 1, 5, 10, 15, 20 };
constexpr int valuePageStep = 40;
constexpr auto lsamplerVersion = "0.99.11";
constexpr auto lsamplerReleaseDate = "7 October 2026";
constexpr auto lsamplerProjectUrl = "https://github.com/Lo-lo78/LSampler-24";
constexpr auto lsamplerContactEmail = "vmanolo301@gmail.com";

// TEST129: Keyboard initial search must use the SAME accessible parameter
// name as the parameter grid, not the immutable host parameter descriptor.
// For example the host ID/name 'Global One Shot' is shown to the musician
// as 'Main Playback Mode' and must be reached with M, never G.
juce::String displayedGridParameterName(const GridEntry& entry)
{
    return entry.parameter == int(P::global_one_shot)
        ? juce::String("Main Playback Mode")
        : juce::String(descriptor(entry).name);
}

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
             || key.getKeyCode() == juce::KeyPress::tabKey
             || key.getKeyCode() == juce::KeyPress::spaceKey)
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

// TEST124: Give every name-entry dialog ONE stable keyboard/accessibility
// target from its first appearance. AlertWindow's addButton() gives buttons
// focus order 1, which otherwise makes OK receive the initial focus instead
// of the editable field and creates two competing NVDA focus announcements.
// Escape/Return are handled directly by the focused TextEditor: relying on
// propagation to AlertWindow is inconsistent in some DAW plug-in hosts.
void prepareNameEntryDialog(juce::AlertWindow& dialog,
                            const juce::String& editorName,
                            const juce::String& accessibleFieldLabel)
{
    dialog.setAccessible(true);
    // TEST125: AlertWindow exposes its title/message through BOTH its own
    // description and an extra internal accessibility Label.  NVDA reads
    // that Label along with the dialog and the initially focused editor,
    // duplicating the prompt and sometimes the preset name.  All name
    // dialogs have a short descriptive title; their visible label is not a
    // separate screen-reader instruction.  Hide JUCE's internal Label(s)
    // from the accessibility tree, but preserve their visual rendering.
    dialog.setDescription(juce::String());
    for (auto* child : dialog.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            label->setAccessible(false);
    if (auto* editor = dialog.getTextEditor(editorName))
    {
        editor->setTitle(accessibleFieldLabel);
        editor->setWantsKeyboardFocus(true);
        editor->setExplicitFocusOrder(1);
        editor->setSelectAllWhenFocused(true);
        editor->setEscapeAndReturnKeysConsumed(true);
        const juce::Component::SafePointer<juce::AlertWindow> safeDialog(&dialog);
        editor->onEscapeKey = [safeDialog]
        {
            if (safeDialog != nullptr) safeDialog->exitModalState(0);
        };
        editor->onReturnKey = [safeDialog]
        {
            if (safeDialog != nullptr) safeDialog->exitModalState(1);
        };
    }
    if (auto* ok = dialog.getButton("OK"))
    {
        ok->setExplicitFocusOrder(2);
        ok->setTitle("OK");
        ok->setDescription(juce::String());
    }
    if (auto* cancel = dialog.getButton("Cancel"))
    {
        cancel->setExplicitFocusOrder(3);
        cancel->setTitle("Cancel");
        cancel->setDescription(juce::String());
    }
    // No grabKeyboardFocus() here or after enterModalState(): the modal's
    // single initial grab selects the text editor by focus order, avoiding
    // a transient announcement of the OK button followed by the editor.
}

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
        editor->setInputRestrictions(0, "-0123456789.");
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
        : shortcut(std::move(shortcutToUse)) { rackgui::Theme::apply(*this); }

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

class HelpMenuLookAndFeel final : public rackgui::Theme
{
public:
    void preparePopupMenuWindow(juce::Component& window) override
    {
        juce::LookAndFeel_V4::preparePopupMenuWindow(window);
        window.setName("Choose Help language");
        window.setTitle("Choose Help language");
    }
};

class AdvancedMenuLookAndFeel final : public rackgui::Theme
{
public:
    void preparePopupMenuWindow(juce::Component& window) override
    {
        juce::LookAndFeel_V4::preparePopupMenuWindow(window);
        window.setName("Advanced");
        window.setTitle("Advanced");
    }
};
}

LSampler24AudioProcessorEditor::LSampler24AudioProcessorEditor(LSampler24AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&rackTheme);
    helpMenuLookAndFeel = std::make_unique<HelpMenuLookAndFeel>();
    advancedMenuLookAndFeel = std::make_unique<AdvancedMenuLookAndFeel>();
    for (auto* display : std::array<juce::Component*,5>{ &waveform, &slotOverview, &currentEdit, &sliceOverview, &masterOutput })
        addAndMakeVisible(*display);
    addChildComponent(propertiesPanel);
    propertiesPanel.sampleInfo.addKeyListener(this);
    propertiesPanel.configInfo.addKeyListener(this);
    setSize(1120, 800);

    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[static_cast<size_t>(i)];
        cell.setAccessible(true);
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
    addButton(help, 7);
    addButton(aboutButton, 8);
    addButton(advancedButton, 9);
    addButton(fxButton, 10);
    fxButton.setButtonText("FX");
    fxButton.setDescription("Alt+F");
    fxButton.onClick=[this] { openFX(); };
    addChildComponent(fxPanel);
    fxPanel.onClose=[this] { closeFX(); };
    fxPanel.onToggleSlotPreview=[this](juce::Component& target) { toggleSlotPreview(target); };
    fxPanel.onPreviewSpace=[this] { return toggleSlotPreviewIfEnabled(); };
    // Show keyboard equivalents directly on the mouse buttons so sighted users
    // discover the accessible workflow while exploring the interface.
    loadSample.setButtonText("Load Sample");
    loadSample.setDescription("Alt+O");
    loadSlot.setButtonText("Load Slot");
    loadSlot.setDescription("Alt+S");
    saveSlot.setButtonText("Save Slot");
    saveSlot.setDescription("Alt+Shift+S");
    loadBank.setButtonText("Load Bank");
    loadBank.setDescription("Alt+B");
    saveBank.setButtonText("Save Bank");
    saveBank.setDescription("Alt+Shift+B");
    help.setButtonText("Help");
    help.setDescription("Alt+H");
    help.onClick = [this] { actionOpenedByShortcut = false; showHelpLanguageMenu(); };
    aboutButton.setButtonText("About");
    aboutButton.setDescription("Alt+A");
    aboutButton.onClick = [this] { actionOpenedByShortcut = false; openAbout(); };
    advancedButton.setButtonText("Advanced");
    advancedButton.setDescription("Alt+T");
    advancedButton.onClick = [this] { actionOpenedByShortcut = false; showAdvancedMenu(); };

    // Keep the text free of the shortcut so the screen reader announces
    // "Slice Editor, button, Alt+E" / "Sample Set, button, Alt+M".
    addButton(sliceEditorButton, 3);
    sliceEditorButton.setButtonText("Slice Editor");
    sliceEditorButton.setDescription("Alt+E");
    sliceEditorButton.onClick = [this] { openSliceEditor(false); };
    addButton(sampleSetButton, 4);
    sampleSetButton.setButtonText("Sample Set");
    sampleSetButton.setDescription("Alt+M");
    sampleSetButton.onClick = [this] { enterSampleSetEditor(); };
    sliceEditorButton.setVisible(false);
    sampleSetButton.setVisible(false);

    const auto aboutText = juce::String("LSampler-24\nVersion: ") + lsamplerVersion
        + "\nRelease date: " + lsamplerReleaseDate
        + "\nProject: " + lsamplerProjectUrl
        + "\nContact: " + lsamplerContactEmail;
    aboutInfo.setTitle("About LSampler-24");
    aboutInfo.setMultiLine(true, true);
    aboutInfo.setReturnKeyStartsNewLine(false);
    aboutInfo.setReadOnly(true);
    aboutInfo.setScrollbarsShown(true);
    aboutInfo.setCaretVisible(true);
    aboutInfo.setPopupMenuEnabled(false);
    aboutInfo.setTabKeyUsedAsCharacter(false);
    aboutInfo.setText(aboutText, false);
    aboutInfo.setDescription(aboutText);
    aboutInfo.setJustification(juce::Justification::centredLeft);
    aboutInfo.setFont(juce::Font(juce::FontOptions(18.0f)));
    aboutInfo.setColour(juce::TextEditor::backgroundColourId, rackgui::screen);
    aboutInfo.setColour(juce::TextEditor::textColourId, rackgui::text);
    aboutInfo.setColour(juce::TextEditor::outlineColourId, rackgui::amber);
    aboutInfo.setColour(juce::TextEditor::focusedOutlineColourId, rackgui::amber);
    aboutInfo.setBorder(juce::BorderSize<int>(12));
    aboutInfo.setWantsKeyboardFocus(true);
    aboutInfo.setExplicitFocusOrder(1);
    aboutInfo.setKeyHandler([this](const juce::KeyPress& key) { return keyPressed(key, &aboutInfo); });
    aboutInfo.onEscapeKey = [this] { closeAbout(); };
    addAndMakeVisible(aboutInfo);
    aboutInfo.setVisible(false);

    aboutProject.setButtonText(juce::String("Project: ") + lsamplerProjectUrl);
    aboutProject.setDescription("LSampler-24 GitHub project URL. Press Enter to open in the default browser.");
    aboutProject.setWantsKeyboardFocus(true);
    aboutProject.setExplicitFocusOrder(2);
    aboutProject.onClick = [this] { openProjectPage(); };
    aboutProject.addKeyListener(this);
    addAndMakeVisible(aboutProject);
    aboutProject.setVisible(false);

    aboutContact.setButtonText(juce::String("Contact: ") + lsamplerContactEmail);
    aboutContact.setDescription("LSampler-24 contact email. Press Enter to open the default email application.");
    aboutContact.setWantsKeyboardFocus(true);
    aboutContact.setExplicitFocusOrder(3);
    aboutContact.onClick = [this] { openContactEmail(); };
    aboutContact.addKeyListener(this);
    addAndMakeVisible(aboutContact);
    aboutContact.setVisible(false);

    aboutClose.setDescription("Closes About. Shortcut Alt+C");
    aboutClose.setWantsKeyboardFocus(true);
    aboutClose.setExplicitFocusOrder(4);
    aboutClose.onClick = [this] { closeAbout(); };
    aboutClose.addKeyListener(this);
    addAndMakeVisible(aboutClose);
    aboutClose.setVisible(false);

    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    status.setAccessible(true);
    status.setTitle("Status");
    status.setDescription({});
    status.setWantsKeyboardFocus(false);
    addAndMakeVisible(status);

    parameterSelector.setWantsKeyboardFocus(true);
    parameterSelector.setExplicitFocusOrder(1);
    parameterSelector.setShortcutHandler([this](const juce::KeyPress& key, juce::Component* source)
    {
        return handleKeyPress(key, source);
    });
    parameterSelector.addKeyListener(this);
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    selectedParameter = juce::jlimit(0, normalGridSize + slotBankSendCount - 1,
                                     processor.getSlotGridPosition(processor.getCurrentSlot()));
    for (int i = 0; i < normalGridSize + slotBankSendCount; ++i)
        parameterSelector.addItem(parameterCellText(i), i + 1);
    parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
    parameterSelector.onChange = [this]
    {
        if (updatingParameterUi) return;
        const int index = parameterSelector.getSelectedItemIndex();
        if (index >= 0)
        {
            const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
            if (bankMacroOpen && bankMacroEntries.empty()) return;
            if (bankMacroOpen)
                bankMacroCursor = juce::jlimit(0, int(bankMacroEntries.size()) - 1, index);
            selectedParameter = bankMacroOpen ? bankMacroEntries[size_t(bankMacroCursor)]
                              : globalOpen ? normalGridSize + index : index;
            syncSelectedAutomationIndicator();
            configureValueForSelectedParameter();
            if (bankMacroOpen) refreshVisuals();
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
    parameterValue.setShortcutHandler([this](const juce::KeyPress& key, juce::Component* source)
    {
        return handleKeyPress(key, source);
    });
    parameterValue.addKeyListener(this);
    parameterValue.onValueChange = [this]
    {
        if (!parameterPage || updatingParameterUi) return;
        setSelectedParameterValue(parameterValue.getValue());
        refreshParameterGrid();
        announceSelectedValue();
    };
    addAndMakeVisible(parameterValue);

    importBrowserCell.setWantsKeyboardFocus(true);
    importBrowserCell.setExplicitFocusOrder(1);
    importBrowserCell.addKeyListener(this);
    addChildComponent(importBrowserCell);
    importBrowserCell.setVisible(false);

    sampleSetCell.setWantsKeyboardFocus(true);
    sampleSetCell.setExplicitFocusOrder(1);
    sampleSetCell.addKeyListener(this);
    addChildComponent(sampleSetCell);
    sampleSetCell.setVisible(false);

    slotLibraryCell.setWantsKeyboardFocus(true);
    slotLibraryCell.setExplicitFocusOrder(1);
    slotLibraryCell.addKeyListener(this);
    addChildComponent(slotLibraryCell);
    slotLibraryCell.setVisible(false);

    importSourceCombo.setWantsKeyboardFocus(true);
    importSourceCombo.setExplicitFocusOrder(2);
    importSourceCombo.setName("Import to Library");
    importSourceCombo.addItem("Files", 1);
    importSourceCombo.addItem("Folder", 2);
    importSourceCombo.setSelectedId(1, juce::dontSendNotification);
    importSourceCombo.addKeyListener(this);
    addChildComponent(importSourceCombo);
    importSourceCombo.setVisible(false);

    exportLibraryButton.setWantsKeyboardFocus(true);
    exportLibraryButton.setExplicitFocusOrder(3);
    exportLibraryButton.addKeyListener(this);
    addChildComponent(exportLibraryButton);
    exportLibraryButton.setVisible(false);

    exportAllLibraryButton.setWantsKeyboardFocus(true);
    exportAllLibraryButton.setExplicitFocusOrder(4);
    exportAllLibraryButton.addKeyListener(this);
    addChildComponent(exportAllLibraryButton);
    exportAllLibraryButton.setVisible(false);

    importLibraryButton.setWantsKeyboardFocus(true);
    importLibraryButton.setExplicitFocusOrder(5);
    importLibraryButton.addKeyListener(this);
    addChildComponent(importLibraryButton);
    importLibraryButton.setVisible(false);

    loadSample.onClick = [this] { actionOpenedByShortcut = false; enterImportBrowser(); };
    loadSlot.onClick   = [this] { actionOpenedByShortcut = false; enterSlotLibraryBrowser(); };
    saveSlot.onClick   = [this] { actionOpenedByShortcut = false; chooseSaveSlot(); };
    loadBank.onClick   = [this] { actionOpenedByShortcut = false; enterSlotLibraryBrowser(false, true); };
    saveBank.onClick   = [this] { actionOpenedByShortcut = false; chooseSaveBank(); };
    exportLibraryButton.onClick = [this] { chooseExportFolder(); };
    exportAllLibraryButton.onClick = [this] { chooseExportLibrary(); };
    importLibraryButton.onClick = [this] { chooseImportLibrary(); };

    // Delay the warning until the editor is fully constructed so the modal
    // alert has a valid parent and Advanced can be opened after OK.
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr)
            safeThis->showStartupLibraryWarningIfNeeded();
    });

    refreshSlotCells();
    leaveSlotParameters();
    startTimerHz(10);
}

void LSampler24AudioProcessorEditor::toggleSlotPreview(juce::Component& announcementTarget)
{
    // One mode shared by Slots and FX. Turning it on also immediately
    // auditions the currently selected slot; Space then toggles playback.
    slotPreviewMode = !slotPreviewMode;
    if (slotPreviewMode)
    {
        processor.requestPreviewStart();
        lsampler::announceToActiveScreenReader(announcementTarget, "Slot preview On");
    }
    else
    {
        processor.requestPreviewStop();
        lsampler::announceToActiveScreenReader(announcementTarget, "DAW space On");
    }
}

bool LSampler24AudioProcessorEditor::toggleSlotPreviewIfEnabled()
{
    if (!slotPreviewMode) return false; // Let REAPER handle Space as usual.
    processor.requestPreviewToggle();
    return true;
}

void LSampler24AudioProcessorEditor::openFX()
{
    if (fxOpen || aboutOpen || fileUiBusy || processor.isFileTaskRunning()) return;
    // Audio sample/browser windows can be obscured without losing selection,
    // cursor, slot, pending edit or current bank. The overlay owns keyboard focus.
    fxReturnFocus=juce::Component::getCurrentlyFocusedComponent();
    fxOpen=true;
    fxPanel.setBounds(getLocalBounds());
    fxPanel.enter();
}

void LSampler24AudioProcessorEditor::closeFX()
{
    if (!fxOpen)return;
    fxOpen=false;
    fxPanel.setVisible(false);
    if (fxReturnFocus != nullptr && fxReturnFocus->isShowing())
        fxReturnFocus->grabKeyboardFocus();
    else if (parameterPage)parameterSelector.grabKeyboardFocus();
    else slotCells[size_t(processor.getCurrentSlot())].grabKeyboardFocus();
}

LSampler24AudioProcessorEditor::~LSampler24AudioProcessorEditor()
{
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    processor.requestLibraryPreviewStop();
    processor.stopSlicePreview();
    if(sliceEditor){sliceEditor->exitModalState(0);sliceEditor.reset();}
    parameterValue.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void LSampler24AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(rackgui::chassis);
    rackgui::caption(g, {20, 12, getWidth()-40, 26}, "LSampler-24  /  DIGITAL SAMPLING WORKSTATION");
    g.setColour(rackgui::muted);g.setFont(12.0f);
    g.drawText("24 SLOTS   /   SAMPLE / SLICE / MULTI OUTPUT",20,40,getWidth()-40,18,juce::Justification::centredLeft);
    if(!importBrowserActive && !slotLibraryActive && !sampleSetActive) {
        rackgui::frame(g, {16,72,352,490}, "SLOTS / 01-24");
        g.setColour(rackgui::muted);g.setFont(13.0f);
        g.drawText(bankMacroOpen?(bankMacroUniform?"BANK MACRO / Set All  /  Alt+U: Relative  /  Backspace: Reset  /  Esc: Back":"BANK MACRO / Relative  /  Alt+U: Set All  /  Backspace: Reset  /  Esc: Back"):globalOpen?"Enter: Confirm  /  Esc: Cancel  /  Alt+V: Value":parameterPage?"Alt+L: Grid  /  Alt+Shift+L: Loop On-Off  /  Alt+M: Sample Set  /  Alt+E: Slice":"Alt+L: Slots  /  Enter: Edit selected slot  /  Alt+K: Bank Macro  /  Alt+T: Advanced",384,588,getWidth()-400,24,juce::Justification::centredLeft);
    }
}

void LSampler24AudioProcessorEditor::resized()
{
    // The tools belong to the Slot parameter grid, not the slot page,
    // global-output editor, browser, or modal Properties page.
    const bool showParameterTools = !globalOpen && !bankMacroOpen && !propertiesOpen
        && !importBrowserActive && !slotLibraryActive && !sampleSetActive;
    sliceEditorButton.setVisible(showParameterTools);
    sampleSetButton.setVisible(showParameterTools);
    fxButton.setVisible(!aboutOpen && !importBrowserActive && !slotLibraryActive && !sampleSetActive && !propertiesOpen);
    if(sliceEditor)sliceEditor->setBounds(getLocalBounds());
    fxPanel.setBounds(getLocalBounds());
    if (propertiesOpen)
    {
        propertiesPanel.setBounds(getLocalBounds().reduced(20).withTrimmedTop(42).withTrimmedBottom(18));
        return;
    }
    repaint(); // Refresh the painted help line when switching editor modes.
    const bool browser=importBrowserActive||slotLibraryActive||sampleSetActive;
    for(auto* display:std::array<juce::Component*,4>{ &waveform,&currentEdit,&sliceOverview,&masterOutput })display->setVisible(!browser);
    slotOverview.setVisible(!browser && parameterPage);
    if(browser) {
        auto area=getLocalBounds().reduced(20).withTrimmedTop(60);
        if(sampleSetActive && !importBrowserActive && !slotLibraryActive) {
            sampleSetCell.setBounds(area.removeFromTop(60));
            return;
        }
        auto& browserCell=slotLibraryActive?slotLibraryCell:importBrowserCell;
        browserCell.setBounds(area.removeFromTop(60));area.removeFromTop(12);
        // Alt+O and the Slot Library browsers use the full browser area.
        // Import/Export controls are exposed only through Advanced (Alt+T).
        return;
    }
    waveform.setBounds(384,72,getWidth()-400,294);
    slotOverview.setBounds(16,72,352,490);
    const auto grid=juce::Rectangle<int>(28,114,328,436);
    for(int col=0;col<3;++col)for(int row=0;row<8;++row)
        slotCells[size_t(col*8+row)].setBounds(grid.getX()+col*110,grid.getY()+row*54,104,50);
    currentEdit.setBounds(384,378,getWidth()-400,200);
    parameterSelector.setBounds(398,504,getWidth()-428,30);
    parameterValue.setBounds(398,540,getWidth()-428,30);
    // Below the numeric edit field, above the Slice/Master panels.
    sliceEditorButton.setBounds(618, 614, 148, 32);
    sampleSetButton.setBounds(774, 614, 148, 32);
    fxButton.setBounds(930, 614, 172, 32);
    fxPanel.setBounds(getLocalBounds());
    const std::array<juce::TextButton*,6> buttons { &loadSample,&loadSlot,&saveSlot,&loadBank,&saveBank,&help };
    for(int i=0;i<6;++i)buttons[size_t(i)]->setBounds(16+(i%3)*118,574+(i/3)*36,114,32);
    aboutButton.setBounds(370,610,114,32);
    advancedButton.setBounds(492,610,114,32);

    auto aboutArea = getLocalBounds().withSizeKeepingCentre(700, 430);
    auto aboutCloseArea = aboutArea.removeFromBottom(42);
    aboutArea.removeFromBottom(10);
    auto aboutContactArea = aboutArea.removeFromBottom(38);
    aboutArea.removeFromBottom(8);
    auto aboutProjectArea = aboutArea.removeFromBottom(38);
    aboutArea.removeFromBottom(10);
    aboutInfo.setBounds(aboutArea);
    aboutProject.setBounds(aboutProjectArea);
    aboutContact.setBounds(aboutContactArea);
    aboutClose.setBounds(aboutCloseArea.withSizeKeepingCentre(140, 36));

    sliceOverview.setBounds(16,650,540,126);
    masterOutput.setBounds(568,650,getWidth()-584,126);
    status.setBounds(16, getHeight()-18, getWidth()-32, 18);
    refreshVisuals();
}

void LSampler24AudioProcessorEditor::refreshVisuals()
{
    if(importBrowserActive||slotLibraryActive||sampleSetActive)return;
    const int slot=processor.getCurrentSlot();
    const auto snapshot=processor.getVisualSlotState(slot);
    waveform.update(snapshot,-1,-1,selectedLoop);
    slotOverview.current=slot;
    for(int i=0;i<24;++i) {
        const auto name=processor.getSlotName(i);
        slotOverview.names[size_t(i)]=name.isNotEmpty()?name:(processor.slotHasSample(i)?"Sample":"Empty");
        slotOverview.loaded[size_t(i)]=processor.slotHasSample(i);
    }
    slotOverview.repaint();
    currentEdit.category=parameterPage?juce::String(selectedEntry().category):"Slot "+juce::String(slot+1);
    currentEdit.name=parameterPage?selectedParameterName():"Press Enter on the selected slot to edit";
    currentEdit.value=parameterPage?selectedParameterValueText():processor.getSlotName(slot);
    currentEdit.repaint();sliceOverview.state=snapshot.slice;sliceOverview.repaint();
    for(int i=0;i<5;++i)masterOutput.values[size_t(i)]=processor.getGlobalOutputParameter(static_cast<lsampler::GlobalP>(i));
    masterOutput.repaint();
}

void LSampler24AudioProcessorEditor::selectSlot(int slotIndex, bool moveKeyboardFocus)
{
    slotIndex = juce::jlimit(0, LSampler24AudioProcessor::slotCount - 1, slotIndex);
    processor.setCurrentSlot(slotIndex);
    refreshSlotCells();
    status.setText(processor.getSampleStatus(), juce::dontSendNotification);
    if (moveKeyboardFocus)
        slotCells[static_cast<size_t>(slotIndex)].grabKeyboardFocus();

    // Alt+P slot audition is deliberately selection-following. Always issue a
    // start, not a toggle: moving to a new slot replaces the old preview and
    // plays this slot through the same configured preview path. Empty slots
    // simply stop the previous preview.
    if (!propertiesOpen && (parameterPage || slotPreviewMode))
        processor.requestPreviewStart();
}


void LSampler24AudioProcessorEditor::returnToCurrentSlotAndAnnounce()
{
    const int slot = processor.getCurrentSlot();
    refreshSlotCells();

    // Moving keyboard/accessibility focus to the slot already makes NVDA announce
    // the slot title.  Do not send a second explicit announcement here: doing both
    // caused the same slot message to overlap itself after navigation and closing
    // modal pages such as Properties.
    juce::Timer::callAfterDelay(60,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), slot]
        {
            if (safeThis == nullptr) return;
            auto& cell = safeThis->slotCells[static_cast<size_t>(slot)];
            cell.grabKeyboardFocus();
            if (auto* handler = cell.getAccessibilityHandler())
                handler->grabFocus();
        });
}

void LSampler24AudioProcessorEditor::showHelpLanguageMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(helpMenuLookAndFeel.get());
    menu.addSectionHeader("Help language");
    menu.addItem(1, "English");
    menu.addItem(2, "Italiano");
    menu.addItem(3, juce::String::fromUTF8("Español"));
    menu.addItem(4, juce::String::fromUTF8("Português"));
    menu.addItem(5, juce::String::fromUTF8("Français"));
    menu.addItem(6, juce::String::fromUTF8("Русский"));
    menu.addItem(7, juce::String::fromUTF8("中文"));
    menu.addItem(8, juce::String::fromUTF8("日本語"));

    auto* popupTarget = help.isShowing() ? static_cast<juce::Component*>(&help)
                                       : juce::Component::getCurrentlyFocusedComponent();
    const juce::Component::SafePointer<juce::Component> returnFocus(popupTarget);
    const bool invokedByShortcut = actionOpenedByShortcut;
    auto options = juce::PopupMenu::Options();
    if (popupTarget != nullptr)
        options = options.withTargetComponent(popupTarget);

    menu.showMenuAsync(options,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), returnFocus, invokedByShortcut](int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 0)
            {
                if (invokedByShortcut)
                    safeThis->returnToActionOrigin(safeThis->help);
                else
                    juce::Timer::callAfterDelay(50,
                        [returnFocus]
                        {
                            if (returnFocus == nullptr) return;
                            returnFocus->grabKeyboardFocus();
                            if (auto* handler = returnFocus->getAccessibilityHandler())
                                handler->grabFocus();
                        });
                return;
            }
            safeThis->actionOpenedByShortcut = false;

            static constexpr const char* codes[] { "en", "it", "es", "pt", "fr", "ru", "zh", "ja" };
            if (juce::isPositiveAndBelow(result - 1, static_cast<int>(std::size(codes))))
                safeThis->openHelp(codes[result - 1]);
        });
}

void LSampler24AudioProcessorEditor::openHelp(const juce::String& languageCode)
{
    int dataSize = 0;
    const auto* data = BinaryData::getNamedResource("LSampler24Help_html", dataSize);
    if (data == nullptr || dataSize <= 0)
    {
        lsampler::announceToActiveScreenReader(help, "Help file unavailable");
        return;
    }

    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("LSampler-24 Help");
    if (folder.createDirectory().failed())
    {
        lsampler::announceToActiveScreenReader(help, "Cannot create help folder");
        return;
    }

    auto html = juce::String::fromUTF8(data, dataSize);
    const auto safeLanguage = juce::StringArray { "en", "it", "es", "pt", "fr", "ru", "zh", "ja" }
                                  .contains(languageCode) ? languageCode : "en";
    html = html.replace("const supported=",
                        "const requestedLanguage='" + safeLanguage + "';const supported=");
    html = html.replace(":'en';document.querySelectorAll",
                        ":requestedLanguage;document.querySelectorAll");

    const auto file = folder.getChildFile("LSampler-24 Help " + safeLanguage + ".html");
    if (!file.replaceWithText(html, false, false, "\n"))
    {
        lsampler::announceToActiveScreenReader(help, "Cannot write help file");
        return;
    }

    if (!file.startAsProcess())
        lsampler::announceToActiveScreenReader(help, "Cannot open help in the default browser");
}

void LSampler24AudioProcessorEditor::openProperties()
{
    if (propertiesOpen || sliceEditor != nullptr || importBrowserActive || slotLibraryActive)
        return;
    propertiesReturnFocus = juce::Component::getCurrentlyFocusedComponent();
    propertiesPanel.setInfo(processor.getCurrentSamplePropertiesText(), processor.getCurrentSlotPropertiesText());
    propertiesConfigColumn = false;
    propertiesSampleRow = 0;
    propertiesConfigRow = 0;
    propertiesOpen = true;
    propertiesPanel.setVisible(true);
    propertiesPanel.toFront(false);
    resized();
    propertiesPanel.sampleInfo.grabKeyboardFocus();
    propertiesPanel.highlightLine(false, propertiesSampleRow);
    juce::Timer::callAfterDelay(1,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
        {
            if (safeThis == nullptr || !safeThis->propertiesOpen) return;
            if (auto* handler = safeThis->propertiesPanel.sampleInfo.getAccessibilityHandler())
                handler->grabFocus();
            const auto line = safeThis->propertiesPanel.lineText(false, safeThis->propertiesSampleRow);
            if (line.isNotEmpty())
                lsampler::announceToActiveScreenReader(safeThis->propertiesPanel.sampleInfo,
                    "Properties. Audio File. " + line);
        });
}

void LSampler24AudioProcessorEditor::closeProperties()
{
    if (!propertiesOpen) return;
    propertiesOpen = false;
    propertiesPanel.setVisible(false);
    propertiesReturnFocus = nullptr;
    resized();
    // Properties is opened from the current slot. Escape always returns to that
    // slot and reuses the normal existing slot announcement.
    returnToCurrentSlotAndAnnounce();
}

void LSampler24AudioProcessorEditor::refreshSlotCells()
{
    const int selected = processor.getCurrentSlot();
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[static_cast<size_t>(i)];
        const auto label = processor.getSlotLabel(i);
        cell.setSlotText(label);
        // Notification text must never become part of the persistent slot line.
        cell.setDescription({});
        const auto name=processor.getSlotName(i);
        cell.setVisualState(i+1,name.isNotEmpty()?name:(processor.slotHasSample(i)?"Sample":"Empty"),processor.slotHasSample(i),i==selected);
        cell.setWantsKeyboardFocus(!parameterPage && i == selected);
    }
}

juce::String LSampler24AudioProcessorEditor::midiNoteText(int note)
{
    note = juce::jlimit(0, 127, note);
    static const char* names[] = { "C", "C sharp", "D", "D sharp", "E", "F", "F sharp", "G", "G sharp", "A", "A sharp", "B" };
    return juce::String(note) + " " + names[note % 12] + " " + juce::String((note / 12) - 1);
}

bool LSampler24AudioProcessorEditor::isSlotBankSend(int index) const noexcept
{
    return !bankMacroOpen && !globalOpen
        && index >= slotBankSendFirst && index < slotBankSendFirst + slotBankSendCount;
}

juce::String LSampler24AudioProcessorEditor::slotBankSendName(int index) const
{
    static constexpr const char* names[] = { "Bank Chorus Send", "Bank Delay Send", "Bank Reverb Send" };
    return names[size_t(juce::jlimit(0, slotBankSendCount - 1, slotBankSendIndex(index)))];
}

const lsampler::Descriptor& LSampler24AudioProcessorEditor::slotBankSendDescriptor() noexcept
{
    static const lsampler::Descriptor d { "slot_bank_send", "Bank Send", "Bank Sends",
        lsampler::Kind::continuous, 100, 0, 100, 1, 1, "%", 0, "" };
    return d;
}

bool LSampler24AudioProcessorEditor::isBankFXSend(int index) const noexcept
{
    return bankMacroOpen && index >= bankFXFirst && index < bankFXFirst + bankFXCount;
}

int LSampler24AudioProcessorEditor::bankFXParameter(int index) const noexcept
{
    switch (index - bankFXFirst)
    {
        case 0: return lsampler::fx::chorusMix;
        case 1: return lsampler::fx::delayMix;
        case 2: return lsampler::fx::reverbMix;
        default: return -1;
    }
}

const GridEntry& LSampler24AudioProcessorEditor::bankEntry(int index) const noexcept
{
    if (isBankFXSend(index))
    {
        static const GridEntry send { -1, -1, Action::none, "Bank FX Sends", -1 };
        return send;
    }
    if (!juce::isPositiveAndBelow(index, int(lsampler::grid.size())))
    {
        static const GridEntry invalid { -1, -1, Action::none, "Bank Macro", -1 };
        return invalid;
    }
    return lsampler::grid[size_t(index)];
}

juce::String LSampler24AudioProcessorEditor::bankEntryName(int index) const
{
    if (isBankFXSend(index))
        return "Bank " + juce::String(lsampler::fx::parameters[size_t(bankFXParameter(index))].name);
    if (!juce::isPositiveAndBelow(index, int(lsampler::grid.size()))) return "Bank Macro";
    return displayedGridParameterName(lsampler::grid[size_t(index)]);
}

const GridEntry& LSampler24AudioProcessorEditor::selectedEntry() const
{
    if (isSlotBankSend(selectedParameter))
    {
        static const GridEntry virtualEntry { -1, -1, Action::none, "Bank Sends", -1 };
        return virtualEntry;
    }
    return bankMacroOpen ? bankEntry(selectedParameter) : lsampler::grid[size_t(selectedParameter)];
}
int LSampler24AudioProcessorEditor::categoryBegin(int index) const {
    if (isSlotBankSend(index)) return slotBankSendFirst;
    const auto* name=lsampler::grid[size_t(index)].category;
    while(index>0 && std::strcmp(lsampler::grid[size_t(index-1)].category,name)==0)--index;
    return index;
}
int LSampler24AudioProcessorEditor::categoryEnd(int index) const {
    if (isSlotBankSend(index)) return slotBankSendFirst + slotBankSendCount - 1;
    const auto* name=lsampler::grid[size_t(index)].category;
    while(index+1<int(lsampler::grid.size()) && std::strcmp(lsampler::grid[size_t(index+1)].category,name)==0)++index;
    return index;
}
bool LSampler24AudioProcessorEditor::selectedRateIsSynced() const {
    // A macro may target slots with different LFO sync settings. No single
    // selected-slot value can describe all of them.
    if (bankMacroOpen) return false;
    const auto id=selectedEntry().parameter;
    if(id!=int(P::lfo1_rate)&&id!=int(P::lfo2_rate))return false;
    const auto sync=id==int(P::lfo1_rate)?P::lfo1_bpm_sync:P::lfo2_bpm_sync;
    for(size_t i=0;i<lsampler::grid.size();++i)if(lsampler::grid[i].parameter==int(sync))return processor.getSlotParameter(int(i))!=0;
    return false;
}
juce::String LSampler24AudioProcessorEditor::selectedParameterName() const {
    if (isSlotBankSend(selectedParameter))
        return slotBankSendName(selectedParameter)
            + ((observedAutomationState == 1 && observedAutomationParameter == automationGridSelection())
                ? juce::String(", Auto") : juce::String());
    if (isBankFXSend(selectedParameter))
        return bankEntryName(selectedParameter)
            + ((observedAutomationState == 1 && observedAutomationParameter == automationGridSelection())
                ? juce::String(", Auto") : juce::String());
    const auto& e=selectedEntry();
    const juce::String name = displayedGridParameterName(e);
    return (std::strcmp(e.category,"Loops")==0?"Loop "+juce::String(selectedLoop+1)+" ":juce::String())+name
        + ((observedAutomationState == 1 && observedAutomationParameter == automationGridSelection())
             ? juce::String(", Auto") : juce::String());
}
double LSampler24AudioProcessorEditor::getSelectedParameterValue() const {
    if (isSlotBankSend(selectedParameter))
        return processor.getSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(selectedParameter));
    if (isBankFXSend(selectedParameter))
        return processor.getFXParameter(bankFXParameter(selectedParameter));
    if (bankMacroOpen)
        return bankMacroUniform ? bankMacroAmounts[size_t(selectedEntry().parameter)]
                                : processor.getBankMacroOffset(selectedEntry().parameter);
    return processor.getSlotParameter(selectedParameter,selectedLoop);
}
void LSampler24AudioProcessorEditor::setSelectedParameterValue(double value)
{
    if (isSlotBankSend(selectedParameter))
    {
        processor.setSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(selectedParameter), value);
        return;
    }
    if (isBankFXSend(selectedParameter))
    {
        processor.setFXParameter(bankFXParameter(selectedParameter), value);
        return;
    }
    const auto parameter = selectedEntry().parameter;
    if (bankMacroOpen)
    {
        const auto& d = descriptor(selectedEntry());
        const double maximumOffset = d.maximum - d.minimum;
        value = bankMacroUniform ? sanitise(d, value) :
            juce::jlimit(-maximumOffset, maximumOffset, value);
        if (!bankMacroUniform && (d.kind == Kind::integer || d.kind == Kind::enumeration || d.kind == Kind::note))
            value = std::round(value);
        if (bankMacroUniform) {
            bankMacroAmounts[size_t(parameter)] = value;
            processor.applyBankMacro(selectedParameter, bankMacroReference, value, true);
        } else {
            processor.setBankMacroOffset(parameter, value);
        }
        return;
    }
    processor.setSlotParameter(selectedParameter, value, selectedLoop);
    if (parameter == int(P::sample_start))
        processor.requestSampleBoundaryAudition(false, true);
    else if (parameter == int(P::sample_end))
        processor.requestSampleBoundaryAudition(true, true);
    else if (parameter == int(P::start_threshold))
        processor.requestSampleBoundaryAudition(false, false);
    else if (parameter == int(P::end_threshold) || parameter == int(P::end_preview_length))
        processor.requestSampleBoundaryAudition(true, false);
}
juce::String LSampler24AudioProcessorEditor::formatParameter(int index,double value) const {
    if (isBankFXSend(index) || isSlotBankSend(index)) return juce::String(value, 0) + " %";
    if (!juce::isPositiveAndBelow(index, int(lsampler::grid.size()))) return {};
    const auto& e=lsampler::grid[size_t(index)];const auto& d=descriptor(e);
    if(d.kind==Kind::note)return value<0?juce::String("Off"):midiNoteText(juce::roundToInt(value));
    if(e.parameter==int(P::global_one_shot)) {
        static const juce::StringArray labels { "Main Loop", "One Shot", "On Release" };
        return labels[juce::jlimit(0, labels.size()-1, juce::roundToInt(value-d.minimum))];
    }
    if(d.kind==Kind::enumeration||d.kind==Kind::action) {
        const auto labels=juce::StringArray::fromTokens(d.labels,"|","");
        return labels[juce::jlimit(0,labels.size()-1,juce::roundToInt(value-d.minimum))];
    }
    if(e.loop==int(L::repeats)&&value==0)return "0, infinite";
    if(e.parameter==int(P::lfo1_rate)||e.parameter==int(P::lfo2_rate)) {
        const auto sync=e.parameter==int(P::lfo1_rate)?P::lfo1_bpm_sync:P::lfo2_bpm_sync;
        bool synced=false;
        for(size_t i=0;i<lsampler::grid.size();++i)if(lsampler::grid[i].parameter==int(sync)){synced=processor.getSlotParameter(int(i))!=0;break;}
        if(!synced)return juce::String(value,3)+" Hz";
        juce::String label;
        const std::pair<double,const char*> anchors[]{{.125,"8 bars"},{.25,"4 bars"},{.5,"2 bars"},{1,"1 bar"},{2,"half bar"},
            {4,"1 beat"},{8,"eighth"},{16,"sixteenth"},{32,"thirty second"},{64,"sixty fourth"},{128,"one twenty eighth"},
            {256,"one two fifty sixth"},{512,"one five twelfth"},{1.5,"bar triplet"},{3,"half bar triplet"},{6,"beat triplet"},
            {12,"eighth triplet"},{24,"sixteenth triplet"},{48,"thirty second triplet"},{96,"sixty fourth triplet"},{192,"one twenty eighth triplet"},{384,"one two fifty sixth triplet"},
            {5,"four fifths beat"},{10,"two fifths beat"},{20,"one fifth beat"},{40,"one tenth beat"},{80,"one twentieth beat"}};
        for(const auto& a:anchors)if(std::abs(value-a.first)<.000001){label=", "+juce::String(a.second);break;}
        return juce::String(value,3)+label;
    }
    return juce::String(value,d.decimals)+(juce::String(d.unit).isEmpty()?juce::String():" "+juce::String(d.unit));
}
juce::String LSampler24AudioProcessorEditor::bankMacroRelativeText(int gridIndex, double offset) const
{
    if (!juce::isPositiveAndBelow(gridIndex, int(lsampler::grid.size()))) return {};
    const auto& entry = lsampler::grid[size_t(gridIndex)];
    const auto& d = descriptor(entry);
    const int parameter = entry.parameter;
    const bool isChoice = d.kind == Kind::enumeration;

    // Relative macro values are offsets, not the underlying setting.  Show the
    // actual result when every occupied slot agrees, including the real enum
    // label (Off/On, filter mode, LFO waveform, etc.).  When slots differ,
    // explain the direction of the offset instead of announcing a misleading
    // signed "raw" value or enum index.
    bool hasSlot = false;
    bool matches = true;
    double common = 0.0;
    for (int slot = 0; slot < LSampler24AudioProcessor::slotCount; ++slot)
    {
        if (!bankMacroReference.occupied[size_t(slot)]) continue;
        const double result = sanitise(d,
            bankMacroReference.values[size_t(slot)][size_t(parameter)] + offset);
        if (!hasSlot) { common = result; hasSlot = true; }
        else if (std::abs(result - common) > 1.0e-8) matches = false;
    }
    if (!hasSlot) return "No loaded slots";

    const double magnitude = std::abs(offset);
    const bool neutral = magnitude < 1.0e-9;
    juce::String change = "unchanged";
    if (!neutral)
    {
        const juce::String unit = juce::String(d.unit);
        const juce::String amount = juce::String(magnitude, d.decimals);
        if (isChoice)
            change = (offset > 0.0 ? "next option" : "previous option")
                     + juce::String(" by ") + amount;
        else
            change = (offset > 0.0 ? "increase by " : "decrease by ")
                     + amount + (unit.isEmpty() ? juce::String() : " " + unit);
    }

    if (!matches)
        return neutral ? "Mixed values" : "Mixed values, " + change;

    // LFO frequency units depend on each slot's BPM Sync setting. Avoid using
    // the currently selected slot's state as though it applied to the bank.
    juce::String resultText;
    if (parameter == int(P::lfo1_rate) || parameter == int(P::lfo2_rate))
    {
        const int sync = parameter == int(P::lfo1_rate)
            ? int(P::lfo1_bpm_sync) : int(P::lfo2_bpm_sync);
        bool syncSeen = false;
        bool allSync = true, allFree = true;
        for (int slot = 0; slot < LSampler24AudioProcessor::slotCount; ++slot)
        {
            if (!bankMacroReference.occupied[size_t(slot)]) continue;
            const bool active = bankMacroReference.values[size_t(slot)][size_t(sync)] != 0.0;
            syncSeen = true;
            allSync &= active;
            allFree &= !active;
        }
        resultText = juce::String(common, d.decimals)
                   + (syncSeen && allFree ? " Hz" : syncSeen && allSync
                      ? " (BPM Sync)" : " (mixed BPM Sync)");
    }
    else resultText = formatParameter(gridIndex, common);

    // Include the movement as well as the result, so that even when a setting
    // is clamped at its limit the user can tell a non-neutral macro is stored.
    return neutral ? resultText : resultText + ", " + change;
}
juce::String LSampler24AudioProcessorEditor::bankMacroValueText(int gridIndex) const
{
    if (isBankFXSend(gridIndex))
        return formatParameter(gridIndex, processor.getFXParameter(bankFXParameter(gridIndex)));
    if (!juce::isPositiveAndBelow(gridIndex, int(lsampler::grid.size()))) return {};
    const auto& entry = lsampler::grid[size_t(gridIndex)];
    const int parameter = entry.parameter;
    const auto value = bankMacroUniform ? bankMacroAmounts[size_t(parameter)]
                                        : processor.getBankMacroOffset(parameter);
    if (bankMacroUniform)
        return formatParameter(gridIndex, value);
    return bankMacroRelativeText(gridIndex, value);
}

juce::String LSampler24AudioProcessorEditor::selectedParameterValueText() const {
    return bankMacroOpen ? bankMacroValueText(selectedParameter) :
        formatParameter(selectedParameter,getSelectedParameterValue());
}
juce::String LSampler24AudioProcessorEditor::parameterCellText(int index) const {
    if (isSlotBankSend(index))
    {
        const juce::String marker = (index == selectedParameter && observedAutomationState == 1
            && observedAutomationParameter == automationGridSelection()) ? ", Auto" : "";
        return slotBankSendName(index) + ", "
            + formatParameter(index, processor.getSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(index))) + marker;
    }
    if (isBankFXSend(index))
    {
        const juce::String marker = (index == selectedParameter && observedAutomationState == 1
            && observedAutomationParameter == automationGridSelection()) ? ", Auto" : "";
        return bankEntryName(index) + ", " + bankMacroValueText(index) + marker;
    }
    if (!juce::isPositiveAndBelow(index, int(lsampler::grid.size()))) return {};
    const auto& e=lsampler::grid[size_t(index)];
    const juce::String prefix=std::strcmp(e.category,"Loops")==0?"Loop "+juce::String(selectedLoop+1)+" ":juce::String();
    const juce::String name = displayedGridParameterName(e);
    const juce::String marker = (index == selectedParameter && observedAutomationState == 1
        && observedAutomationParameter == automationGridSelection()) ? ", Auto" : "";
    if (bankMacroOpen)
        return name + ", " + bankMacroValueText(index) + marker;
    return prefix+name+", "+formatParameter(index,processor.getSlotParameter(index,selectedLoop)) + marker;
}
void LSampler24AudioProcessorEditor::refreshParameterGrid() {
    // ComboBox rebuilds must never re-enter selection/value callbacks.
    const juce::ScopedValueSetter<bool> uiGuard(updatingParameterUi, true);
    // Update one selected parameter, not the entire 143-parameter grid.
    auto* selectedHost = automationGridSelection();
    if (selectedHost != observedAutomationParameter)
    {
        observedAutomationParameter = selectedHost;
        observedAutomationState = getHostAutomationEnvelopeState(selectedHost);
    }
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    parameterSelector.clear(juce::dontSendNotification);
    if (bankMacroOpen)
    {
        if (bankMacroEntries.empty()) return;
        for (int i = 0; i < int(bankMacroEntries.size()); ++i)
            parameterSelector.addItem(parameterCellText(bankMacroEntries[size_t(i)]), i + 1);
        bankMacroCursor = juce::jlimit(0, int(bankMacroEntries.size()) - 1, bankMacroCursor);
        selectedParameter = bankMacroEntries[size_t(bankMacroCursor)];
        parameterSelector.setSelectedItemIndex(bankMacroCursor, juce::dontSendNotification);
    }
    else if (globalOpen)
    {
        for (int i = 0; i < lsampler::globalParameterCount; ++i)
            parameterSelector.addItem(parameterCellText(normalGridSize + i), i + 1);
        const int localIndex = juce::jlimit(0, lsampler::globalParameterCount - 1, selectedParameter - normalGridSize);
        parameterSelector.setSelectedItemIndex(localIndex, juce::dontSendNotification);
    }
    else
    {
        for (int i = 0; i < normalGridSize + slotBankSendCount; ++i)
            parameterSelector.addItem(parameterCellText(i), i + 1);
        selectedParameter = juce::jlimit(0, normalGridSize + slotBankSendCount - 1, selectedParameter);
        parameterSelector.setSelectedItemIndex(selectedParameter,juce::dontSendNotification);
    }
    configureValueForSelectedParameter();
    // Navigation changes the displayed parameter/loop without changing audio state.
    refreshVisuals();
}
void LSampler24AudioProcessorEditor::configureValueForSelectedParameter() {
    if (isSlotBankSend(selectedParameter))
    {
        const auto& d = slotBankSendDescriptor();
        parameterValue.setRange(d.minimum, d.maximum, d.step);
        parameterValue.textFromValueFunction = [](double v) { return juce::String(v, 0) + " %"; };
        parameterValue.valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue(); };
        parameterValue.setParameterAccessibilityName(selectedParameterName());
        parameterValue.setValue(getSelectedParameterValue(), juce::dontSendNotification);
        return;
    }
    if (isBankFXSend(selectedParameter))
    {
        const auto& desc = lsampler::fx::parameters[size_t(bankFXParameter(selectedParameter))];
        parameterValue.setRange(desc.minimum, desc.maximum, desc.step);
        parameterValue.textFromValueFunction = [this](double v) {
            return juce::String(v, 0) + " %";
        };
        parameterValue.valueFromTextFunction = [](const juce::String& text) {
            return text.getDoubleValue();
        };
        parameterValue.setParameterAccessibilityName(selectedParameterName());
        parameterValue.setValue(getSelectedParameterValue(), juce::dontSendNotification);
        return;
    }
    const auto& d=descriptor(selectedEntry());
    const double offsetLimit = d.maximum - d.minimum;
    parameterValue.setRange(bankMacroOpen && !bankMacroUniform ? -offsetLimit : (selectedRateIsSynced()?.125:d.minimum),
        bankMacroOpen && !bankMacroUniform ? offsetLimit : d.maximum,
        d.kind==Kind::integer||d.kind==Kind::enumeration||d.kind==Kind::note||d.kind==Kind::action?1.0:0.0);
    parameterValue.textFromValueFunction=[this](double v){
        if (bankMacroOpen && !bankMacroUniform)
            return bankMacroRelativeText(selectedParameter, v);
        return formatParameter(selectedParameter,v);
    };
    parameterValue.valueFromTextFunction=[this](const juce::String& text) {
        const auto& desc=descriptor(selectedEntry());
        if (bankMacroOpen && !bankMacroUniform) return text.getDoubleValue();
        if(selectedEntry().parameter==int(P::global_one_shot)) {
            static const juce::StringArray labels { "Main Loop", "One Shot", "On Release" };
            const int found=labels.indexOf(text.trim(),true);if(found>=0)return desc.minimum+found;
        } else if(desc.kind==Kind::enumeration||desc.kind==Kind::action) {
            const auto labels=juce::StringArray::fromTokens(desc.labels,"|","");
            const int found=labels.indexOf(text.trim(),true);if(found>=0)return desc.minimum+found;
        }
        if(text.trim().equalsIgnoreCase("Off")&&desc.kind==Kind::note)return -1.0;
        return text.getDoubleValue();
    };
    parameterValue.setParameterAccessibilityName(selectedParameterName());
    parameterValue.setValue(getSelectedParameterValue(),juce::dontSendNotification);
}
void LSampler24AudioProcessorEditor::openValueEditor() {
    parameterValue.showTextBox();
    if(auto* editor=dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent())) {
        const int decimals = (isBankFXSend(selectedParameter) || isSlotBankSend(selectedParameter))
            ? 0 : descriptor(selectedEntry()).decimals;
        editor->setText(juce::String(getSelectedParameterValue(),decimals),false);
        editor->selectAll();
    }
}

void LSampler24AudioProcessorEditor::enterSlotParameters()
{
    // The main grid intentionally defaults to configured-slot audition. The
    // slot page defaults to DAW Space; each surface can therefore be learned
    // consistently with Alt+P as the explicit hand-off toggle.
    gridPreviewMode = true;
    parameterPage = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setVisible(true);
    parameterValue.setVisible(true);
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    selectedParameter = juce::jlimit(0, normalGridSize + slotBankSendCount - 1,
                                     processor.getSlotGridPosition(processor.getCurrentSlot()));
    parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
    refreshParameterGrid();
    resized();
    parameterSelector.setEntryAccessibility();
    parameterSelector.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveSlotParameters()
{
    // Persist the runtime-only cursor only when we are genuinely leaving the
    // slot parameter grid.  The editor constructor also calls this helper to
    // establish the slot-page visibility; saving there would overwrite the
    // processor's remembered cursor with the selector's default item (Level).
    if (parameterPage && !bankMacroOpen)
    {
        const int currentGridIndex = parameterSelector.getSelectedItemIndex();
        if (currentGridIndex >= 0)
        {
            selectedParameter = currentGridIndex;
            processor.setSlotGridPosition(processor.getCurrentSlot(), selectedParameter);
        }
    }
    processor.requestPreviewStop();
    parameterPage = false;
    parameterSelector.setVisible(false);
    parameterValue.setVisible(false);
    for (auto& cell : slotCells) cell.setVisible(true);
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true); aboutButton.setVisible(true); advancedButton.setVisible(true);
    refreshSlotCells();
    resized();
}

// Do not clear/repopulate the focused ComboBox from inside its own Alt+K
// keyPressed/KeyListener dispatch (including the NVDA accessibility call).
// A queued transition also collapses repeated listener dispatch into one action.
void LSampler24AudioProcessorEditor::queueBankMacroToggle()
{
    if (bankMacroTransitionPending) return;
    bankMacroTransitionPending = true;
    juce::MessageManager::callAsync(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
        {
            if (safeThis == nullptr) return;
            safeThis->bankMacroTransitionPending = false;
            if (safeThis->bankMacroOpen) safeThis->closeBankMacro();
            else safeThis->openBankMacro();
        });
}

// The Relative mode edits host-automatable, persistent Bank Macro offsets;
// Set All is a deliberate one-shot edit of the ordinary per-slot parameters.
void LSampler24AudioProcessorEditor::openBankMacro()
{
    if (bankMacroOpen || globalOpen || importBrowserActive || slotLibraryActive ||
        sampleSetActive || propertiesOpen || sliceEditor || aboutOpen || fileUiBusy)
        return;

    bankMacroEntries.clear();
    const int normalGridSize = int(lsampler::grid.size()) - lsampler::globalParameterCount;
    for (int i = 0; i < normalGridSize; ++i)
    {
        const auto& e = lsampler::grid[size_t(i)];
        if (e.global >= 0 || e.loop >= 0 || e.action != Action::none || e.parameter < 0) continue;
        if (!lsampler::bankMacroEligible(e)) continue;
        bankMacroEntries.push_back(i);
    }
    // One shared send amount per effect engine. The 3 controls belong to the
    // bank and REAPER state, never to a saved Slot. No new DSP or FX IDs.
    for (int i = 0; i < bankFXCount; ++i)
        bankMacroEntries.push_back(bankFXFirst + i);

    bankMacroReference = processor.getBankMacroSnapshot();
    int count = 0;
    for (bool occupied : bankMacroReference.occupied) if (occupied) ++count;
    // Alt+K also opens on an empty bank so the global FX sends are editable.
    // The three bank-wide sends ensure the list is non-empty; still guard
    // unexpected catalog changes before indexing a row.
    if (bankMacroEntries.empty()) return;
    const juce::ScopedValueSetter<bool> uiGuard(updatingParameterUi, true);
    bankMacroReturnWasParameterPage = parameterPage;
    bankMacroReturnParameter = selectedParameter;
    bankMacroOpen = true;
    processor.requestPreviewStop();
    bankMacroUniform = false;
    bankMacroAmounts.fill(0.0);
    bankMacroCursor = juce::jlimit(0, int(bankMacroEntries.size()) - 1, bankMacroLastCursor);
    selectedParameter = bankMacroEntries[size_t(bankMacroCursor)];
    parameterPage = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false);
    aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setBankMacroContext(true);
    parameterSelector.setVisible(true);
    parameterValue.setVisible(true);
    refreshParameterGrid();
    resized();
    parameterSelector.setEntryAccessibility();
    parameterSelector.grabKeyboardFocus();
    lsampler::announceToActiveScreenReader(parameterSelector,
        "Bank Macro Grid. " + juce::String(count) + " loaded slots. Relative. " +
        parameterCellText(selectedParameter));
}

void LSampler24AudioProcessorEditor::closeBankMacro(bool restoreFocus)
{
    if (!bankMacroOpen) return;
    const juce::ScopedValueSetter<bool> uiGuard(updatingParameterUi, true);
    bankMacroLastCursor = bankMacroCursor;
    // The last Bank FX Send uses a UI-only index beyond grid.size(). Never
    // expose it to ordinary parameter callbacks once macro mode is closed.
    selectedParameter = juce::jlimit(0, slotBankSendFirst + slotBankSendCount - 1,
                                     bankMacroReturnParameter);
    bankMacroOpen = false;
    bankMacroUniform = false;
    parameterSelector.setBankMacroContext(false);
    if (bankMacroReturnWasParameterPage)
    {
        parameterPage = true;
        selectedParameter = bankMacroReturnParameter;
        refreshParameterGrid();
        resized();
        if (restoreFocus) focusParameterGrid();
    }
    else
    {
        parameterPage = false;
        parameterSelector.setVisible(false);
        parameterValue.setVisible(false);
        for (auto& cell : slotCells) cell.setVisible(true);
        loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
        loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true);
        aboutButton.setVisible(true); advancedButton.setVisible(true);
        refreshSlotCells();
        resized();
        if (restoreFocus) returnToCurrentSlotAndAnnounce();
    }
}

void LSampler24AudioProcessorEditor::selectBankMacroEntry(int position, bool announce)
{
    if (!bankMacroOpen || !juce::isPositiveAndBelow(position, int(bankMacroEntries.size()))) return;
    if (position == bankMacroCursor) return;
    bankMacroCursor = position;
    selectedParameter = bankMacroEntries[size_t(bankMacroCursor)];
    parameterSelector.setSelectedItemIndex(position,
        announce ? juce::sendNotificationSync : juce::dontSendNotification);
    syncSelectedAutomationIndicator();
    configureValueForSelectedParameter();
    refreshVisuals();
}

void LSampler24AudioProcessorEditor::moveBankMacroPage(int direction, bool firstParameter)
{
    if (!bankMacroOpen || bankMacroEntries.empty()) return;
    const int count = int(bankMacroEntries.size());
    const auto categoryAt = [this](int cursor) {
        return bankEntry(bankMacroEntries[size_t(cursor)]).category;
    };
    const auto* currentCategory = categoryAt(bankMacroCursor);
    int begin = bankMacroCursor;
    int end = bankMacroCursor;
    while (begin > 0 && std::strcmp(categoryAt(begin - 1), currentCategory) == 0)
        --begin;
    while (end + 1 < count && std::strcmp(categoryAt(end + 1), currentCategory) == 0)
        ++end;

    int nextBegin = 0;
    int nextEnd = 0;
    if (direction > 0)
    {
        if (end + 1 >= count) return;
        nextBegin = end + 1;
        nextEnd = nextBegin;
        const auto* nextCategory = categoryAt(nextBegin);
        while (nextEnd + 1 < count && std::strcmp(categoryAt(nextEnd + 1), nextCategory) == 0)
            ++nextEnd;
    }
    else
    {
        if (begin == 0) return;
        nextEnd = begin - 1;
        nextBegin = nextEnd;
        const auto* previousCategory = categoryAt(nextEnd);
        while (nextBegin > 0 && std::strcmp(categoryAt(nextBegin - 1), previousCategory) == 0)
            --nextBegin;
    }
    const int row = firstParameter ? 0 : bankMacroCursor - begin;
    selectBankMacroEntry(juce::jmin(nextBegin + row, nextEnd), true);
}

void LSampler24AudioProcessorEditor::toggleBankMacroUniform()
{
    if (!bankMacroOpen) return;
    // Host macro offsets remain persistent when toggling to Set All; the latter
    // edits base slot values. Returning to Relative shows host values again.
    bankMacroReference = processor.getBankMacroSnapshot();
    bankMacroUniform = !bankMacroUniform;
    bankMacroAmounts.fill(0.0);
    if (bankMacroUniform)
        for (int gridIndex : bankMacroEntries)
        {
            if (isBankFXSend(gridIndex)) continue;
            const int parameter = lsampler::grid[size_t(gridIndex)].parameter;
            int slot = processor.getCurrentSlot();
            if (!bankMacroReference.occupied[size_t(slot)])
                for (int s = 0; s < LSampler24AudioProcessor::slotCount; ++s)
                    if (bankMacroReference.occupied[size_t(s)]) { slot = s; break; }
            bankMacroAmounts[size_t(parameter)] = bankMacroReference.values[size_t(slot)][size_t(parameter)];
        }
    refreshParameterGrid();
    lsampler::announceToActiveScreenReader(parameterSelector,
        bankMacroUniform ? "Bank Macro, Set All. Changes assign the same value to every loaded slot"
                         : "Bank Macro, Relative. Original slot differences preserved");
}

void LSampler24AudioProcessorEditor::resetSelectedBankMacro()
{
    if (!bankMacroOpen) return;
    if (isBankFXSend(selectedParameter))
    {
        // For an auxiliary bank send, reset silences its wet path.
        if (getSelectedParameterValue() == 0.0) return;
        setSelectedParameterValue(0.0);
        refreshParameterGrid();
        announceSelectedValue();
        return;
    }
    const int parameter = selectedEntry().parameter;
    if (bankMacroUniform)
    {
        // In Set All mode Backspace restores the per-slot snapshot, not one
        // common value (and without altering the current host macro offset).
        processor.applyBankMacro(selectedParameter, bankMacroReference, 0.0, false);
        int slot = processor.getCurrentSlot();
        if (!bankMacroReference.occupied[size_t(slot)])
            for (int s = 0; s < LSampler24AudioProcessor::slotCount; ++s)
                if (bankMacroReference.occupied[size_t(s)]) { slot = s; break; }
        bankMacroAmounts[size_t(parameter)] = bankMacroReference.values[size_t(slot)][size_t(parameter)];
    }
    else setSelectedParameterValue(0.0);
    refreshParameterGrid();
    lsampler::announceToActiveScreenReader(parameterSelector, "Macro reset. " + parameterCellText(selectedParameter));
}

void LSampler24AudioProcessorEditor::openGlobal()
{
    if (globalOpen || importBrowserActive || slotLibraryActive) return;

    globalReturnWasParameterPage = parameterPage;
    globalReturnSelectedParameter = selectedParameter;
    for (int i = 0; i < lsampler::globalParameterCount; ++i)
        globalSnapshot[static_cast<size_t>(i)] = processor.getGlobalOutputParameter(static_cast<lsampler::GlobalP>(i));

    globalOpen = true;
    parameterPage = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setVisible(true);
    parameterValue.setVisible(true);
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    selectedParameter = normalGridSize + juce::jlimit(0, lsampler::globalParameterCount - 1, globalGridIndex);
    refreshParameterGrid();
    resized();
    parameterSelector.setTitle("Global");
    parameterSelector.setEntryAccessibility();
    parameterSelector.grabKeyboardFocus();
    lsampler::announceToActiveScreenReader(parameterSelector, "Global");
}

void LSampler24AudioProcessorEditor::closeGlobal(bool accept)
{
    if (!globalOpen) return;

    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    globalGridIndex = juce::jlimit(0, lsampler::globalParameterCount - 1, selectedParameter - normalGridSize);

    if (!accept)
        for (int i = 0; i < lsampler::globalParameterCount; ++i)
            processor.setGlobalOutputParameter(static_cast<lsampler::GlobalP>(i), globalSnapshot[static_cast<size_t>(i)]);

    globalOpen = false;
    parameterSelector.setTitle("Grid");

    if (globalReturnWasParameterPage)
    {
        parameterPage = true;
        selectedParameter = juce::jlimit(0, normalGridSize + slotBankSendCount - 1, globalReturnSelectedParameter);
        refreshParameterGrid();
        resized();
        parameterSelector.setEntryAccessibility();
        parameterSelector.grabKeyboardFocus();
    }
    else
    {
        parameterPage = false;
        parameterSelector.setVisible(false);
        parameterValue.setVisible(false);
        for (auto& cell : slotCells) cell.setVisible(true);
        loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
        loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true); aboutButton.setVisible(true); advancedButton.setVisible(true);
        refreshSlotCells();
        resized();
        selectSlot(processor.getCurrentSlot(), true);
    }

    auto* source = juce::Component::getCurrentlyFocusedComponent();
    if (source == nullptr) source = &status;
    lsampler::announceToActiveScreenReader(*source, accept ? "Global confirmed" : "Global cancelled");
}

void LSampler24AudioProcessorEditor::selectParameter(int index, bool announce)
{
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    const int minimumIndex = globalOpen ? normalGridSize : 0;
    const int maximumIndex = globalOpen ? static_cast<int>(lsampler::grid.size()) - 1
        : normalGridSize + slotBankSendCount - 1;
    index = juce::jlimit(minimumIndex, maximumIndex, index);
    if(index==selectedParameter)return;
    selectedParameter = index;
    if (!globalOpen && !bankMacroOpen)
        processor.setSlotGridPosition(processor.getCurrentSlot(), selectedParameter);
    parameterSelector.setSelectedItemIndex(globalOpen ? selectedParameter - normalGridSize : selectedParameter,
        announce ? juce::sendNotificationSync : juce::dontSendNotification);
    syncSelectedAutomationIndicator();
    configureValueForSelectedParameter();
}

void LSampler24AudioProcessorEditor::focusValue()
{
    syncSelectedAutomationIndicator();
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

void LSampler24AudioProcessorEditor::focusMainParameterGrid()
{
    // Alt+L on the main window is the Slots locator, even in an empty bank.
    // Alt+L within a slot's parameter editor is the Grid locator.
    // Modal dialogs and browser pages intercept keys before reaching here.
    if (globalOpen)
        closeGlobal(true);

    if (!parameterPage)
        returnToCurrentSlotAndAnnounce();
    else
        focusParameterGrid();
}

void LSampler24AudioProcessorEditor::focusParameterActionButton(juce::TextButton& button)
{
    juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
    button.grabKeyboardFocus();
    if (auto* handler = button.getAccessibilityHandler())
        handler->grabFocus();
}

void LSampler24AudioProcessorEditor::announceSelectedValue()
{
    auto* source = juce::Component::getCurrentlyFocusedComponent();
    if (source == nullptr)
        source = &status;
    // Macro notifications are transient: do not store the changed offset in
    // the focused Grid/Value control's persistent accessibility description.
    if (bankMacroOpen) {
        lsampler::announceToActiveScreenReader(status, selectedParameterValueText());
        return;
    }
    lsampler::announceToActiveScreenReader(*source, selectedEntry().action!=Action::none?juce::String("Applied"):selectedParameterValueText());
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
    if (isSlotBankSend(selectedParameter))
    {
        const auto current = getSelectedParameterValue();
        const double amount = double(stepWidths[size_t(stepWidthIndex)]) * (coarse ? valuePageStep : 1);
        const auto next = juce::jlimit(0.0, 100.0, current + direction * amount);
        if (next == current) return;
        setSelectedParameterValue(next);
        refreshParameterGrid();
        announceSelectedValue();
        return;
    }
    if (isBankFXSend(selectedParameter))
    {
        const auto& d = lsampler::fx::parameters[size_t(bankFXParameter(selectedParameter))];
        const double amount = d.step * double(stepWidths[size_t(stepWidthIndex)]) * (coarse ? valuePageStep : 1);
        const auto current = getSelectedParameterValue();
        const auto next = juce::jlimit(d.minimum, d.maximum, current + direction * amount);
        if (std::abs(next - current) < 1.0e-9) return;
        setSelectedParameterValue(next);
        refreshParameterGrid();
        announceSelectedValue();
        return;
    }
    const auto& d=descriptor(selectedEntry());
    const double baseStep = d.step;
    const double multiplier = static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)])
                            * (coarse ? static_cast<double>(valuePageStep) : 1.0);
    const double step = selectedRateIsSynced()?.125:baseStep*multiplier;
    const double maximum = bankMacroOpen && !bankMacroUniform ? (d.maximum - d.minimum) : d.maximum;
    const double minimum = bankMacroOpen && !bankMacroUniform ? -(d.maximum - d.minimum)
                                                              : (selectedRateIsSynced()?.125:d.minimum);
    const auto current = getSelectedParameterValue();
    const auto next = juce::jlimit(minimum, maximum, current + direction * step);
    if (std::abs(next - current) < 1.0e-9)
        return;
    setSelectedParameterValue(next);
    refreshParameterGrid();
    announceSelectedValue();
}

void LSampler24AudioProcessorEditor::setSelectedParameterBoundary(bool maximum)
{
    if (isSlotBankSend(selectedParameter))
    {
        const auto boundary = maximum ? 100.0 : 0.0;
        if (getSelectedParameterValue() == boundary) return;
        setSelectedParameterValue(boundary);
        refreshParameterGrid();
        announceSelectedValue();
        return;
    }
    if (isBankFXSend(selectedParameter))
    {
        const auto& d = lsampler::fx::parameters[size_t(bankFXParameter(selectedParameter))];
        const double boundary = maximum ? d.maximum : d.minimum;
        if (getSelectedParameterValue() == boundary) return;
        setSelectedParameterValue(boundary);
        refreshParameterGrid();
        announceSelectedValue();
        return;
    }
    const auto& d=descriptor(selectedEntry());
    const double value = bankMacroOpen && !bankMacroUniform ?
        (maximum ? d.maximum - d.minimum : d.minimum - d.maximum) :
        (maximum ? d.maximum : (selectedRateIsSynced()?.125:d.minimum));
    if (std::abs(value - getSelectedParameterValue()) < 1.0e-9)
        return;
    setSelectedParameterValue(value);
    refreshParameterGrid();
    announceSelectedValue();
}

void LSampler24AudioProcessorEditor::setMainControlsEnabled(bool enabled)
{
    for (auto& cell : slotCells) cell.setEnabled(enabled);
    for (auto* control : std::array<juce::Component*, 13> {
             &loadSample, &loadSlot, &saveSlot, &loadBank, &saveBank, &help, &aboutButton, &advancedButton,
             &parameterSelector, &parameterValue, &sliceEditorButton, &sampleSetButton, &fxButton })
        control->setEnabled(enabled);
}

void LSampler24AudioProcessorEditor::openAbout()
{
    if (aboutOpen) return;
    aboutReturnFocus = juce::Component::getCurrentlyFocusedComponent();
    aboutOpen = true;
    setMainControlsEnabled(false);
    aboutInfo.setVisible(true);
    aboutProject.setVisible(true);
    aboutContact.setVisible(true);
    aboutClose.setVisible(true);
    aboutInfo.toFront(false);
    aboutProject.toFront(false);
    aboutContact.toFront(false);
    aboutClose.toFront(false);
    repaint();

    aboutInfo.setCaretPosition(0);
    juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
    aboutInfo.grabKeyboardFocus();
    if (auto* handler = aboutInfo.getAccessibilityHandler()) handler->grabFocus();
}

void LSampler24AudioProcessorEditor::closeAbout()
{
    if (!aboutOpen) return;
    aboutOpen = false;
    aboutInfo.setVisible(false);
    aboutProject.setVisible(false);
    aboutContact.setVisible(false);
    aboutClose.setVisible(false);
    setMainControlsEnabled(true);
    repaint();

    if (actionOpenedByShortcut)
    {
        aboutReturnFocus = nullptr;
        returnToActionOrigin(aboutButton);
        return;
    }
    auto* target = aboutReturnFocus.getComponent();
    if (target == nullptr || !target->isShowing() || !target->isEnabled()) target = &aboutButton;
    aboutReturnFocus = nullptr;
    juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
    target->grabKeyboardFocus();
    if (auto* handler = target->getAccessibilityHandler()) handler->grabFocus();
}

void LSampler24AudioProcessorEditor::openProjectPage()
{
    if (juce::URL(lsamplerProjectUrl).launchInDefaultBrowser())
        lsampler::announceToActiveScreenReader(aboutProject, "Opening LSampler-24 GitHub project page");
    else
        lsampler::announceToActiveScreenReader(aboutProject, "Cannot open the LSampler-24 GitHub project page");
}

void LSampler24AudioProcessorEditor::openContactEmail()
{
    const auto mailUrl = juce::String("mailto:") + lsamplerContactEmail;
    if (juce::URL(mailUrl).launchInDefaultBrowser())
        lsampler::announceToActiveScreenReader(aboutContact, "Opening email to " + juce::String(lsamplerContactEmail));
    else
        lsampler::announceToActiveScreenReader(aboutContact, "Cannot open the email application");
}

bool LSampler24AudioProcessorEditor::navigateAboutText(const juce::KeyPress& key)
{
    const auto keyCode = key.getKeyCode();
    const auto modifiers = key.getModifiers();
    const bool selecting = modifiers.isShiftDown();
    const bool byWordOrDocument = modifiers.isCtrlDown() || modifiers.isCommandDown();

    if (keyCode == juce::KeyPress::leftKey)  { aboutInfo.moveCaretLeft(byWordOrDocument, selecting); return true; }
    if (keyCode == juce::KeyPress::rightKey) { aboutInfo.moveCaretRight(byWordOrDocument, selecting); return true; }
    if (keyCode == juce::KeyPress::upKey)
    {
        if (byWordOrDocument && selecting) aboutInfo.moveCaretToTop(true);
        else aboutInfo.moveCaretUp(selecting);
        return true;
    }
    if (keyCode == juce::KeyPress::downKey)
    {
        if (byWordOrDocument && selecting) aboutInfo.moveCaretToEnd(true);
        else aboutInfo.moveCaretDown(selecting);
        return true;
    }
    if (keyCode == juce::KeyPress::homeKey)
    {
        if (byWordOrDocument) aboutInfo.moveCaretToTop(selecting);
        else aboutInfo.moveCaretToStartOfLine(selecting);
        return true;
    }
    if (keyCode == juce::KeyPress::endKey)
    {
        if (byWordOrDocument) aboutInfo.moveCaretToEnd(selecting);
        else aboutInfo.moveCaretToEndOfLine(selecting);
        return true;
    }
    if (keyCode == juce::KeyPress::pageUpKey) { aboutInfo.pageUp(selecting); return true; }
    if (keyCode == juce::KeyPress::pageDownKey) { aboutInfo.pageDown(selecting); return true; }
    return false;
}

bool LSampler24AudioProcessorEditor::isActionButton(const juce::Component* component) const
{
    return component == &loadSample || component == &loadSlot || component == &saveSlot
        || component == &loadBank || component == &saveBank || component == &help || component == &aboutButton || component == &advancedButton
        || component == &sliceEditorButton || component == &sampleSetButton || component == &fxButton;
}

int LSampler24AudioProcessorEditor::slotCellIndex(const juce::Component* component) const
{
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
        if (component == &slotCells[static_cast<size_t>(i)]) return i;
    return -1;
}

// TEST113: Execute a host-provided show/hide/toggle envelope command without
// opening a popup. Never invent a host state: refresh the host's menu for every
// keypress, and do nothing when the host doesn't provide a recognisable item.
// The VST3 context menu callback is owned by the DAW and can be called only
// while the menu is still alive, on the UI/message thread.
namespace
{
enum class HostEnvelopeCommand { none, show, hide, toggle, activate, deactivate };

HostEnvelopeCommand classifyHostEnvelopeItem(const juce::String& original)
{
    auto label = original.toLowerCase().trim();
    // Avoid automation-writing modes, Learn, and bulk operations (show ALL).
    if (label.isEmpty() || label.contains("all envelope") || label.contains("all automation")
        || label.contains("tutte") || label.contains("tutti")
        || label.contains("write") || label.contains("record") || label.contains("learn")
        || label.contains("mode") || label.contains("arm") || label.contains("modulation"))
        return HostEnvelopeCommand::none;

    // A few hosts add an ellipsis or an accelerator to the label.
    label = label.replace("&", "").replace("...", "").trim();
    const bool envelope = label.contains("envelope") || label.contains("automation lane")
                       || label.contains("automation track") || label.contains("automation for parameter")
                       || label.contains("parameter automation") || label.contains("inviluppo")
                       || label.contains("corsia di automazione") || label.contains("traccia di automazione")
                       || label.contains("pista de automatizaci") || label.contains("courbe d'automation")
                       || label.contains("automationsspur");
    if (!envelope) return HostEnvelopeCommand::none;

    if (label.startsWith("disable ") || label.startsWith("deactivate ") || label.startsWith("bypass ")
        || label.startsWith("disattiva ")) return HostEnvelopeCommand::deactivate;
    if (label.startsWith("enable ") || label.startsWith("activate ")
        || label.startsWith("attiva ")) return HostEnvelopeCommand::activate;
    if (label.startsWith("hide ") || label.startsWith("remove ")
        || label.startsWith("nascondi ") || label.startsWith("ocultar ")
        || label.startsWith("masquer ") || label.contains(" ausblenden"))
        return HostEnvelopeCommand::hide;
    if (label.startsWith("show ") || label.startsWith("display ")
        || label.startsWith("mostra ") || label.startsWith("visualizza ")
        || label.startsWith("mostrar ") || label.startsWith("afficher ")
        || label.contains(" anzeigen"))
        return HostEnvelopeCommand::show;
    if (label.startsWith("toggle ") || label.startsWith("commuta ")
        || label.startsWith("attiva/disattiva "))
        return HostEnvelopeCommand::toggle;
    return HostEnvelopeCommand::none;
}
}

// Query only the selected parameter, on the message thread. Never infer an
// envelope from the existence of an automatable parameter or an old keypress.
// The host may describe the action as Show/Hide, or provide a ticked toggle.
int LSampler24AudioProcessorEditor::getHostAutomationEnvelopeState(juce::AudioProcessorParameter* param) const
{
    if (processor.hasReaperEnvelopeApi())
        return processor.getReaperEnvelopeActive(param);

    if (param == nullptr) return -1;
    auto* host = getHostContext();
    if (host == nullptr) return -1;
    auto context = host->getContextMenuForParameter(param);
    if (context == nullptr) return -1;
    const auto menu = context->getEquivalentPopupMenu();
    juce::PopupMenu::MenuItemIterator iterator(menu, true);
    int matches = 0;
    int state = -1;
    while (iterator.next())
    {
        const auto& item = iterator.getItem();
        if (!item.isEnabled || !item.action || item.isSeparator || item.isSectionHeader)
            continue;
        const auto kind = classifyHostEnvelopeItem(item.text);
        if (kind == HostEnvelopeCommand::none) continue;
        ++matches;
        if (matches > 1) return -1; // ambiguous: don't label the wrong parameter
        if (kind == HostEnvelopeCommand::deactivate) state = 1;
        else if (kind == HostEnvelopeCommand::activate) state = 0;
        else state = -1; // show/hide means only visibility, not activity
    }
    return matches == 1 ? state : -1;
}

juce::AudioProcessorParameter* LSampler24AudioProcessorEditor::automationGridSelection() const
{
    if (!parameterPage) return nullptr;
    if (isSlotBankSend(selectedParameter))
        return processor.getSlotFXSendHostParameter(processor.getCurrentSlot(), slotBankSendIndex(selectedParameter));
    if (isBankFXSend(selectedParameter)) return processor.getFXHostParameter(bankFXParameter(selectedParameter));
    if (bankMacroOpen && bankMacroUniform) return nullptr;
    return processor.getAutomationGridParameter(processor.getCurrentSlot(),
        selectedParameter, selectedLoop, bankMacroOpen);
}

void LSampler24AudioProcessorEditor::syncSelectedAutomationIndicator(bool forceQuery)
{
    if (!parameterPage) return;
    auto* param = automationGridSelection();
    const bool parameterChanged = observedAutomationParameter != param;
    if (forceQuery || parameterChanged)
    {
        observedAutomationParameter = param;
        const int newState = getHostAutomationEnvelopeState(param);
        if (parameterChanged || observedAutomationState != newState)
        {
            observedAutomationState = newState;
            const int itemIndex = parameterSelector.getSelectedItemIndex();
            if (juce::isPositiveAndBelow(itemIndex, parameterSelector.getNumItems()))
            {
                const auto text = parameterCellText(selectedParameter);
                if (parameterSelector.getItemText(itemIndex) != text)
                    parameterSelector.changeItemText(itemIndex + 1, text);
            }
            // Value-mode line reading uses the same stable, compact suffix.
            parameterValue.setParameterAccessibilityName(selectedParameterName());
        }
    }
}

void LSampler24AudioProcessorEditor::syncSampleSetAutomationIndicator(bool forceQuery)
{
    if (!sampleSetActive || !sampleSetGridFocus) return;
    auto* param = processor.getAutomationSampleSetParameter(processor.getCurrentSlot(),
        sampleSetIndex, sampleSetPage, sampleSetField);
    const bool parameterChanged = param != observedSampleSetParameter;
    if (forceQuery || parameterChanged)
    {
        observedSampleSetParameter = param;
        const int newState = getHostAutomationEnvelopeState(param);
        if (parameterChanged || observedSampleSetState != newState)
        {
            observedSampleSetState = newState;
            const auto prefix = sampleSetPage == 2 ? "F3 Global Parameters. " : "F2 Sample Parameters. ";
            sampleSetCell.setBrowserText(juce::String(prefix) + sampleSetParameterText());
        }
    }
}

bool LSampler24AudioProcessorEditor::toggleHostAutomationEnvelope(juce::AudioProcessorParameter* param,
                                                                   juce::Component* source)
{
    if (param == nullptr) return false;

    auto announceCompact = [this, param, source]()
    {
        syncSelectedAutomationIndicator(true);
        syncSampleSetAutomationIndicator(true);
        if (sliceEditor) sliceEditor->refreshAutomationIndicator();
        juce::String line;
        if (sampleSetActive) line = sampleSetParameterText();
        else if (parameterPage) line = parameterCellText(selectedParameter);
        else if (sliceEditor) line = sliceEditor->automationAnnouncement();
        else line = param->getName(160);
        lsampler::announceToActiveScreenReader(source != nullptr ? *source : status, line);
    };

    if (processor.hasReaperEnvelopeApi())
    {
        // REAPER extension: ACTIVE off actually releases control of the FX
        // parameter; VISIBLE off alone (TEST115) only concealed the lane.
        if (processor.toggleReaperEnvelopeActive(param))
        {
            announceCompact();
            return true;
        }
        lsampler::announceToActiveScreenReader(source != nullptr ? *source : status,
            "Automation unavailable for this FX");
        return true; // Never fall through to the misleading Hide-only action.
    }

    auto* host = getHostContext();
    if (host == nullptr) return false;
    auto context = host->getContextMenuForParameter(param);
    if (context == nullptr) return false;
    const auto menu = context->getEquivalentPopupMenu();
    juce::PopupMenu::MenuItemIterator iterator(menu, true);
    std::function<void()> command;
    int matches = 0;
    while (iterator.next())
    {
        const auto& item = iterator.getItem();
        if (!item.isEnabled || !item.action || item.isSeparator || item.isSectionHeader) continue;
        const auto kind = classifyHostEnvelopeItem(item.text);
        // For all other DAWs prefer a REAL activation/deactivation action.
        // Never call Hide, which leaves automation active and the value locked.
        if (kind != HostEnvelopeCommand::activate && kind != HostEnvelopeCommand::deactivate) continue;
        if (++matches > 1) break;
        command = item.action;
    }
    if (matches != 1 || !command) return false;
    command(); // callback must run while host context is alive
    announceCompact();
    return true;
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
    if (fileUiBusy || processor.isFileTaskRunning())
        return key.getKeyCode() != juce::KeyPress::escapeKey;

    // A dedicated FX Grid is reachable from every ordinary sampler page.
    // Do not intercept typing in the modal file-name and rename dialogs.
    if (!fxOpen && !aboutOpen && !fileUiBusy && !processor.isFileTaskRunning()
        && key.getModifiers().isAltDown() && !key.getModifiers().isCtrlDown()
        && !key.getModifiers().isShiftDown() && !key.getModifiers().isCommandDown()
        && (juce::CharacterFunctions::toLowerCase(key.getTextCharacter()) == 'f'
            || juce::CharacterFunctions::toLowerCase(juce::juce_wchar(key.getKeyCode())) == 'f'))
    {
        openFX();return true;
    }
    if (fxOpen) return fxPanel.keyPressed(key);

    if (aboutOpen)
    {
        const auto aboutCharacter = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        const auto aboutCode = key.getKeyCode();
        const auto aboutMods = key.getModifiers();
        if (aboutCode == juce::KeyPress::escapeKey
            || (aboutMods.isAltDown() && !aboutMods.isCtrlDown() && aboutCharacter == 'c'))
        {
            closeAbout();
            return true;
        }

        if (aboutCode == juce::KeyPress::tabKey)
        {
            const bool backwards = aboutMods.isShiftDown();
            juce::Component* target = nullptr;
            if (!backwards)
            {
                if (source == &aboutInfo) target = &aboutProject;
                else if (source == &aboutProject) target = &aboutContact;
                else if (source == &aboutContact) target = &aboutClose;
                else target = &aboutInfo;
            }
            else
            {
                if (source == &aboutInfo) target = &aboutClose;
                else if (source == &aboutClose) target = &aboutContact;
                else if (source == &aboutContact) target = &aboutProject;
                else target = &aboutInfo;
            }
            juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
            target->grabKeyboardFocus();
            if (auto* handler = target->getAccessibilityHandler()) handler->grabFocus();
            return true;
        }

        if (source == &aboutInfo && navigateAboutText(key))
            return true;

        if (aboutCode == juce::KeyPress::returnKey && source == &aboutInfo)
        {
            closeAbout();
            return true;
        }

        if (aboutCode == juce::KeyPress::returnKey || key.getTextCharacter() == ' ')
        {
            if (source == &aboutProject) { openProjectPage(); return true; }
            if (source == &aboutContact) { openContactEmail(); return true; }
            if (source == &aboutClose) { closeAbout(); return true; }
        }
        return true;
    }

    // Modal editors own every key before main grid or host shortcut dispatch.
    if(sliceEditor!=nullptr){sliceEditor->keyPressed(key);return true;}
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (propertiesOpen)
    {
        auto announceProperty = [this]()
        {
            auto& column = propertiesConfigColumn ? propertiesPanel.configInfo : propertiesPanel.sampleInfo;
            const int row = propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow;
            propertiesPanel.highlightLine(propertiesConfigColumn, row);
            const auto line = propertiesPanel.lineText(propertiesConfigColumn, row);
            if (line.isNotEmpty())
                lsampler::announceToActiveScreenReader(column, line);
        };

        if (code == juce::KeyPress::escapeKey) { closeProperties(); return true; }

        if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            const int direction = code == juce::KeyPress::upKey ? -1 : 1;
            const int current = processor.getCurrentSlot();
            int found = -1;
            for (int candidate = current + direction;
                 candidate >= 0 && candidate < LSampler24AudioProcessor::slotCount;
                 candidate += direction)
            {
                if (processor.slotHasSample(candidate))
                {
                    found = candidate;
                    break;
                }
            }

            if (found >= 0)
            {
                processor.setCurrentSlot(found);
                refreshSlotCells();
                propertiesPanel.setInfo(processor.getCurrentSamplePropertiesText(), processor.getCurrentSlotPropertiesText());
                propertiesSampleRow = juce::jlimit(0, juce::jmax(0, propertiesPanel.lineCount(false) - 1), propertiesSampleRow);
                propertiesConfigRow = juce::jlimit(0, juce::jmax(0, propertiesPanel.lineCount(true) - 1), propertiesConfigRow);
                auto& targetColumn = propertiesConfigColumn ? propertiesPanel.configInfo : propertiesPanel.sampleInfo;
                targetColumn.grabKeyboardFocus();
                propertiesPanel.highlightLine(propertiesConfigColumn,
                    propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow);
                const auto line = propertiesPanel.lineText(propertiesConfigColumn,
                    propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow);
                lsampler::announceToActiveScreenReader(targetColumn,
                    processor.getSlotLabel(found) + (line.isNotEmpty() ? juce::String(". ") + line : juce::String()));
                refreshVisuals();
            }
            else
            {
                int loadedCount = 0;
                for (int slot = 0; slot < LSampler24AudioProcessor::slotCount; ++slot)
                    if (processor.slotHasSample(slot))
                        ++loadedCount;
                if (loadedCount <= 1)
                {
                    auto& targetColumn = propertiesConfigColumn ? propertiesPanel.configInfo : propertiesPanel.sampleInfo;
                    lsampler::announceToActiveScreenReader(targetColumn, "Only one loaded slot");
                }
            }
            return true;
        }

        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                int& row = propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow;
                const int count = propertiesPanel.lineCount(propertiesConfigColumn);
                if (count > 0)
                {
                    const int next = row + (code == juce::KeyPress::upKey ? -1 : 1);
                    if (next >= 0 && next < count)
                    {
                        row = next;
                        announceProperty();
                    }
                }
                return true;
            }
            if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
            {
                const bool targetConfig = code == juce::KeyPress::rightKey;
                if (targetConfig != propertiesConfigColumn)
                {
                    propertiesConfigColumn = targetConfig;
                    auto& target = propertiesConfigColumn ? propertiesPanel.configInfo : propertiesPanel.sampleInfo;
                    target.grabKeyboardFocus();
                    int& row = propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow;
                    row = juce::jlimit(0, juce::jmax(0, propertiesPanel.lineCount(propertiesConfigColumn) - 1), row);
                    announceProperty();
                }
                return true;
            }
        }

        if (code == juce::KeyPress::tabKey)
        {
            propertiesConfigColumn = !propertiesConfigColumn;
            auto& target = propertiesConfigColumn ? propertiesPanel.configInfo : propertiesPanel.sampleInfo;
            target.grabKeyboardFocus();
            int& row = propertiesConfigColumn ? propertiesConfigRow : propertiesSampleRow;
            row = juce::jlimit(0, juce::jmax(0, propertiesPanel.lineCount(propertiesConfigColumn) - 1), row);
            announceProperty();
            return true;
        }

        // Properties is modal: never let plain navigation keys escape to REAPER.
        if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
            || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
            return true;
        return true;
    }

    const int sourceSlot = slotCellIndex(source);
    const bool sourceIsSlot = sourceSlot >= 0;
    const bool sourceIsValueEditor = dynamic_cast<juce::TextEditor*>(source) != nullptr
                                  && parameterValue.isParentOf(source);

    if (slotLibraryActive)
    {
        const bool onExportLibrary = source == &exportLibraryButton;
        const bool onExportAllLibrary = source == &exportAllLibraryButton;
        const bool onImportLibrary = source == &importLibraryButton;
        const bool onLibraryAction = onExportLibrary || onExportAllLibrary || onImportLibrary;
        const auto focusSlotLibrary = [this]
        {
            slotLibraryCell.grabKeyboardFocus();
            juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
            {
                if (safeThis != nullptr && safeThis->slotLibraryActive)
                    safeThis->announceSlotLibraryEntry();
            });
        };

        // Identical filesystem commands in Alt+S / Alt+B and Save browsers.
        // Sample Set F1 browsing deliberately keeps its specialised behaviour.
        if (!slotLibraryForSampleSet && !slotLibraryForExport
            && !slotLibraryForExportFile && !slotLibraryForExportBank)
        {
            if (code == juce::KeyPress::F10Key && mods.isShiftDown()
                && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
            { showSlotLibraryContextMenu(); return true; }
            if (code == juce::KeyPress::F2Key && !mods.isCtrlDown()
                && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown())
            { renameSlotLibraryEntry(); return true; }
            if (code == juce::KeyPress::deleteKey && !mods.isCtrlDown()
                && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown())
            { deleteSlotLibraryEntry(); return true; }
        }

        if (code == juce::KeyPress::tabKey)
        {
            if (slotLibrarySaving) { focusSlotLibrary(); return true; }
            if (slotLibraryForSampleSet || slotLibraryForBank)
            {
                focusSlotLibrary();
                return true;
            }
            if (mods.isShiftDown())
            {
                if (onExportLibrary) focusSlotLibrary();
                else if (onExportAllLibrary) exportLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) exportAllLibraryButton.grabKeyboardFocus();
                else importLibraryButton.grabKeyboardFocus();
            }
            else
            {
                if (onExportLibrary) exportAllLibraryButton.grabKeyboardFocus();
                else if (onExportAllLibrary) importLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) focusSlotLibrary();
                else exportLibraryButton.grabKeyboardFocus();
            }
            return true;
        }

        if (code == juce::KeyPress::escapeKey)
        {
            const bool wasExport = slotLibraryForExport || slotLibraryForExportBank;
            const bool wasSave = slotLibrarySaving;
            const bool wasBank = slotLibraryForBank;
            const bool wasSampleSet = slotLibraryForSampleSet && sampleSetActive;
            leaveSlotLibraryBrowser(false);
            if (wasSampleSet) return true;
            if (wasExport) returnToAdvancedAndAnnounce();
            else returnToActionOrigin(wasSave ? (wasBank ? saveBank : saveSlot)
                                                   : (wasBank ? loadBank : loadSlot));
            return true;
        }

        if (onLibraryAction)
        {
            if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
            {
                if (onExportLibrary) chooseExportFolder();
                else if (onExportAllLibrary) chooseExportLibrary();
                else chooseImportLibrary();
                return true;
            }
            return true;
        }
        if (!slotLibrarySaving && !slotLibraryForSampleSet && !slotLibraryForBank && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown())
        {
            const auto ctrlChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (ctrlChar == 'c') { copySlotLibraryEntry(false); return true; }
            if (ctrlChar == 'x') { copySlotLibraryEntry(true); return true; }
            if (ctrlChar == 'v') { pasteSlotLibraryEntry(); return true; }
        }
        if (!slotLibrarySaving && !slotLibraryForSampleSet && !slotLibraryForBank && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            moveSlotLibraryDestination(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (!slotLibrarySaving && !slotLibraryForSampleSet && !slotLibraryForBank && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            shiftSelectSlotLibraryEntry(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (code == juce::KeyPress::upKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (slotLibraryEntryIndex > 0) selectSlotLibraryEntry(slotLibraryEntryIndex - 1, true);
            return true;
        }
        if (code == juce::KeyPress::downKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (slotLibraryEntryIndex + 1 < int(slotLibraryEntries.size())) selectSlotLibraryEntry(slotLibraryEntryIndex + 1, true);
            return true;
        }
        if (code == juce::KeyPress::pageUpKey)
        {
            const int next = juce::jmax(0, slotLibraryEntryIndex - 10);
            if (next != slotLibraryEntryIndex) selectSlotLibraryEntry(next, true);
            return true;
        }
        if (code == juce::KeyPress::pageDownKey)
        {
            const int next = juce::jmin(int(slotLibraryEntries.size()) - 1, slotLibraryEntryIndex + 10);
            if (next != slotLibraryEntryIndex) selectSlotLibraryEntry(next, true);
            return true;
        }
        if (code == juce::KeyPress::homeKey)
        {
            if (slotLibraryEntryIndex != 0) selectSlotLibraryEntry(0, true);
            return true;
        }
        if (code == juce::KeyPress::endKey)
        {
            const int last = int(slotLibraryEntries.size()) - 1;
            if (last >= 0 && slotLibraryEntryIndex != last) selectSlotLibraryEntry(last, true);
            return true;
        }
        const bool plainLeft = code == juce::KeyPress::leftKey
            && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown();
        const bool plainRight = code == juce::KeyPress::rightKey
            && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown();
        if (code == juce::KeyPress::backspaceKey || plainLeft)
        {
            if (slotLibraryDirectory == slotLibraryRoot) return true;
            const auto child = slotLibraryDirectory;
            auto parent = slotLibraryDirectory.getParentDirectory();
            if (!parent.isAChildOf(slotLibraryRoot) && parent != slotLibraryRoot) parent = slotLibraryRoot;
            slotLibraryDirectorySelectionMemory[parent.getFullPathName()] = child.getFullPathName();
            slotLibraryDirectory = parent;
            refreshSlotLibraryEntries();
            selectSlotLibraryEntry(slotLibraryEntryIndex, true);
            return true;
        }
        if (plainRight)
        {
            if (slotLibraryEntries.empty()) return true;
            const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
            if (!e.directory) return true;
            slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = e.file.getFullPathName();
            slotLibraryDirectory = e.file;
            refreshSlotLibraryEntries();
            selectSlotLibraryEntry(slotLibraryEntryIndex, true);
            return true;
        }
        if (!slotLibrarySaving && !slotLibraryForSampleSet && !slotLibraryForBank && code == juce::KeyPress::spaceKey && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        { toggleSlotLibrarySelection(); return true; }
        if (code == juce::KeyPress::spaceKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (slotLibraryForBank || slotLibrarySaving) return true; // Save mode and Banks have no audio preview.
            if (slotLibraryForSampleSet)
            {
                sampleSetBrowserPreviewEnabled = !sampleSetBrowserPreviewEnabled;
                saveSlotLibraryNavigationState(); // Persist On/Off even after closing the editor.
                if (!sampleSetBrowserPreviewEnabled)
                {
                    processor.requestImportPreviewStop();
                    slotLibraryPendingPreview = {};
                    slotLibraryPreviewDelayTicks = 0;
                    lsampler::announceToActiveScreenReader(slotLibraryCell, "Preview Off");
                }
                else
                {
                    // A selected folder/empty folder is silent, but Preview On
                    // stays armed for the next audio file.
                    updateSlotLibraryPreviewForSelection();
                    lsampler::announceToActiveScreenReader(slotLibraryCell, "Preview On");
                }
                return true;
            }
            if (slotLibraryEntries.empty()) return true;
            const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
            if (e.directory) return true;
            if (slotLibraryPreviewEnabled)
            {
                slotLibraryPreviewEnabled = false;
                importPreviewEnabled = false;
                saveImportPreviewPreferenceOnly();
                slotLibraryPendingPreview = {};
                slotLibraryPreviewDelayTicks = 0;
                processor.requestLibraryPreviewStop();
            }
            else
            {
                juce::String error;
                if (processor.prepareLibrarySlotPreview(e.file, error))
                {
                    slotLibraryPreviewEnabled = true;
                    importPreviewEnabled = true;
                    saveImportPreviewPreferenceOnly();
                    processor.requestLibraryPreviewToggle();
                }
                else lsampler::announceToActiveScreenReader(slotLibraryCell, error);
            }
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            // Save: Enter ALWAYS confirms the current directory, even when
            // the first child folder is merely highlighted by default.
            // Right opens a highlighted subfolder.  Left/Backspace goes up.
            if (slotLibrarySaving) { commitLibrarySave(); return true; }
            if (slotLibraryEntries.empty()) return true;
            const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
            if (e.directory)
            {
                if (slotLibraryForExport && !slotLibraryForExportFile) { chooseExportFolder(); return true; }
                slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = e.file.getFullPathName();
                slotLibraryDirectory = e.file; refreshSlotLibraryEntries(); selectSlotLibraryEntry(slotLibraryEntryIndex, true); return true;
            }
            if (slotLibraryForExportFile) { chooseExportFile(); return true; }
            if (slotLibraryForExportBank) { chooseExportBank(); return true; }
            if (slotLibraryForExport) { lsampler::announceToActiveScreenReader(slotLibraryCell, "Select a folder to export"); return true; }
            commitSlotLibrarySelection(); return true;
        }
        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            const auto typed = key.getTextCharacter();
            if (typed >= 33 && typed != 127)
            {
                cycleSlotLibraryEntryByInitial(typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }
        return true;
    }

    if (importBrowserActive)
    {
        if (code == juce::KeyPress::tabKey)
        {
            importBrowserCell.grabKeyboardFocus();
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            if (importRecentPathsMode) { leaveImportRecentPaths(); return true; }
            const bool returningToSampleSet = importForSampleSet && sampleSetActive;
            leaveImportBrowser(false, true);
            if (!returningToSampleSet) returnToActionOrigin(loadSample);
            return true;
        }
        if (importForSampleSet)
        {
            const bool recentShortcut = mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
                && juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code)) == 'r';
            const bool previewShortcut = code == juce::KeyPress::spaceKey
                && ((!mods.isShiftDown() && !mods.isAltDown() && !mods.isCtrlDown())
                    || (mods.isCtrlDown() && !mods.isShiftDown() && !mods.isAltDown()));
            const bool plainNavigation = !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown();
            const bool shiftedInitial = mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()
                && code >= 33 && code <= 126;
            if (!plainNavigation && !shiftedInitial && !recentShortcut && !previewShortcut) return true;
        }
        if (mods.isAltDown() && !mods.isCtrlDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            moveToImportFileWithSlice(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            moveImportDestinationSlot(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            shiftSelectImportEntry(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (code == juce::KeyPress::upKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (importEntryIndex > 0) selectImportEntry(importEntryIndex - 1, true);
            return true;
        }
        if (code == juce::KeyPress::downKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (importEntryIndex + 1 < int(importEntries.size())) selectImportEntry(importEntryIndex + 1, true);
            return true;
        }
        if (code == juce::KeyPress::pageUpKey)
        {
            const int next = juce::jmax(0, importEntryIndex - 10);
            if (next != importEntryIndex) selectImportEntry(next, true);
            return true;
        }
        if (code == juce::KeyPress::pageDownKey)
        {
            const int next = juce::jmin(int(importEntries.size()) - 1, importEntryIndex + 10);
            if (next != importEntryIndex) selectImportEntry(next, true);
            return true;
        }
        if (code == juce::KeyPress::homeKey && !mods.isCtrlDown())
        {
            if (importEntryIndex != 0) selectImportEntry(0, true);
            return true;
        }
        if (code == juce::KeyPress::endKey && !mods.isCtrlDown())
        {
            const int last = int(importEntries.size()) - 1;
            if (last >= 0 && importEntryIndex != last) selectImportEntry(last, true);
            return true;
        }
        const bool plainBrowserLeft = code == juce::KeyPress::leftKey
            && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown();
        const bool plainBrowserRight = code == juce::KeyPress::rightKey
            && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown();
        if (code == juce::KeyPress::backspaceKey || plainBrowserLeft)
        {
            if (importRecentPathsMode) { leaveImportRecentPaths(); return true; }
            if (importDriveList) return true;
            const auto childWeCameFrom = importDirectory;
            auto parent = importDirectory.getParentDirectory();
            if (parent != importDirectory)
            {
                importDirectory = parent;
                importDirectorySelectionMemory[parent.getFullPathName()] = childWeCameFrom.getFullPathName();
                addImportRecentPath(importDirectory);
                refreshImportEntries();
                selectImportEntry(importEntryIndex, true);
                saveImportSettings();
            }
            else
            {
                importDriveList = true;
                refreshImportEntries();
                int rootIndex = 0;
                for (int i = 0; i < static_cast<int>(importEntries.size()); ++i)
                    if (importEntries[static_cast<size_t>(i)].file == childWeCameFrom) { rootIndex = i; break; }
                selectImportEntry(rootIndex, true);
            }
            return true;
        }
        if (plainBrowserRight)
        {
            if (importEntries.empty()) return true;
            const auto& entry = importEntries[static_cast<size_t>(importEntryIndex)];
            if (!entry.directory) return true;
            if (!importDriveList && importDirectory.isDirectory())
                importDirectorySelectionMemory[importDirectory.getFullPathName()] = entry.file.getFullPathName();
            importDirectory = entry.file;
            importDriveList = false;
            importRecentPathsMode = false;
            addImportRecentPath(importDirectory);
            refreshImportEntries();
            selectImportEntry(importEntryIndex, true);
            saveImportSettings();
            return true;
        }
        const auto logicalKey = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(code));
        if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && logicalKey == 'r') { enterImportRecentPaths(); return true; }
        if (code == juce::KeyPress::spaceKey && mods.isCtrlDown() && !mods.isShiftDown() && !mods.isAltDown())
        {
            toggleImportPreviewPlayPause();
            return true;
        }
        if (code == juce::KeyPress::spaceKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            importPreviewEnabled = !importPreviewEnabled;
            if (importPreviewEnabled) updateImportPreviewForSelection();
            else processor.requestImportPreviewStop();
            lsampler::announceToActiveScreenReader(importBrowserCell, importPreviewEnabled ? "Preview On" : "Preview Off");
            return true;
        }
        if (code == juce::KeyPress::spaceKey && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()) { toggleImportFileSelection(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'q') { markImportSliceStart(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'w') { markImportSliceEnd(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'a') { toggleImportSliceEndMode(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'l') { toggleImportFileLoop(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'm') { markImportPlayStart(); return true; }
        if (mods.isAltDown() && !mods.isCtrlDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            navigateImportSlice(code == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }
        if (mods.isCtrlDown() && !mods.isAltDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            seekOutsideImportSlice(code == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }
        if (mods.isCtrlDown() && (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey))
        {
            prepareImportPreviewForCurrent();
            processor.requestImportPreviewLoop(0.0, 0.0, false);
            importCurrentSlicePlanIndex = -1;
            processor.requestImportPreviewSeek(code == juce::KeyPress::homeKey ? 0.0 : processor.getImportPreviewLengthSeconds());
            return true;
        }
        if (code == juce::KeyPress::deleteKey)
        {
            if (importRecentPathsMode) removeCurrentImportRecentPath();
            else deleteImportPlanItemAtCursor();
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            if (importEntries.empty()) return true;
            const auto& entry = importEntries[static_cast<size_t>(importEntryIndex)];
            if (entry.directory)
            {
                if (!importDriveList && importDirectory.isDirectory())
                    importDirectorySelectionMemory[importDirectory.getFullPathName()] = entry.file.getFullPathName();
                importDirectory = entry.file;
                importDriveList = false;
                importRecentPathsMode = false;
                addImportRecentPath(importDirectory);
                refreshImportEntries();
                selectImportEntry(importEntryIndex, true);
                saveImportSettings();
            }
            else commitImportPlan();
            return true;
        }
        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            const auto typed = key.getTextCharacter();
            if (typed >= 33 && typed != 127)
            {
                cycleImportEntryByInitial(typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }
        return true;
    }

    if (sampleSetActive && mods.isCtrlDown() && mods.isShiftDown()
        && !mods.isAltDown()
        && juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code)) == 'a')
    {
        auto* selected = processor.getAutomationSampleSetParameter(processor.getCurrentSlot(),
            sampleSetIndex, sampleSetPage, sampleSetField);
        if (toggleHostAutomationEnvelope(selected, &sampleSetCell))
            return true;
        const auto name = processor.touchAutomationSampleSetParameter(processor.getCurrentSlot(),
            sampleSetIndex, sampleSetPage, sampleSetField);
        lsampler::announceToActiveScreenReader(status, name.isNotEmpty()
            ? "Direct automation toggle unavailable. Last touched: " + name
            : "No automatable parameter selected");
        return true;
    }

    if (sampleSetActive)
    {
        // Space is Preview On/Off on every Sample Set page, including Value.
        // The enabled state outlives the audio voice and changes only on Space.
        if (code == juce::KeyPress::spaceKey && !mods.isCtrlDown() && !mods.isAltDown()
            && !mods.isShiftDown() && !mods.isCommandDown())
        {
            previewSampleSetEntry();
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            if (sampleSetValueFocus)
            {
                sampleSetValueFocus = false;
                refreshSampleSetCell(false);
                lsampler::announceToActiveScreenReader(sampleSetCell, "Grid. " + sampleSetParameterText());
            }
            else
            {
                leaveSampleSetEditor();
            }
            return true;
        }
        if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown() && ch == 'm')
        {
            leaveSampleSetEditor();
            return true;
        }
        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            if (code == juce::KeyPress::F1Key) { switchSampleSetPage(0); return true; }
            if (code == juce::KeyPress::F2Key) { switchSampleSetPage(1); return true; }
            if (code == juce::KeyPress::F3Key) { switchSampleSetPage(2); return true; }
            if (code == juce::KeyPress::tabKey)
            {
                switchSampleSetPage(sampleSetPage + (mods.isShiftDown() ? -1 : 1));
                return true;
            }
        }
        // TEST119: Ctrl+Up/Down changes the current sample on F2 and F3 only.
        // Keep the current page, selected parameter (sampleSetField), Grid/Value
        // mode and keyboard focus intact, so editing immediately targets the
        // corresponding parameter of the newly selected sample.  F1 is excluded.
        // The edges at Sample 1/16 are silent.
        if ((sampleSetPage == 1 || sampleSetPage == 2)
            && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            const int next = sampleSetIndex + (code == juce::KeyPress::upKey ? -1 : 1);
            if (juce::isPositiveAndBelow(next, LSampler24AudioProcessor::sampleSetSize))
            {
                sampleSetIndex = next;
                refreshSampleSetCell(false);
                lsampler::announceToActiveScreenReader(sampleSetCell,
                    "Sample " + juce::String(sampleSetIndex + 1) + ", " + sampleSetParameterText());
                previewSampleSetEntry(false);
            }
            return true;
        }
        if (sampleSetGridFocus && mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown() && ch == 'v')
        {
            sampleSetValueFocus = !sampleSetValueFocus;
            refreshSampleSetCell(false);
            lsampler::announceToActiveScreenReader(sampleSetCell,
                sampleSetValueFocus ? "Value. " + sampleSetParameterText() : "Grid. " + sampleSetParameterText());
            return true;
        }
        if (code == juce::KeyPress::returnKey && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown())
        {
            if (!sampleSetGridFocus)
            {
                enterSampleSetBrowser();
            }
            else
            {
                sampleSetValueFocus = !sampleSetValueFocus;
                refreshSampleSetCell(false);
                lsampler::announceToActiveScreenReader(sampleSetCell,
                    sampleSetValueFocus ? "Value. " + sampleSetParameterText() : "Grid. " + sampleSetParameterText());
            }
            return true;
        }

        if (sampleSetGridFocus && mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
        {
            if (code == juce::KeyPress::upKey)       { changeSampleSetValue(1, false); return true; }
            if (code == juce::KeyPress::downKey)     { changeSampleSetValue(-1, false); return true; }
            if (code == juce::KeyPress::leftKey)     { changeStepWidth(-1); return true; }
            if (code == juce::KeyPress::rightKey)    { changeStepWidth(1); return true; }
            if (code == juce::KeyPress::pageUpKey)   { changeSampleSetValue(1, true); return true; }
            if (code == juce::KeyPress::pageDownKey) { changeSampleSetValue(-1, true); return true; }
            if (code == juce::KeyPress::homeKey)     { setSampleSetBoundary(true); return true; }
            if (code == juce::KeyPress::endKey)      { setSampleSetBoundary(false); return true; }
        }

        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown())
        {
            if (!sampleSetGridFocus)
            {
                if (code == juce::KeyPress::upKey)   { moveSampleSetEntry(-1); return true; }
                if (code == juce::KeyPress::downKey) { moveSampleSetEntry(1); return true; }
                if (code == juce::KeyPress::homeKey)
                {
                    if (sampleSetIndex != 0)
                    {
                        sampleSetIndex = 0;
                        refreshSampleSetCell(true);
                        previewSampleSetEntry(false);
                    }
                    return true;
                }
                if (code == juce::KeyPress::endKey)
                {
                    const int last = LSampler24AudioProcessor::sampleSetSize - 1;
                    if (sampleSetIndex != last)
                    {
                        sampleSetIndex = last;
                        refreshSampleSetCell(true);
                        previewSampleSetEntry(false);
                    }
                    return true;
                }
                if (code == juce::KeyPress::deleteKey)
                {
                    processor.clearSampleSetEntry(processor.getCurrentSlot(), sampleSetIndex);
                    refreshSlotCells(); refreshSampleSetCell(true);
                    previewSampleSetEntry(false); // Silence an emptied entry without disabling Preview On.
                    return true;
                }
            }
            else if (sampleSetValueFocus)
            {
                if (code == juce::KeyPress::upKey)       { changeSampleSetValue(1, false); return true; }
                if (code == juce::KeyPress::downKey)     { changeSampleSetValue(-1, false); return true; }
                if (code == juce::KeyPress::pageUpKey)   { changeSampleSetValue(1, true); return true; }
                if (code == juce::KeyPress::pageDownKey) { changeSampleSetValue(-1, true); return true; }
                if (code == juce::KeyPress::homeKey)     { setSampleSetBoundary(true); return true; }
                if (code == juce::KeyPress::endKey)      { setSampleSetBoundary(false); return true; }
            }
            else
            {
                if (code == juce::KeyPress::upKey)       { moveSampleSetField(-1); return true; }
                if (code == juce::KeyPress::downKey)     { moveSampleSetField(1); return true; }
                if (code == juce::KeyPress::leftKey)     { moveSampleSetGridColumn(-1); return true; }
                if (code == juce::KeyPress::rightKey)    { moveSampleSetGridColumn(1); return true; }
                if (code == juce::KeyPress::pageUpKey)   { moveSampleSetGridColumn(-1); return true; }
                if (code == juce::KeyPress::pageDownKey) { moveSampleSetGridColumn(1); return true; }
                if (sampleSetPage == 2 && (code == juce::KeyPress::homeKey
                    || code == juce::KeyPress::endKey)) return true; // Single global parameter: silent borders.
                if (code == juce::KeyPress::homeKey)
                {
                    constexpr int rows = 8;
                    const int columnStart = (sampleSetField / rows) * rows;
                    if (sampleSetField != columnStart) { sampleSetField = columnStart; refreshSampleSetCell(true); }
                    return true;
                }
                if (code == juce::KeyPress::endKey)
                {
                    constexpr int rows = 8;
                    const int total = sampleSetPage == 2 ? 3 : 2;
                    const int columnStart = (sampleSetField / rows) * rows;
                    const int columnEnd = juce::jmin(total - 1, columnStart + rows - 1);
                    if (sampleSetField != columnEnd) { sampleSetField = columnEnd; refreshSampleSetCell(true); }
                    return true;
                }
            }
        }
        return true; // Sample Set is modal: never leak shortcuts to REAPER.
    }

    // Ctrl+Shift+A: execute DAW-provided envelope toggle, if available.
    // No popup; otherwise preserve the TEST111 last-touched fallback.
    if (mods.isCtrlDown() && mods.isShiftDown() && !mods.isAltDown()
        && juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code)) == 'a'
        && parameterPage && !sourceIsValueEditor)
    {
        if (bankMacroOpen && bankMacroUniform && !isBankFXSend(selectedParameter))
        {
            lsampler::announceToActiveScreenReader(status,
                "Set All is not automatable. Use Alt+U for Relative macro automation");
            return true;
        }
        auto* selected = automationGridSelection();
        if (toggleHostAutomationEnvelope(selected, source))
            return true;
        const auto name = isSlotBankSend(selectedParameter) ? slotBankSendName(selectedParameter)
            : isBankFXSend(selectedParameter) ? bankEntryName(selectedParameter)
            : processor.touchAutomationGridParameter(processor.getCurrentSlot(),
                selectedParameter, selectedLoop, bankMacroOpen);
        lsampler::announceToActiveScreenReader(status, name.isNotEmpty()
            ? "Direct automation toggle unavailable. Last touched: " + name
            : "No automatable parameter selected");
        return true;
    }

    // A temporary numeric editor must be closed before hiding or repopulating
    // its owning Slider. This avoids focus/use-after-free problems in REAPER.
    if (bankMacroOpen && sourceIsValueEditor &&
        (code == juce::KeyPress::escapeKey ||
         (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() &&
          (ch == 'k' || ch == 'u' || ch == 'g'))))
    {
        const bool discard = code == juce::KeyPress::escapeKey;
        if (!discard)
            if (auto* editor = dynamic_cast<juce::TextEditor*>(source))
                setSelectedParameterValue(parameterValue.getValueFromText(editor->getText()));
        const auto command = ch;
        parameterValue.hideTextBox(discard);
        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
             command, discard]
            {
                if (safeThis == nullptr || !safeThis->bankMacroOpen) return;
                if (discard || command == 'k') safeThis->closeBankMacro();
                else if (command == 'u') safeThis->toggleBankMacroUniform();
                else if (command == 'g') { safeThis->closeBankMacro(false); safeThis->openGlobal(); }
            });
        return true;
    }

    // Alt+K is a new independent editor for all occupied slots. It can be
    // entered from Slots or from an individual slot grid, without changing the
    // current slot, key ranges, sample sets or existing VST3 automation IDs.
    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown()
        && !mods.isCommandDown() && ch == 'k' && !sourceIsValueEditor)
    {
        queueBankMacroToggle();
        return true;
    }

    if (bankMacroOpen)
    {
        if (code == juce::KeyPress::escapeKey)
        {
            closeBankMacro();
            return true;
        }
        if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown()
            && !mods.isCommandDown() && ch == 'u')
        {
            if (!isBankFXSend(selectedParameter)) toggleBankMacroUniform();
            return true;
        }
        if (!mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown()
            && code == juce::KeyPress::backspaceKey)
        {
            resetSelectedBankMacro();
            return true;
        }
        if (source == &parameterSelector)
        {
            if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() &&
                (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey))
            {
                selectBankMacroEntry(code == juce::KeyPress::homeKey ? 0 : int(bankMacroEntries.size()) - 1, true);
                return true;
            }
            if (code == juce::KeyPress::tabKey && !mods.isCtrlDown() && !mods.isAltDown())
            {
                focusValue();
                return true;
            }
            if (mods.isAltDown() && mods.isShiftDown() && !mods.isCtrlDown()
                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
            {
                moveBankMacroPage(code == juce::KeyPress::rightKey ? 1 : -1, true);
                return true;
            }
            if (!mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
            {
                int target = bankMacroCursor;
                if (code == juce::KeyPress::upKey) --target;
                else if (code == juce::KeyPress::downKey) ++target;
                else if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
                    || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
                {
                    const auto* category = selectedEntry().category;
                    int begin = bankMacroCursor, end = bankMacroCursor;
                    while (begin > 0 && std::strcmp(bankEntry(bankMacroEntries[size_t(begin - 1)]).category, category) == 0) --begin;
                    while (end + 1 < int(bankMacroEntries.size()) && std::strcmp(bankEntry(bankMacroEntries[size_t(end + 1)]).category, category) == 0) ++end;
                    if (code == juce::KeyPress::homeKey) target = begin;
                    else if (code == juce::KeyPress::endKey) target = end;
                    else if (code == juce::KeyPress::pageUpKey) target = juce::jmax(begin, bankMacroCursor - 8);
                    else target = juce::jmin(end, bankMacroCursor + 8);
                }
                else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
                {
                    moveBankMacroPage(code == juce::KeyPress::rightKey ? 1 : -1, false);
                    return true;
                }
                else if (code == juce::KeyPress::returnKey)
                {
                    focusValue();
                    return true;
                }
                else if (code == juce::KeyPress::tabKey)
                {
                    focusValue();
                    return true;
                }
                else
                {
                    const auto typed = key.getTextCharacter();
                    const auto initial = juce::CharacterFunctions::toLowerCase(typed);
                    if (typed >= 33 && typed != 127)
                    {
                        const int direction = mods.isShiftDown() ? -1 : 1;
                        for (int distance = 1; distance <= int(bankMacroEntries.size()); ++distance)
                        {
                            const int cursor = (bankMacroCursor + int(bankMacroEntries.size()) +
                                                direction * distance % int(bankMacroEntries.size())) % int(bankMacroEntries.size());
                            const auto name = bankEntryName(bankMacroEntries[size_t(cursor)]);
                            if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase(name[0]) == initial)
                            { target = cursor; break; }
                        }
                    }
                    else return true;
                }
                if (target >= 0 && target < int(bankMacroEntries.size()))
                {
                    // Like the slot grid, Up/Down do not cross page borders.
                    if ((code != juce::KeyPress::upKey && code != juce::KeyPress::downKey) ||
                        std::strcmp(bankEntry(bankMacroEntries[size_t(target)]).category,
                                    selectedEntry().category) == 0)
                        selectBankMacroEntry(target, true);
                }
                return true;
            }
        }
        // Prevent slot-specific actions and file dialogs in this modal macro
        // surface; only Grid/Value, output Global, mode and fine-step remain.
        if (mods.isAltDown() && !mods.isCtrlDown()
            && ch != 'l' && ch != 'v' && ch != 'g'
            && code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey
            && code != juce::KeyPress::upKey && code != juce::KeyPress::downKey
            && code != juce::KeyPress::homeKey && code != juce::KeyPress::endKey
            && code != juce::KeyPress::pageUpKey && code != juce::KeyPress::pageDownKey)
            return true;
        if (mods.isCtrlDown() && !mods.isAltDown()) return true;
        if (code == juce::KeyPress::spaceKey) return true;
    }

    // Contextual Alt+L on Slots or on the selected Slot's Grid/Value.
    // The manual numeric text field
    // commits its value before leaving, further below in sourceIsValueEditor.
    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown()
        && !mods.isCommandDown() && ch == 'l' && !sourceIsValueEditor)
    {
        focusMainParameterGrid();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 't' && !parameterPage)
    {
        actionOpenedByShortcut = true;
        advancedButton.grabKeyboardFocus();
        if (auto* handler = advancedButton.getAccessibilityHandler()) handler->grabFocus();
        showAdvancedMenu();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'h')
    {
        actionOpenedByShortcut = true;
        showHelpLanguageMenu();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'a')
    {
        actionOpenedByShortcut = true;
        openAbout();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'm')
    {
        enterSampleSetEditor();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'p')
    {
        auto* announceSource = source != nullptr ? source : static_cast<juce::Component*>(&status);

        if (parameterPage && !globalOpen)
        {
            // Inside the main parameter grid the default is the opposite of the
            // slot page: Space auditions the configured slot. Alt+P temporarily
            // hands Space back to the DAW, and toggles back to audition mode.
            gridPreviewMode = !gridPreviewMode;
            if (gridPreviewMode)
            {
                processor.requestPreviewStart();
                lsampler::announceToActiveScreenReader(*announceSource, "Grid preview On");
            }
            else
            {
                processor.requestPreviewStop();
                lsampler::announceToActiveScreenReader(*announceSource, "DAW space On");
            }
        }
        else
        {
            // Slots and FX use exactly the same preview-mode implementation.
            toggleSlotPreview(*announceSource);
        }
        return true;
    }

    if(!mods.isCtrlDown()&&!mods.isShiftDown()&&!mods.isCommandDown()
       && mods.isAltDown()&&(ch=='e'||code=='E')) {
        openSliceEditor(false);return true;
    }

    const auto moveParameterPage = [this](int direction, bool firstParameter)
    {
        const int begin = categoryBegin(selectedParameter);
        const int end = categoryEnd(selectedParameter);
        const int row = firstParameter ? 0 : selectedParameter - begin;
        if (direction < 0 && begin > 0)
        {
            const int previous = categoryBegin(begin - 1);
            selectParameter(juce::jmin(previous + row, begin - 1), true);
        }
        else if (direction > 0 && end + 1 < (globalOpen ? static_cast<int>(lsampler::grid.size())
                                                   : slotBankSendFirst + slotBankSendCount))
        {
            const int next = end + 1;
            selectParameter(juce::jmin(next + row, categoryEnd(next)), true);
        }
    };

    if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown() && ch == 's')
    {
        actionOpenedByShortcut = true;
        if (mods.isShiftDown())
            chooseSaveBank();
        else
            chooseSaveSlot();
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown() && ch == 'g')
    {
        if (bankMacroOpen) closeBankMacro(false);
        if (globalOpen) closeGlobal(true); else openGlobal();
        return true;
    }

    if (globalOpen)
    {
        if (code == juce::KeyPress::escapeKey) { closeGlobal(false); return true; }
        if (code == juce::KeyPress::returnKey && !sourceIsValueEditor) { closeGlobal(true); return true; }
    }

    if (code == juce::KeyPress::spaceKey && parameterPage)
    {
        if (!globalOpen)
        {
            if (gridPreviewMode)
            {
                processor.requestPreviewToggle();
                return true;
            }
            return false; // Alt+P selected DAW Space while the main grid is open.
        }
        return true;
    }

    // Alt+Shift+Left/Right opens the NEXT/PREVIOUS page at its FIRST control.
    // Plain Left/Right preserves the current row in the destination page.
    // Ctrl+Left/Right is reserved for Sample Play Start scrubbing.
    if (parameterPage && !globalOpen && !bankMacroOpen && !sourceIsValueEditor && mods.isAltDown() && mods.isShiftDown()
        && !mods.isCtrlDown() && !mods.isCommandDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1, true);
        return true;
    }

    // Ctrl+Up/Down changes the current loaded slot while keeping the parameter grid open.
    // Empty slots are skipped, there is no wrap, and boundaries stay silent when
    // more than one loaded slot exists.  If this is the only loaded slot, announce
    // that fact so the user knows there is nowhere else to navigate.
    if (parameterPage && !globalOpen && !bankMacroOpen && !sourceIsValueEditor && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
        && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        const int current = processor.getCurrentSlot();
        const int direction = code == juce::KeyPress::upKey ? -1 : 1;
        int found = -1;
        for (int candidate = current + direction;
             candidate >= 0 && candidate < LSampler24AudioProcessor::slotCount;
             candidate += direction)
        {
            if (processor.slotHasSample(candidate))
            {
                found = candidate;
                break;
            }
        }

        if (found >= 0)
        {
            processor.setSlotGridPosition(current, selectedParameter);
            processor.setCurrentSlot(found);
            const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
            selectedParameter = juce::jlimit(0, normalGridSize + slotBankSendCount - 1,
                processor.getSlotGridPosition(found));
            parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
            refreshSlotCells();
            refreshParameterGrid();
            configureValueForSelectedParameter();
            // The Grid's Ctrl+Up/Down navigation does not use selectSlot().
            // Publish the actual destination as an unconditional Start command
            // so moving the Grid cursor plays exactly the newly chosen Slot.
            processor.requestPreviewStartForSlot(found);
            lsampler::announceToActiveScreenReader(parameterSelector, processor.getSlotLabel(found) + ". " + parameterCellText(selectedParameter));
        }
        else
        {
            int loadedCount = 0;
            for (int slot = 0; slot < LSampler24AudioProcessor::slotCount; ++slot)
                if (processor.slotHasSample(slot))
                    ++loadedCount;

            if (loadedCount <= 1)
                lsampler::announceToActiveScreenReader(parameterSelector, "Only one loaded slot");
        }
        return true;
    }

    // Ctrl+Left/Right scrubs Sample Play Start without exposing it in the Grid.
    // Use the same coarse multiplier as Page Up/Down so sample navigation is fast,
    // while Alt+Left/Right still selects the step-width multiplier.
    if (parameterPage && !globalOpen && !bankMacroOpen && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        const auto& d = lsampler::parameters[static_cast<size_t>(lsampler::P::sample_play_start)];
        const double step = d.step
                          * static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)])
                          * static_cast<double>(valuePageStep);
        const double current = processor.getSamplePlayStart();
        const double windowStart = processor.getSampleWindowStart();
        const double windowEnd = processor.getSampleWindowEnd();
        const double next = juce::jlimit(windowStart, windowEnd,
                                         current + (code == juce::KeyPress::rightKey ? step : -step));
        if (std::abs(next - current) >= 1.0e-9)
        {
            processor.setSamplePlayStart(next);
            processor.requestPreviewRestartIfPlaying();

            auto percentText = juce::String(next, 2);
            while (percentText.endsWithChar('0'))
                percentText = percentText.dropLastCharacters(1);
            if (percentText.endsWithChar('.'))
                percentText = percentText.dropLastCharacters(1);

            // Ctrl+Left/Right is a manual audition scrub: announce only the
            // compact position (for example "10%"), never the parameter
            // name. Sample Start/End edits keep their normal parameter
            // announcements and remain a separate interaction.
            lsampler::announceToActiveScreenReader(parameterSelector, percentText + "%");
        }
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'o' && !mods.isShiftDown()) { actionOpenedByShortcut = true; enterImportBrowser(); return true; }
        if (ch == 's' && !mods.isShiftDown()) { actionOpenedByShortcut = true; enterSlotLibraryBrowser(); return true; }
        if (ch == 'b' && !mods.isShiftDown()) { actionOpenedByShortcut = true; enterSlotLibraryBrowser(false, true); return true; }
        if (ch == 's' && mods.isShiftDown()) { actionOpenedByShortcut = true; chooseSaveSlot(); return true; }
        if (ch == 'b' && mods.isShiftDown()) { actionOpenedByShortcut = true; chooseSaveBank(); return true; }
        if (ch == 'v' && parameterPage && !sourceIsValueEditor) { focusValue(); return true; }
        // The old Alt+L loop toggle is retained as Alt+Shift+L; plain Alt+L
        // now locates Slots on the main page or Grid inside the slot editor.
        if (ch == 'l' && mods.isShiftDown() && parameterPage && !globalOpen && !bankMacroOpen)
        {
            for (int i = 0; i < static_cast<int>(lsampler::grid.size()); ++i)
                if (lsampler::grid[static_cast<size_t>(i)].parameter == int(lsampler::P::global_one_shot)
                    && std::strcmp(lsampler::grid[static_cast<size_t>(i)].category, "Sample Window") == 0)
                {
                    const double current = processor.getSlotParameter(i);
                    const bool loopWillBeOn = current != 0.0;
                    processor.setSlotParameter(i, loopWillBeOn ? 0.0 : 1.0);
                    refreshParameterGrid();
                    // ScreenReaderAnnouncer sets the source component's accessibility
                    // description.  Do not use the focused control here: when Alt+Shift+L
                    // is pressed from Slice Editor or Sample Set, that would replace the
                    // button's stable shortcut (Alt+E / Alt+M) with "Loop On/Off".
                    // The dedicated Status component can broadcast the notification
                    // without modifying the accessible label of a focused button.
                    lsampler::announceToActiveScreenReader(status, loopWillBeOn ? "Loop On" : "Loop Off");
                    return true;
                }
        }

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
        auto commitEditorValue = [this, source]()
        {
            if (auto* editor = dynamic_cast<juce::TextEditor*>(source))
            {
                const auto typed = parameterValue.getValueFromText(editor->getText());
                const auto& d = isSlotBankSend(selectedParameter) ? slotBankSendDescriptor()
                    : isBankFXSend(selectedParameter)
                    ? lsampler::fx::parameters[size_t(bankFXParameter(selectedParameter))]
                    : descriptor(selectedEntry());
                const double macroRange = d.maximum - d.minimum;
                setSelectedParameterValue(bankMacroOpen && !bankMacroUniform && !isBankFXSend(selectedParameter)
                    ? juce::jlimit(-macroRange, macroRange, typed)
                    : juce::jlimit(d.minimum, d.maximum, typed));
                refreshParameterGrid();
            }
        };

        auto closeEditorThen = [this](std::function<void()> next, bool discard)
        {
            parameterValue.hideTextBox(discard);
            juce::MessageManager::callAsync(
                [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
                 next = std::move(next)]() mutable
                {
                    if (safeThis != nullptr && next)
                        next();
                });
        };

        if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown()
            && !mods.isCommandDown() && ch == 'l')
        {
            // Finish the typed value, hide the transient editor and only then
            // transfer focus; this is the same safe sequencing used by Tab.
            commitEditorValue();
            closeEditorThen([this] { focusMainParameterGrid(); }, false);
            return true;
        }
        if (mods.isAltDown() && ch == 'v' && !mods.isCtrlDown() && !mods.isCommandDown())
        {
            commitEditorValue();
            closeEditorThen([this] { focusValue(); }, false);
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            commitEditorValue();
            closeEditorThen([this] { focusParameterGrid(); }, false);
            return true;
        }
        if (code == juce::KeyPress::tabKey)
        {
            commitEditorValue();
            if(mods.isShiftDown())closeEditorThen([this] { focusValue(); }, false);
            else closeEditorThen([this]
            {
                if (!globalOpen && !bankMacroOpen)
                    focusParameterActionButton(sliceEditorButton);
                else
                    focusParameterGrid();
            }, false);
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            closeEditorThen([this]
            {
                leaveSlotParameters();
                selectSlot(processor.getCurrentSlot(), true);
            }, true);
            return true;
        }
    }

    if (sourceIsValueEditor) return false;
    if (parameterPage)
    {
        // Main parameter page: Grid -> Value -> Edit -> Slice -> Sample Set
        // -> Grid. Reverse navigation is symmetrical with Shift+Tab.
        if (!globalOpen && (source == &sliceEditorButton || source == &sampleSetButton || source == &fxButton))
        {
            if (code == juce::KeyPress::tabKey)
            {
                if (source == &sliceEditorButton)
                {
                    if (mods.isShiftDown()) openValueEditor();
                    else focusParameterActionButton(sampleSetButton);
                }
                else if (source == &sampleSetButton)
                {
                    if (mods.isShiftDown()) focusParameterActionButton(sliceEditorButton);
                    else focusParameterActionButton(fxButton);
                }
                else
                {
                    if (mods.isShiftDown()) focusParameterActionButton(sampleSetButton);
                    else focusParameterGrid();
                }
                return true;
            }
            if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
            {
                if (source == &sliceEditorButton) openSliceEditor(false);
                else if (source == &sampleSetButton) enterSampleSetEditor();
                else openFX();
                return true;
            }
            if (code == juce::KeyPress::escapeKey) { focusParameterGrid(); return true; }
            // Keep stray arrow keys out of REAPER while one of these buttons
            // owns focus; the actual action is triggered by Enter or Space.
            return true;
        }
        // Shift+arrows: spatial navigation among ACTIVE host envelopes only.
        // Up/Down stay in the same category (grid column); Left/Right move
        // to the nearest active category, preferring the current row.
        if (source == &parameterSelector && !bankMacroOpen && !globalOpen
            && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()
            && !mods.isCommandDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
                || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            const int first = 0;
            const int last = slotBankSendFirst + slotBankSendCount - 1;
            const int begin = categoryBegin(selectedParameter);
            const int end = categoryEnd(selectedParameter);
            int target = -1;
            auto activeAt = [this](int index)
            {
                auto* param = index >= slotBankSendFirst
                    ? processor.getSlotFXSendHostParameter(processor.getCurrentSlot(), index - slotBankSendFirst)
                    : processor.getAutomationGridParameter(
                        processor.getCurrentSlot(), index, selectedLoop, false);
                return param != nullptr && getHostAutomationEnvelopeState(param) == 1;
            };
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
            {
                const int delta = code == juce::KeyPress::upKey ? -1 : 1;
                for (int i = selectedParameter + delta; i >= begin && i <= end; i += delta)
                    if (activeAt(i)) { target = i; break; }
            }
            else
            {
                const int delta = code == juce::KeyPress::leftKey ? -1 : 1;
                const int row = selectedParameter - begin;
                int nextColumn = delta < 0 ? begin - 1 : end + 1;
                while (nextColumn >= first && nextColumn <= last)
                {
                    const int columnBegin = categoryBegin(nextColumn);
                    const int columnEnd = categoryEnd(nextColumn);
                    const int preferred = juce::jmin(columnBegin + row, columnEnd);
                    // Find within this column before considering any later one.
                    for (int distance = 0; distance <= columnEnd - columnBegin; ++distance)
                    {
                        const int lower = preferred - distance;
                        const int upper = preferred + distance;
                        if (lower >= columnBegin && activeAt(lower)) { target = lower; break; }
                        if (distance != 0 && upper <= columnEnd && activeAt(upper)) { target = upper; break; }
                    }
                    if (target >= 0) break;
                    nextColumn = delta < 0 ? columnBegin - 1 : columnEnd + 1;
                }
            }
            if (target >= 0) selectParameter(target, true);
            return true;
        }
        // Alt+Shift+Up/Down switches the selected subloop; Shift+arrows
        // remain reserved for navigating active DAW automation envelopes.
        if(source == &parameterSelector && mods.isAltDown() && mods.isShiftDown()
            && !mods.isCtrlDown() && !mods.isCommandDown()
            && std::strcmp(selectedEntry().category,"Loops")==0
            && (code==juce::KeyPress::upKey||code==juce::KeyPress::downKey)) {
            const int next=juce::jlimit(0,9,selectedLoop+(code==juce::KeyPress::upKey?-1:1));
            if(next!=selectedLoop) {selectedLoop=next;refreshParameterGrid();
                lsampler::announceToActiveScreenReader(*source,parameterCellText(selectedParameter));}
            return true;
        }
        if(code==juce::KeyPress::backspaceKey) {
            if (isSlotBankSend(selectedParameter))
                processor.setSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(selectedParameter), 100.0);
            else processor.resetSlotParameter(selectedParameter,selectedLoop);
            refreshParameterGrid();announceSelectedValue();return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            leaveSlotParameters();
            selectSlot(processor.getCurrentSlot(), true);
            return true;
        }

        if (source == &parameterValue)
        {
            if (code == juce::KeyPress::tabKey)
            {
                if(mods.isShiftDown())focusParameterGrid();else openValueEditor();
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
            return true;
        }

        if (source == &parameterSelector)
        {
            parameterSelector.setLineReadingMode();

            if(code==juce::KeyPress::tabKey)
            {
                if(mods.isShiftDown())
                {
                    if (!globalOpen) focusParameterActionButton(fxButton);
                    else openValueEditor();
                }
                else focusValue();
                return true;
            }
            if (code == juce::KeyPress::returnKey) {
                if(selectedEntry().action!=Action::none){setSelectedParameterValue(1);refreshParameterGrid();announceSelectedValue();}
                else focusValue();
                return true;
            }
            if (mods.isCtrlDown() && code == juce::KeyPress::homeKey)
            {
                const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
                selectParameter(globalOpen ? normalGridSize : 0, true);
                return true;
            }
            if (mods.isCtrlDown() && code == juce::KeyPress::endKey)
            {
                const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
                selectParameter(globalOpen ? static_cast<int>(lsampler::grid.size()) - 1
                    : normalGridSize + slotBankSendCount - 1, true);
                return true;
            }
            if (code == juce::KeyPress::homeKey) { selectParameter(categoryBegin(selectedParameter), true); return true; }
            if (code == juce::KeyPress::endKey) { selectParameter(categoryEnd(selectedParameter), true); return true; }
            if (code == juce::KeyPress::pageUpKey) { selectParameter(juce::jmax(categoryBegin(selectedParameter), selectedParameter - 8), true); return true; }
            if (code == juce::KeyPress::pageDownKey) { selectParameter(juce::jmin(categoryEnd(selectedParameter), selectedParameter + 8), true); return true; }
            if (code == juce::KeyPress::upKey)
            {
                if (selectedParameter > categoryBegin(selectedParameter)) selectParameter(selectedParameter - 1, true);
                return true;
            }
            if (code == juce::KeyPress::downKey)
            {
                if (selectedParameter < categoryEnd(selectedParameter))
                    selectParameter(selectedParameter + 1, true);
                return true;
            }
            if (!globalOpen && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown()
                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
            {
                moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1, false);
                return true;
            }
            // Only actual printable characters trigger type-ahead. Function and
            // navigation keys may have ASCII-valued virtual key codes; using
            // getKeyCode() here mistakenly searched for P/R/S/T/V/0.
            if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown()) {
                const auto initial = parameterSearchCharacter(key);
                if (initial != 0) {
                    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
                    const int first = globalOpen ? normalGridSize : 0;
                    const int count = globalOpen ? lsampler::globalParameterCount : normalGridSize + slotBankSendCount;
                    const int local = selectedParameter - first;
                    const int direction = mods.isShiftDown() ? -1 : 1;
                    for (int distance = 1; distance <= count; ++distance) {
                        int relative = (local + direction * distance) % count;
                        if (relative < 0) relative += count;
                        const int next = first + relative;
                        const auto name=isSlotBankSend(next) ? slotBankSendName(next)
                            : displayedGridParameterName(lsampler::grid[size_t(next)]);
                        if (name.isNotEmpty()
                            && (juce::CharacterFunctions::toLowerCase(name[0]) == initial
                                || (initial == '0' && name.equalsIgnoreCase("Zero Crossing")))) {
                            selectParameter(next, true);
                            break;
                        }
                    }
                    return true;
                }
            }
        }
        return true; // The parameter surface owns unmatched host shortcuts, including Ctrl keys.
    }

    if (isActionButton(source))
    {
        if ((source == &sliceEditorButton || source == &sampleSetButton || source == &fxButton)
            && (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey))
        {
            if (source == &sliceEditorButton) openSliceEditor(false);
            else if (source == &sampleSetButton) enterSampleSetEditor();
            else openFX();
            return true;
        }
        if (code == juce::KeyPress::tabKey)
        {
            std::array<juce::Component*, 11> buttons {
                &loadSample, &loadSlot, &saveSlot, &loadBank, &saveBank,
                &sliceEditorButton, &sampleSetButton, &fxButton, &help, &aboutButton, &advancedButton
            };
            const bool backwards = mods.isShiftDown();
            int index = -1;
            for (int i = 0; i < static_cast<int>(buttons.size()); ++i)
                if (source == buttons[static_cast<size_t>(i)]) { index = i; break; }

            juce::Component* target = nullptr;
            if (index >= 0)
            {
                if (!backwards && index == static_cast<int>(buttons.size()) - 1)
                    target = &slotCells[static_cast<size_t>(processor.getCurrentSlot())];
                else if (backwards && index == 0)
                    target = &slotCells[static_cast<size_t>(processor.getCurrentSlot())];
                else
                    target = buttons[static_cast<size_t>(index + (backwards ? -1 : 1))];
            }

            if (target != nullptr)
            {
                juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
                target->grabKeyboardFocus();
                if (auto* handler = target->getAccessibilityHandler()) handler->grabFocus();
            }
            return true;
        }

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
            || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
            || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
            || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
            return true;
        return false;
    }

    if (!sourceIsSlot) return false;

    // Properties belongs to the current slot, not to arbitrary editor surfaces.
    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && code == juce::KeyPress::returnKey)
    {
        openProperties();
        return true;
    }

    // With Alt+P audition mode enabled, Space owns slot preview. With the mode
    // disabled it intentionally falls through to the host so REAPER keeps its
    // normal Play/Stop shortcut.
    if (code == juce::KeyPress::spaceKey && !mods.isAltDown() && !mods.isCtrlDown()
        && !mods.isShiftDown() && !mods.isCommandDown())
    {
        return toggleSlotPreviewIfEnabled();
    }

    const int currentSlot = processor.getCurrentSlot();
    auto announceCurrentSlot = [this, currentSlot]()
    {
        refreshSlotCells();
        auto& cell = slotCells[static_cast<size_t>(currentSlot)];
        lsampler::announceToActiveScreenReader(cell, processor.getSlotLabel(currentSlot));
    };

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'r')
        {
            renameCurrentSlot();
            return true;
        }
        if (ch == 'c')
        {
            processor.copyCurrentSlot();
            lsampler::announceToActiveScreenReader(slotCells[static_cast<size_t>(currentSlot)],
                                                  "Copied " + processor.getSlotLabel(currentSlot));
            return true;
        }
        if (ch == 'x')
        {
            processor.cutCurrentSlot();
            announceCurrentSlot();
            return true;
        }
        if (ch == 'v')
        {
            if (!processor.hasCopiedSlot()) return true;
            auto paste = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), announceCurrentSlot]
            {
                if (safeThis != nullptr && safeThis->processor.pasteCurrentSlot())
                    announceCurrentSlot();
            };
            if (processor.isSlotOccupied(currentSlot))
                confirmOverwrite("Slot " + juce::String(currentSlot + 1) + ", "
                    + replacementSlotName(currentSlot) + ", is occupied. Replace it with the copied slot? Press Y or N.",
                    std::move(paste), [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                    {
                        if (safeThis != nullptr) safeThis->returnToCurrentSlotAndAnnounce();
                    });
            else paste();
            return true;
        }
    }

    if (code == juce::KeyPress::deleteKey)
    {
        if (mods.isAltDown())
        {
            processor.clearBank();
            // The next bank starts at the first slot, not the previously selected one.
            selectSlot(0, true);
            lsampler::announceToActiveScreenReader(status, processor.getSlotLabel(0) + ". Bank cleared");
        }
        else
        {
            processor.clearCurrentSlot();
            refreshSlotCells();
            // Speak the result without attaching it to the slot accessibility
            // description: NVDA+Up and later arrow navigation stay concise.
            lsampler::announceToActiveScreenReader(status,
                processor.getSlotLabel(currentSlot) + ". Slot " + juce::String(currentSlot + 1) + " cleared");
        }
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        if (!processor.slotHasSample(currentSlot))
        {
            lsampler::announceToActiveScreenReader(slotCells[static_cast<size_t>(currentSlot)], "Empty slot");
            return true;
        }
        enterSlotParameters();
        return true;
    }
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

void LSampler24AudioProcessorEditor::setFileUiBusy(bool busy)
{
    if (fileUiBusy == busy) return;
    fileUiBusy = busy;
    if (busy) {
        fileReturnFocus = juce::Component::getCurrentlyFocusedComponent();
        fileDisabledComponents.clear();
        for (int i = 0; i < getNumChildComponents(); ++i) {
            auto* child = getChildComponent(i);
            if (child == &status) continue;
            fileDisabledComponents.emplace_back(child, child->isEnabled());
            child->setEnabled(false);
        }
        status.setVisible(true);
        status.setWantsKeyboardFocus(true);
        // The status is the focused, readable line for NVDA+Up. No redundant
        // "Status" heading while the task is in progress.
        status.setTitle({});
        status.setDescription({});
        status.setName(fileTaskTitle + ", 0 percent");
        if (isShowing() && (fileReturnFocus == nullptr || fileReturnFocus == this || isParentOf(fileReturnFocus.getComponent())))
            status.grabKeyboardFocus();
    } else {
        for (auto& child : fileDisabledComponents)
            if (child.first != nullptr) child.first->setEnabled(child.second);
        fileDisabledComponents.clear();
        status.setWantsKeyboardFocus(false);
        status.setTitle("Status");
        status.setDescription({});
        status.setName("Status");
        lastUiRevision = 0;
    }
}

void LSampler24AudioProcessorEditor::runFileTask(const juce::String& title,
    LSampler24AudioProcessor::FileTask task, std::function<void(const FileTaskResult&, bool)> completion)
{
    if (fileUiBusy || processor.isFileTaskRunning()) {
        lsampler::announceToActiveScreenReader(status, "File operation in progress"); return;
    }
    auto safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this);
    fileTaskOwned = true;
    fileTaskTitle = title;
    lastFileTaskProgress = 0;
    lastProgressAnnouncementMs = juce::Time::getMillisecondCounter();
    setFileUiBusy(true);
    status.setText(title + ", 0 percent", juce::dontSendNotification);
    status.setName(title + ", 0 percent");
    // One initial message; subsequent speech is rate-limited to four seconds.
    lsampler::announceToActiveScreenReader(status, title + ", 0 percent");
    const bool started = processor.startFileTask(std::move(task), [safeThis, completion](FileTaskResult result) {
        if (safeThis == nullptr) return;
        const bool restoreFocus = safeThis->hasKeyboardFocus(true);
        safeThis->fileTaskOwned = false;
        safeThis->setFileUiBusy(false);
        if (result.message.isEmpty()) result.message = result.ok ? "File operation completed" : "File operation stopped";
        safeThis->status.setText(result.message, juce::dontSendNotification);
        safeThis->refreshSlotCells();
        if (safeThis->parameterPage) safeThis->refreshParameterGrid();
        if (restoreFocus && safeThis->fileReturnFocus != nullptr && safeThis->fileReturnFocus->isShowing())
            safeThis->fileReturnFocus->grabKeyboardFocus();
        if (completion) completion(result, restoreFocus);
        safeThis->refreshVisuals();
        safeThis->lastUiRevision = safeThis->processor.getUiRevision();
        if (restoreFocus) lsampler::announceToActiveScreenReader(*safeThis, result.message);
    });
    if (!started) { fileTaskOwned = false; setFileUiBusy(false); }
}

void LSampler24AudioProcessorEditor::refreshMeters()
{
    if (importBrowserActive || slotLibraryActive || sampleSetActive || sliceEditor || !isShowing()) return;
    bool changed = false;
    for (int ch = 0; ch < 2; ++ch) {
        const float old = masterOutput.peaks[size_t(ch)];
        float next = std::max(processor.consumeVisualPeak(ch), old * .75f);
        if (next < .00001f) next = 0.0f;
        changed = changed || next != old;
        masterOutput.peaks[size_t(ch)] = next;
    }
    if (changed) masterOutput.repaint();
}

void LSampler24AudioProcessorEditor::timerCallback()
{
    if (fxOpen) fxPanel.refreshIfAutomated();

    if (processor.isFileTaskRunning()) {
        if (!fileUiBusy) {
            fileTaskTitle = "File operation";
            lastFileTaskProgress = -1;
            lastProgressAnnouncementMs = juce::Time::getMillisecondCounter();
            setFileUiBusy(true);
        }
        const int percent = juce::jlimit(0, 100, int(std::round(processor.getFileTaskProgress() * 100.0)));
        const auto progressText = fileTaskTitle + ", " + juce::String(percent) + " percent";
        if (percent != lastFileTaskProgress)
        {
            lastFileTaskProgress = percent;
            status.setText(progressText, juce::dontSendNotification);
            status.setName(progressText);
        }
        // NVDA+Up always reads the current percentage from the focused label,
        // even between spoken updates. Never interrupt REAPER when this editor
        // isn't the focused window. Repeat every 4 s, including slow/long WAVs.
        const auto now = juce::Time::getMillisecondCounter();
        if (isShowing() && status.hasKeyboardFocus(true)
            && now - lastProgressAnnouncementMs >= 4000u)
        {
            lastProgressAnnouncementMs = now;
            lsampler::announceToActiveScreenReader(status, progressText);
        }
        return;
    }
    if (fileTaskOwned) return; // The queued completion owns the UI transition.
    if (fileUiBusy) { // An editor reopened while the processor's worker was running.
        const bool focus = hasKeyboardFocus(true);
        setFileUiBusy(false);
        if (focus) returnToCurrentSlotAndAnnounce();
        const auto result = processor.getLastFileTaskResult();
        refreshVisuals(); lastUiRevision = processor.getUiRevision();
        status.setText(result.message, juce::dontSendNotification);
        if (focus) lsampler::announceToActiveScreenReader(*this, result.message);
    }
    const bool editorFocused = hasKeyboardFocus(true);
    if (editorFocused && ++automationRefreshTicks >= 10)
    {
        automationRefreshTicks = 0;
        if (sampleSetActive) syncSampleSetAutomationIndicator(true);
        else if (sliceEditor) sliceEditor->refreshAutomationIndicator();
        else if (parameterPage) syncSelectedAutomationIndicator(true);
    }
    if (editorFocused)
    {
        editorFocusSeen = true;
        editorHadKeyboardFocus = true;

        if (pendingAdvancedOpen)
        {
            pendingAdvancedOpen = false;
            advancedButton.grabKeyboardFocus();

            // Let JUCE/REAPER finish the focus transition before creating the
            // popup.  This makes its first item the real keyboard and NVDA
            // focus target instead of leaving focus in the FX Chain.
            juce::MessageManager::callAsync(
                [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                {
                    if (safeThis != nullptr && safeThis->hasKeyboardFocus(true))
                        safeThis->showAdvancedMenu();
                });
        }
    }
    else if (editorFocusSeen && editorHadKeyboardFocus)
    {
        // Leaving the plugin window must never leave an audition running in the DAW.
        // Child-to-child focus moves still report true above, so this only fires when
        // keyboard focus actually leaves the editor (for example back to REAPER).
        processor.requestPreviewStop();
        processor.requestImportPreviewStop();
        processor.requestLibraryPreviewStop();
        processor.stopSlicePreview();
        editorHadKeyboardFocus = false;
    }
    if (!isShowing()) return;
    const auto revision = processor.getUiRevision();
    if (revision != lastUiRevision) {
        lastUiRevision = revision;
        if (sliceEditor) sliceEditor->refreshVisuals();
        else {
            refreshVisuals();
            status.setText(processor.getSampleStatus(), juce::dontSendNotification);
            refreshSlotCells();
            if (sampleSetActive) refreshSampleSetCell(false);
            else if (parameterPage) refreshParameterGrid();
        }
    }
    refreshMeters();
    if (slotLibraryActive && slotLibraryPreviewDelayTicks > 0) {
        --slotLibraryPreviewDelayTicks;
        if (slotLibraryPreviewDelayTicks == 0 && slotLibraryPendingPreview.existsAsFile()) {
            const auto file = slotLibraryPendingPreview;
            slotLibraryPendingPreview = {};
            juce::String error;
            if (slotLibraryForSampleSet && sampleSetBrowserPreviewEnabled) {
                // Always start the newly selected raw file, even when the
                // previous one ended by itself. Space alone turns this mode off.
                if (processor.prepareImportPreview(file, error))
                    processor.requestImportPreviewToggle();
                else
                    lsampler::announceToActiveScreenReader(slotLibraryCell, error);
            }
            else if (!slotLibraryForSampleSet && slotLibraryPreviewEnabled) {
                if (processor.prepareLibrarySlotPreview(file, error))
                    processor.requestLibraryPreviewToggle();
            }
        }
    }
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotCells();
    if (parameterPage) refreshParameterGrid();
}

void LSampler24AudioProcessorEditor::enterSampleSetEditor()
{
    if (sampleSetActive || importBrowserActive || slotLibraryActive || globalOpen || sliceEditor != nullptr) return;
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    sampleSetReturnWasParameterPage = parameterPage;
    sampleSetReturnFocus = juce::Component::getCurrentlyFocusedComponent();
    sampleSetIndex = juce::jlimit(0, LSampler24AudioProcessor::sampleSetSize - 1, sampleSetIndex);
    sampleSetField = 0;
    sampleSetPage = 0;
    sampleSetPreviewEnabled = false;
    sampleSetGridFocus = false;
    sampleSetValueFocus = false;
    sampleSetActive = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    sampleSetCell.setVisible(true); sampleSetCell.setWantsKeyboardFocus(true);
    refreshSampleSetCell(false);
    resized();
    sampleSetCell.grabKeyboardFocus();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr && safeThis->sampleSetActive && !safeThis->importBrowserActive)
            lsampler::announceToActiveScreenReader(safeThis->sampleSetCell, safeThis->sampleSetCell.getBrowserText());
    });
}

// The dialog is a genuine modal accessible window. Y, N and Escape have
// independent native accelerators so confirmation never relies on a speech-only
// prompt or an invisible key handler on REAPER's FX Chain.
void LSampler24AudioProcessorEditor::confirmOverwrite(
    const juce::String& message, std::function<void()> confirmed,
    std::function<void()> cancelled, const juce::String& title)
{
    auto* warning = new juce::AlertWindow(title, message,
                                          juce::MessageBoxIconType::WarningIcon);
    warning->addButton("Yes", 1, juce::KeyPress('y'));
    warning->addButton("No", 0, juce::KeyPress('n'), juce::KeyPress(juce::KeyPress::escapeKey));
    warning->setAccessible(true);
    warning->setName(title);
    warning->setTitle(title);
    if (auto* noButton = warning->getButton("No"))
        noButton->setWantsKeyboardFocus(true);
    warning->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
         yes = std::move(confirmed), no = std::move(cancelled)](int result) mutable
        {
            if (safeThis == nullptr) return;
            if (result == 1) { if (yes) yes(); }
            else { if (no) no(); }
        }), true);
    if (auto* noButton = warning->getButton("No"))
        noButton->grabKeyboardFocus();
}

juce::String LSampler24AudioProcessorEditor::replacementSlotName(int slot) const
{
    auto name = processor.getSlotName(slot).trim();
    return name.isNotEmpty() ? name : juce::String("Slot ") + juce::String(slot + 1);
}

int LSampler24AudioProcessorEditor::occupiedBankSlotCount() const
{
    int count = 0;
    for (int slot = 0; slot < LSampler24AudioProcessor::slotCount; ++slot)
        if (processor.isSlotOccupied(slot)) ++count;
    return count;
}

void LSampler24AudioProcessorEditor::switchSampleSetPage(int requestedPage)
{
    sampleSetPage = (requestedPage + 3) % 3;
    sampleSetGridFocus = sampleSetPage != 0;
    sampleSetValueFocus = false;
    sampleSetField = sampleSetPage == 2 ? 2 : 0;
    refreshSampleSetCell(false);
    const auto label = sampleSetPage == 0 ? juce::String("F1, Samples. ")
                     : sampleSetPage == 1 ? juce::String("F2, Sample Parameters. ")
                                          : juce::String("F3, Global Parameters. ");
    lsampler::announceToActiveScreenReader(sampleSetCell, label + sampleSetCell.getBrowserText());
}

void LSampler24AudioProcessorEditor::leaveSampleSetEditor()
{
    if (!sampleSetActive) return;
    sampleSetPreviewEnabled = false;
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    auto returnFocus = sampleSetReturnFocus;
    sampleSetReturnFocus = nullptr;
    sampleSetActive = false;
    sampleSetCell.setVisible(false); sampleSetCell.setWantsKeyboardFocus(false);
    if (sampleSetReturnWasParameterPage)
    {
        parameterPage = true;
        for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
        loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
        loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
        parameterSelector.setVisible(true); parameterValue.setVisible(true);
        refreshParameterGrid();
        resized();
        if (returnFocus != nullptr && returnFocus->isShowing() && returnFocus->isEnabled())
            returnFocus->grabKeyboardFocus();
        else
        {
            parameterSelector.setEntryAccessibility();
            parameterSelector.grabKeyboardFocus();
            lsampler::announceToActiveScreenReader(parameterSelector, parameterCellText(selectedParameter));
        }
    }
    else
    {
        parameterPage = false;
        parameterSelector.setVisible(false); parameterValue.setVisible(false);
        for (auto& cell : slotCells) { cell.setVisible(true); cell.setWantsKeyboardFocus(true); }
        loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
        loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true); aboutButton.setVisible(true); advancedButton.setVisible(true);
        refreshSlotCells();
        resized();
        if (returnFocus != nullptr && returnFocus->isShowing() && returnFocus->isEnabled())
            returnFocus->grabKeyboardFocus();
        else
            selectSlot(processor.getCurrentSlot(), true);
    }
}

void LSampler24AudioProcessorEditor::refreshSampleSetCell(bool announce)
{
    if (sampleSetGridFocus)
    {
        auto* param = processor.getAutomationSampleSetParameter(processor.getCurrentSlot(),
            sampleSetIndex, sampleSetPage, sampleSetField);
        if (observedSampleSetParameter != param)
        {
            observedSampleSetParameter = param;
            observedSampleSetState = getHostAutomationEnvelopeState(param);
        }
    }
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    const auto sampleName = info.loaded ? (info.name.isNotEmpty() ? info.name : juce::String("Loaded")) : juce::String("Empty");

    if (!sampleSetGridFocus)
    {
        sampleSetCell.setBrowserText("F1 Samples. Sample " + juce::String(sampleSetIndex + 1) + " of "
            + juce::String(LSampler24AudioProcessor::sampleSetSize) + ", " + sampleName
            + ", Velocity " + juce::String(info.velocityLow) + " to " + juce::String(info.velocityHigh));

        if (announce)
            lsampler::announceToActiveScreenReader(sampleSetCell,
                "Sample " + juce::String(sampleSetIndex + 1) + ", " + sampleName);
        return;
    }

    sampleSetCell.setBrowserText((sampleSetPage == 2 ? "F3 Global Parameters. " : "F2 Sample Parameters. ")
                                  + sampleSetParameterText());
    if (announce)
        lsampler::announceToActiveScreenReader(sampleSetCell, sampleSetParameterText());
}

juce::String LSampler24AudioProcessorEditor::sampleSetParameterText() const
{
    const juce::String suffix = observedSampleSetState == 1 && observedSampleSetParameter != nullptr
        && observedSampleSetParameter == processor.getAutomationSampleSetParameter(
             processor.getCurrentSlot(), sampleSetIndex, sampleSetPage, sampleSetField)
          ? juce::String(", Auto") : juce::String();
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    switch (sampleSetPage == 2 ? 2 : juce::jlimit(0, 1, sampleSetField))
    {
        case 0: return "Velocity Low, " + juce::String(info.velocityLow) + suffix;
        case 1: return "Velocity High, " + juce::String(info.velocityHigh) + suffix;
        default: return "Variation Mode, " + LSampler24AudioProcessor::variationModeName(processor.getVariationMode(slot)) + suffix;
    }
}

void LSampler24AudioProcessorEditor::moveSampleSetEntry(int direction)
{
    if (sampleSetGridFocus) return;
    const int next = juce::jlimit(0, LSampler24AudioProcessor::sampleSetSize - 1, sampleSetIndex + (direction < 0 ? -1 : 1));
    if (next == sampleSetIndex) return;
    sampleSetIndex = next;
    refreshSampleSetCell(true);
    previewSampleSetEntry(false); // On means follow selected sample even if last one finished.
}

void LSampler24AudioProcessorEditor::moveSampleSetField(int direction)
{
    if (!sampleSetGridFocus || sampleSetValueFocus || sampleSetPage == 2) return;
    constexpr int rows = 8;
    const int total = 2;
    const int column = sampleSetField / rows;
    const int columnStart = column * rows;
    const int columnEnd = juce::jmin(total - 1, columnStart + rows - 1);
    const int next = sampleSetField + (direction < 0 ? -1 : 1);
    if (next < columnStart || next > columnEnd) return;
    sampleSetField = next;
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::moveSampleSetGridColumn(int direction)
{
    if (!sampleSetGridFocus || sampleSetValueFocus || sampleSetPage == 2) return;
    constexpr int rows = 8;
    const int total = 2;
    const int next = sampleSetField + (direction < 0 ? -rows : rows);
    if (next < 0 || next >= total) return;
    sampleSetField = next;
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::setSampleSetBoundary(bool maximum)
{
    if (!sampleSetGridFocus) return;
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    if (sampleSetPage == 2)
    {
        const int next = maximum ? LSampler24AudioProcessor::variationShuffleNoRepeat
                                 : LSampler24AudioProcessor::variationOff;
        if (processor.getVariationMode(slot) == next) return; // Silent boundary.
        processor.setVariationMode(slot, next);
    }
    else if (sampleSetField == 0)
    {
        const int next = maximum ? info.velocityHigh : 1;
        if (info.velocityLow == next) return;
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, next, info.velocityHigh);
    }
    else if (sampleSetField == 1)
    {
        const int next = maximum ? 127 : info.velocityLow;
        if (info.velocityHigh == next) return;
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, info.velocityLow, next);
    }
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::changeSampleSetValue(int direction, bool coarse)
{
    if (!sampleSetGridFocus) return;
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    const int amount = stepWidths[static_cast<size_t>(stepWidthIndex)] * (coarse ? valuePageStep : 1);
    if (sampleSetPage == 2)
    {
        const int current = processor.getVariationMode(slot);
        const int next = juce::jlimit<int>(LSampler24AudioProcessor::variationOff,
            LSampler24AudioProcessor::variationShuffleNoRepeat, current + (direction < 0 ? -1 : 1));
        if (next == current) return; // Silent at the minimum and maximum.
        processor.setVariationMode(slot, next);
    }
    else if (sampleSetField == 0)
    {
        const int next = juce::jlimit(1, info.velocityHigh,
            info.velocityLow + (direction < 0 ? -amount : amount));
        if (next == info.velocityLow) return;
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, next, info.velocityHigh);
    }
    else if (sampleSetField == 1)
    {
        const int next = juce::jlimit(info.velocityLow, 127,
            info.velocityHigh + (direction < 0 ? -amount : amount));
        if (next == info.velocityHigh) return;
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, info.velocityLow, next);
    }
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::previewSampleSetEntry(bool toggleMode)
{
    if (toggleMode)
        sampleSetPreviewEnabled = !sampleSetPreviewEnabled;
    if (!sampleSetPreviewEnabled)
    {
        processor.requestPreviewStop();
        if (toggleMode)
            lsampler::announceToActiveScreenReader(sampleSetCell, "Preview Off");
        return;
    }

    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    if (!info.loaded)
    {
        // An empty entry has no sound, but Preview stays On for the next entry.
        processor.requestPreviewStop();
        if (toggleMode)
            lsampler::announceToActiveScreenReader(sampleSetCell, "Preview On. Empty sample");
        return;
    }
    // Selected Sample Set entries play through the current Slot DSP. Only the
    // explicit Space action changes Preview On/Off, never the end of the audio.
    processor.requestImportPreviewStop();
    processor.requestSampleSetPreview(slot, sampleSetIndex);
    if (toggleMode)
        lsampler::announceToActiveScreenReader(sampleSetCell, "Preview On");
}

void LSampler24AudioProcessorEditor::enterSampleSetBrowser()
{
    if (!sampleSetActive || importBrowserActive || slotLibraryActive) return;
    importForSampleSet = false;
    importSampleSetSlot = processor.getCurrentSlot();
    importSampleSetIndex = sampleSetIndex;
    enterSlotLibraryBrowser(true);
}

void LSampler24AudioProcessorEditor::enterImportBrowser()
{
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    importBrowserActive = true;
    loadImportSettings();
    importDriveList = false;
    importRecentPathsMode = false;
    importShiftSelectionActive = false;
    if (!importForSampleSet) parameterPage = false;
    importPlan.clear();
    importSlicePending = false;
    importLastSlicePlanIndex = -1;
    importCurrentSlicePlanIndex = -1;
    importFileLoopEnabled = false;
    importPlayStartFile = {};
    importPlayStartSeconds = 0.0;
    processor.requestImportPreviewLoop(0.0, 0.0, false);
    importPreviewFile = {};
    importStartSlot = processor.getCurrentSlot();

    const auto current = importForSampleSet ? processor.getSampleSetEntryFile(importSampleSetSlot, importSampleSetIndex)
                                              : processor.getCurrentSampleFile();
    if (!importDirectory.isDirectory())
        importDirectory = current.existsAsFile() ? current.getParentDirectory()
                                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (!importDirectory.isDirectory())
        importDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    addImportRecentPath(importDirectory);

    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    sampleSetCell.setVisible(false); sampleSetCell.setWantsKeyboardFocus(false);
    importBrowserCell.setVisible(true);
    importBrowserCell.setWantsKeyboardFocus(true);
    // Alt+O is only the Sample Browser. Library Import/Export actions live
    // exclusively in Advanced (Alt+T).
    importSourceCombo.setVisible(false);
    importSourceCombo.setWantsKeyboardFocus(false);
    exportLibraryButton.setVisible(false);
    exportLibraryButton.setWantsKeyboardFocus(false);
    exportAllLibraryButton.setVisible(false);
    exportAllLibraryButton.setWantsKeyboardFocus(false);
    importLibraryButton.setVisible(false);
    importLibraryButton.setWantsKeyboardFocus(false);
    refreshImportEntries();
    if (importRememberedEntryPath.isNotEmpty())
    {
        for (int i = 0; i < static_cast<int>(importEntries.size()); ++i)
            if (importEntries[static_cast<size_t>(i)].file.getFullPathName() == importRememberedEntryPath)
            { importEntryIndex = i; selectImportEntry(i, false); break; }
    }
    if (!importEntries.empty() && !importEntries[static_cast<size_t>(importEntryIndex)].directory
        && importEntries[static_cast<size_t>(importEntryIndex)].file.getFullPathName() == importRememberedEntryPath)
    {
        importPlayStartFile = importEntries[static_cast<size_t>(importEntryIndex)].file;
        importPlayStartSeconds = importRememberedPosition;
        prepareImportPreviewForCurrent();
        processor.requestImportPreviewSeek(importRememberedPosition);
        if (importPreviewEnabled && !processor.isImportPreviewPlaying()) processor.requestImportPreviewToggle();
    }
    resized();
    importBrowserCell.grabKeyboardFocus();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis == nullptr || !safeThis->importBrowserActive) return;
        lsampler::announceToActiveScreenReader(safeThis->importBrowserCell, safeThis->importForSampleSet
            ? "Sample Browser. Enter loads the selected file into the current Sample Set position."
            : "Sample Browser. Enter loads selected items into slots.");
    });
}

void LSampler24AudioProcessorEditor::leaveImportBrowser(bool announceSlot, bool resetPreviewPosition)
{
    const bool returningToSampleSet = importForSampleSet && sampleSetActive;
    const bool rememberedPreviewEnabled = importPreviewEnabled;
    if (resetPreviewPosition)
    {
        // Escape is a true cancel for the Alt+O browser.  Persist only the
        // user's Preview On/Off preference; discard cursor, file/selection,
        // slicing and other temporary browser state so the next visit starts
        // cleanly from the beginning.
        saveImportPreviewPreferenceOnly();
        importEntryIndex = 0;
        importRememberedEntryPath.clear();
        importRememberedPosition = 0.0;
        importPlan.clear();
        importShiftSelectionActive = false;
        importSlicePending = false;
        importSliceFile = {};
        importSliceStart = 0.0;
        importLastSlicePlanIndex = -1;
        importCurrentSlicePlanIndex = -1;
        importPlayStartFile = {};
        importPlayStartSeconds = 0.0;
        importPreviewFile = {};
        importLastInitial = 0;
        importDirectorySelectionMemory.clear();
    }
    else
        saveImportSettings(false);

    processor.requestImportPreviewStop();
    processor.requestImportPreviewSeek(0.0);
    processor.requestImportPreviewLoop(0.0, 0.0, false);
    importPreviewEnabled = rememberedPreviewEnabled;
    importFileLoopEnabled = false;
    importCurrentSlicePlanIndex = -1;
    importBrowserActive = false;
    importBrowserCell.setVisible(false);
    importBrowserCell.setWantsKeyboardFocus(false);
    importSourceCombo.setVisible(false);
    importSourceCombo.setWantsKeyboardFocus(false);
    exportLibraryButton.setVisible(false);
    exportLibraryButton.setWantsKeyboardFocus(false);
    exportAllLibraryButton.setVisible(false);
    exportAllLibraryButton.setWantsKeyboardFocus(false);
    importLibraryButton.setVisible(false);
    importLibraryButton.setWantsKeyboardFocus(false);
    if (returningToSampleSet)
    {
        importForSampleSet = false;
        for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
        loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
        loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
        parameterSelector.setVisible(false); parameterValue.setVisible(false);
        sampleSetCell.setVisible(true); sampleSetCell.setWantsKeyboardFocus(true);
        resized();
        sampleSetCell.grabKeyboardFocus();
        refreshSampleSetCell(true);
        return;
    }
    importForSampleSet = false;
    for (auto& cell : slotCells) { cell.setVisible(true); cell.setWantsKeyboardFocus(true); }
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true); aboutButton.setVisible(true); advancedButton.setVisible(true);
    resized();
    if (announceSlot) returnToCurrentSlotAndAnnounce();
}

void LSampler24AudioProcessorEditor::refreshImportEntries()
{
    importEntries.clear();
    if (importRecentPathsMode)
    {
        for (const auto& path : importRecentPaths) if (path.isDirectory()) importEntries.push_back({ path, true });
    }
    else if (importDriveList)
    {
        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots(roots);
        for (const auto& root : roots) importEntries.push_back({ root, true });
    }
    else
    {
        if (!importDirectory.isDirectory()) return;
        juce::Array<juce::File> dirs, files;
        importDirectory.findChildFiles(dirs, juce::File::findDirectories, false);
        importDirectory.findChildFiles(files, juce::File::findFiles, false);
        for (const auto& f : dirs) importEntries.push_back({ f, true });
        for (const auto& f : files)
            if (SamplePool::instance().hasSupportedExtension(f))
                importEntries.push_back({ f, false });
    }
    importEntryIndex = importEntries.empty() ? 0 : juce::jlimit(0, int(importEntries.size()) - 1, importEntryIndex);
    if (!importEntries.empty() && !importRecentPathsMode && !importDriveList && importDirectory.isDirectory())
    {
        const auto it = importDirectorySelectionMemory.find(importDirectory.getFullPathName());
        if (it != importDirectorySelectionMemory.end())
            for (int i = 0; i < static_cast<int>(importEntries.size()); ++i)
                if (importEntries[static_cast<size_t>(i)].file.getFullPathName() == it->second)
                { importEntryIndex = i; break; }
    }
    if (importEntries.empty()) importBrowserCell.setBrowserText(importRecentPathsMode ? "No recent paths" : (importDriveList ? "No drives" : "Empty folder"));
    else selectImportEntry(importEntryIndex, false);
}

void LSampler24AudioProcessorEditor::selectImportEntry(int index, bool announce)
{
    if (importEntries.empty()) { importEntryIndex = 0; importBrowserCell.setBrowserText(importRecentPathsMode ? "No recent paths" : "Empty folder"); return; }
    importEntryIndex = juce::jlimit(0, int(importEntries.size()) - 1, index);
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (importPreviewFile.getFullPathName().isNotEmpty() && importPreviewFile != e.file)
        importPreviewFile = {};

    juce::String text;
    if (importRecentPathsMode)
    {
        text = "Recent path " + juce::String(importEntryIndex + 1) + " of " + juce::String(importEntries.size()) + ", " + e.file.getFullPathName();
    }
    else
    {
        text = e.file.getFileName();
        if (text.isEmpty()) text = e.file.getFullPathName();
        if (e.directory && importDriveList)
            text += ", drive";
        else
        {
            int selectedSlot = -1, sliceCount = 0;
            for (const auto& item : importPlan)
            {
                if (item.file == e.file)
                {
                    if (!item.slice) selectedSlot = item.slot;
                    else ++sliceCount;
                }
            }
            if (selectedSlot >= 0) text += ", selected slot " + juce::String(selectedSlot + 1);
            if (sliceCount > 0) text += ", " + juce::String(sliceCount) + (sliceCount == 1 ? " slice" : " slices");
        }
    }

    importBrowserCell.setBrowserText(text);
    if (!importRecentPathsMode && !importDriveList && importDirectory.isDirectory())
        importDirectorySelectionMemory[importDirectory.getFullPathName()] = e.file.getFullPathName();
    if (announce) announceImportEntry();
    updateImportPreviewForSelection();
    if (!importRecentPathsMode) saveImportSettings();
}

void LSampler24AudioProcessorEditor::cycleImportEntryByInitial(juce::juce_wchar initial, int direction)
{
    if (importEntries.empty()) return;
    const auto target = juce::CharacterFunctions::toLowerCase(initial);
    importLastInitial = target;
    const int count = static_cast<int>(importEntries.size());
    direction = direction < 0 ? -1 : 1;
    for (int offset = 1; offset <= count; ++offset)
    {
        int index = (importEntryIndex + direction * offset) % count;
        if (index < 0) index += count;
        auto name = importEntries[static_cast<size_t>(index)].file.getFileName();
        if (name.isEmpty()) name = importEntries[static_cast<size_t>(index)].file.getFullPathName();
        if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase(name[0]) == target)
        {
            selectImportEntry(index, true);
            return;
        }
    }
}

void LSampler24AudioProcessorEditor::moveImportDestinationSlot(int direction)
{
    const int count = LSampler24AudioProcessor::slotCount;
    int slot = importStartSlot;
    for (int n = 0; n < count; ++n)
    {
        slot += (direction < 0 ? -1 : 1);
        if (slot < 0 || slot >= count) break;
        if (!processor.isSlotOccupied(slot) && !importSlotReserved(slot))
        {
            importStartSlot = slot;
            processor.setCurrentSlot(slot);
            lsampler::announceToActiveScreenReader(importBrowserCell, "Slot " + juce::String(slot + 1));
            return;
        }
    }
}

void LSampler24AudioProcessorEditor::shiftSelectImportEntry(int direction)
{
    if (importRecentPathsMode || importEntries.empty()) return;
    const auto currentIndex = importEntryIndex;
    const auto& current = importEntries[static_cast<size_t>(currentIndex)];
    if (!importShiftSelectionActive)
    {
        if (current.directory) { announceImportEntry(); return; }
        bool already = false;
        for (const auto& item : importPlan) if (!item.slice && item.file == current.file) { already = true; break; }
        if (!already) toggleImportFileSelection();
        importShiftSelectionActive = true;
        return;
    }

    const int next = juce::jlimit(0, static_cast<int>(importEntries.size()) - 1, currentIndex + (direction < 0 ? -1 : 1));
    if (next == currentIndex) return;
    const auto oldFile = current.file;
    selectImportEntry(next, false);
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) { announceImportEntry(); return; }

    bool selected = false;
    for (const auto& item : importPlan) if (!item.slice && item.file == e.file) { selected = true; break; }
    if (!selected)
    {
        const int slot = nextImportFreeSlot();
        if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
        importPlan.push_back({ e.file, slot, false, 0.0, 0.0 });
    }
    else
    {
        for (auto it = importPlan.begin(); it != importPlan.end(); ++it)
        {
            if (!it->slice && it->file == oldFile) { importPlan.erase(it); break; }
        }
    }
    selectImportEntry(importEntryIndex, true);
}

void LSampler24AudioProcessorEditor::moveToImportFileWithSlice(int direction)
{
    if (importRecentPathsMode || importEntries.empty()) return;
    const double reference = processor.getImportPreviewPositionSeconds();
    for (int i = importEntryIndex + (direction < 0 ? -1 : 1); i >= 0 && i < static_cast<int>(importEntries.size()); i += (direction < 0 ? -1 : 1))
    {
        const auto& e = importEntries[static_cast<size_t>(i)];
        if (e.directory) continue;
        std::vector<int> slices;
        for (int p = 0; p < static_cast<int>(importPlan.size()); ++p)
            if (importPlan[static_cast<size_t>(p)].slice && importPlan[static_cast<size_t>(p)].file == e.file) slices.push_back(p);
        if (slices.empty()) continue;
        selectImportEntry(i, false);
        int best = slices.front(); double bestDist = std::numeric_limits<double>::max(); int ordinal = 0, bestOrdinal = 0;
        for (int p : slices)
        {
            const auto& item = importPlan[static_cast<size_t>(p)];
            const double centre = 0.5 * (item.start + item.end);
            const double d = std::abs(centre - reference);
            if (d < bestDist) { bestDist = d; best = p; bestOrdinal = ordinal; }
            ++ordinal;
        }
        importPreviewEnabled = true;
        applyImportSliceLoop(best, true);
        lsampler::announceToActiveScreenReader(importBrowserCell,
            e.file.getFileNameWithoutExtension() + ", Slice " + juce::String(bestOrdinal + 1) + " of " + juce::String(slices.size()));
        return;
    }
    lsampler::announceToActiveScreenReader(importBrowserCell, direction < 0 ? "No previous sliced file" : "No next sliced file");
}

juce::File LSampler24AudioProcessorEditor::importSettingsFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Lo-lo78").getChildFile("LSampler-24");
    dir.createDirectory();
    return dir.getChildFile("settings.xml");
}

void LSampler24AudioProcessorEditor::loadImportSettings()
{
    importRecentPaths.clear();
    auto file = importSettingsFile();
    if (!file.existsAsFile()) return;
    juce::XmlDocument doc(file);
    auto xml = doc.getDocumentElement();
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings")) return;
    auto path = xml->getStringAttribute("f3Directory");
    if (path.isNotEmpty()) importDirectory = juce::File(path);
    importEntryIndex = xml->getIntAttribute("f3Index", 0);
    importPreviewEnabled = xml->getBoolAttribute("f3Preview", false);
    importRememberedEntryPath = xml->getStringAttribute("f3EntryPath");
    importRememberedPosition = xml->getDoubleAttribute("f3Position", 0.0);
    importLastInitial = static_cast<juce::juce_wchar>(xml->getIntAttribute("f3LastInitial", 0));
    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
        if (child->hasTagName("Recent"))
        {
            auto f = juce::File(child->getStringAttribute("path"));
            if (f.isDirectory()) importRecentPaths.push_back(f);
        }
}

void LSampler24AudioProcessorEditor::saveImportPreviewPreferenceOnly()
{
    // Escape/cancel keeps only persistent browser navigation plus the shared
    // Preview On/Off preference.  Playback position, selections, slices, loop
    // and other temporary editing state are deliberately reset.
    auto file = importSettingsFile();
    std::unique_ptr<juce::XmlElement> xml;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        xml = doc.getDocumentElement();
    }
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings"))
        xml = std::make_unique<juce::XmlElement>("LSampler24Settings");

    xml->setAttribute("f3Preview", importPreviewEnabled);

    // When Alt+O itself is being cancelled, remember only its folder/file
    // navigation and reset the editing/playback state.  Calls from Alt+S only
    // update the shared Preview preference and must not disturb Alt+O's last file.
    if (importBrowserActive)
    {
        if (importDirectory.isDirectory())
            xml->setAttribute("f3Directory", importDirectory.getFullPathName());
        xml->setAttribute("f3Index", importEntryIndex);

        juce::String selectedPath;
        if (!importRecentPathsMode && !importDriveList && !importEntries.empty()
            && importEntryIndex >= 0 && importEntryIndex < static_cast<int>(importEntries.size()))
            selectedPath = importEntries[static_cast<size_t>(importEntryIndex)].file.getFullPathName();
        if (selectedPath.isNotEmpty())
            xml->setAttribute("f3EntryPath", selectedPath);
        else
            xml->removeAttribute("f3EntryPath");

        xml->setAttribute("f3Position", 0.0);
        xml->setAttribute("f3LastInitial", 0);
    }
    xml->writeTo(file);
}

void LSampler24AudioProcessorEditor::saveImportSettings(bool resetPreviewPosition)
{
    if (!importBrowserActive && !importDirectory.isDirectory()) return;
    auto file = importSettingsFile();

    // Preserve the other browsers' independent positions (f4/f5/f6), the
    // configured Library and its history. Updating Alt+O must not erase them.
    std::unique_ptr<juce::XmlElement> xml;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        xml = doc.getDocumentElement();
    }
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings"))
        xml = std::make_unique<juce::XmlElement>("LSampler24Settings");

    xml->setAttribute("f3Directory", importDirectory.getFullPathName());
    xml->setAttribute("f3Index", importEntryIndex);
    xml->setAttribute("f3Preview", importPreviewEnabled);
    juce::String selectedPath;
    if (!importRecentPathsMode && !importEntries.empty() && importEntryIndex >= 0 && importEntryIndex < static_cast<int>(importEntries.size()))
        selectedPath = importEntries[static_cast<size_t>(importEntryIndex)].file.getFullPathName();
    xml->setAttribute("f3EntryPath", selectedPath);
    xml->setAttribute("f3Position", resetPreviewPosition ? 0.0 : processor.getImportPreviewPositionSeconds());
    xml->setAttribute("f3LastInitial", static_cast<int>(importLastInitial));

    // Only replace Alt+O's Recent entries. All other settings remain intact.
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->hasTagName("Recent")) xml->removeChildElement(child, true);
        child = next;
    }
    int count = 0;
    for (const auto& p : importRecentPaths)
    {
        if (!p.isDirectory() || count++ >= 20) continue;
        auto* child = xml->createNewChildElement("Recent"); child->setAttribute("path", p.getFullPathName());
    }
    xml->writeTo(file);
}

void LSampler24AudioProcessorEditor::addImportRecentPath(const juce::File& directory)
{
    if (!directory.isDirectory()) return;
    std::vector<juce::File> next; next.push_back(directory);
    for (const auto& old : importRecentPaths) if (old != directory && old.isDirectory() && next.size() < 20) next.push_back(old);
    importRecentPaths = std::move(next);
}

void LSampler24AudioProcessorEditor::enterImportRecentPaths()
{
    addImportRecentPath(importDirectory);
    importRecentPathsMode = true; importDriveList = false; importEntryIndex = 0;
    refreshImportEntries();
    if (!importEntries.empty()) selectImportEntry(0, true);
    else lsampler::announceToActiveScreenReader(importBrowserCell, "No recent paths");
}

void LSampler24AudioProcessorEditor::leaveImportRecentPaths()
{
    importRecentPathsMode = false;
    refreshImportEntries();
    selectImportEntry(importEntryIndex, true);
}

void LSampler24AudioProcessorEditor::removeCurrentImportRecentPath()
{
    if (!importRecentPathsMode || importEntries.empty() || importEntryIndex < 0 || importEntryIndex >= static_cast<int>(importEntries.size())) return;
    auto victim = importEntries[static_cast<size_t>(importEntryIndex)].file;
    importRecentPaths.erase(std::remove(importRecentPaths.begin(), importRecentPaths.end(), victim), importRecentPaths.end());
    refreshImportEntries();
    if (!importEntries.empty()) selectImportEntry(juce::jmin(importEntryIndex, static_cast<int>(importEntries.size()) - 1), true);
    else lsampler::announceToActiveScreenReader(importBrowserCell, "No recent paths");
    saveImportSettings();
}

void LSampler24AudioProcessorEditor::updateImportPreviewForSelection()
{
    if (!importPreviewEnabled || importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory)
    {
        processor.requestImportPreviewStop();
        importPreviewFile = {};
        return;
    }
    importPreviewFile = {};
    prepareImportPreviewForCurrent();
    if (importPreviewFile == e.file)
    {
        importCurrentSlicePlanIndex = -1;
        if (importFileLoopEnabled)
            processor.requestImportPreviewLoop(0.0, processor.getImportPreviewLengthSeconds(), true);
        else
            processor.requestImportPreviewLoop(0.0, 0.0, false);
        if (importPlayStartFile == e.file)
            processor.requestImportPreviewSeek(importPlayStartSeconds);
        processor.requestImportPreviewToggle();
    }
}

void LSampler24AudioProcessorEditor::announceImportEntry()
{
    if (importEntries.empty())
    {
        lsampler::announceToActiveScreenReader(importBrowserCell, "Empty folder");
        importBrowserCell.setDescription({});
        return;
    }
    lsampler::announceToActiveScreenReader(importBrowserCell, importBrowserCell.getBrowserText());
    importBrowserCell.setDescription({});
}

void LSampler24AudioProcessorEditor::prepareImportPreviewForCurrent()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) return;
    if (importPreviewFile == e.file) return;
    juce::String error;
    if (processor.prepareImportPreview(e.file, error)) importPreviewFile = e.file;
    else lsampler::announceToActiveScreenReader(importBrowserCell, error.isNotEmpty() ? error : "Cannot preview");
}

void LSampler24AudioProcessorEditor::seekImportPreview(double deltaSeconds)
{
    prepareImportPreviewForCurrent();
    const auto length = processor.getImportPreviewLengthSeconds();
    const auto pos = juce::jlimit(0.0, length, processor.getImportPreviewPositionSeconds() + deltaSeconds);
    processor.requestImportPreviewSeek(pos);
}

bool LSampler24AudioProcessorEditor::importSlotReserved(int slot) const
{
    for (const auto& item : importPlan) if (item.slot == slot) return true;
    return false;
}

int LSampler24AudioProcessorEditor::nextImportFreeSlot() const
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const int begin = pass == 0 ? importStartSlot : 0;
        const int end = pass == 0 ? LSampler24AudioProcessor::slotCount : importStartSlot;
        for (int slot = begin; slot < end; ++slot)
            if (!processor.isSlotOccupied(slot) && !importSlotReserved(slot)) return slot;
    }
    return -1;
}

void LSampler24AudioProcessorEditor::toggleImportFileSelection()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) { announceImportEntry(); return; }
    for (auto it = importPlan.begin(); it != importPlan.end(); ++it)
        if (!it->slice && it->file == e.file)
        {
            importPlan.erase(it);
            selectImportEntry(importEntryIndex, true);
            return;
        }
    const int slot = nextImportFreeSlot();
    if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
    importPlan.push_back({ e.file, slot, false, 0.0, 0.0 });
    selectImportEntry(importEntryIndex, true);
}

void LSampler24AudioProcessorEditor::markImportSliceStart()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) { lsampler::announceToActiveScreenReader(importBrowserCell, "No audio file"); return; }
    prepareImportPreviewForCurrent();
    const int slot = nextImportFreeSlot();
    if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
    importSlicePending = true;
    importLastSlicePlanIndex = -1;
    importSliceFile = e.file;
    importSliceStart = processor.getImportPreviewPositionSeconds();
    processor.requestImportPreviewLoop(0.0, 0.0, false);
    importCurrentSlicePlanIndex = -1;
    int sliceNumber = 1;
    for (const auto& item : importPlan) if (item.slice) ++sliceNumber;
    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Slice " + juce::String(sliceNumber) + " slot " + juce::String(slot + 1) + " start " + juce::String(importSliceStart, 3));
}

void LSampler24AudioProcessorEditor::markImportSliceEnd()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) { lsampler::announceToActiveScreenReader(importBrowserCell, "No audio file"); return; }

    double end = processor.getImportPreviewPositionSeconds();

    // Like the Lua F3 browser, a second Ctrl+W may adjust the end of the last
    // completed slice until Ctrl+Q starts a new slice.
    if (!importSlicePending && importLastSlicePlanIndex >= 0
        && importLastSlicePlanIndex < static_cast<int>(importPlan.size()))
    {
        auto& item = importPlan[static_cast<size_t>(importLastSlicePlanIndex)];
        if (item.slice && item.file == e.file)
        {
            double start = item.start;
            if (end < start) std::swap(start, end);
            if (end - start <= 0.001) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slice end not valid"); return; }
            item.start = start;
            item.end = end;
            if (importSliceEndRestarts) applyImportSliceLoop(importLastSlicePlanIndex, importPreviewEnabled);
            else
            {
                processor.requestImportPreviewLoop(0.0, 0.0, false);
                importCurrentSlicePlanIndex = -1;
            }
            int sliceNumber = 0;
            for (int i = 0; i <= importLastSlicePlanIndex; ++i) if (importPlan[static_cast<size_t>(i)].slice) ++sliceNumber;
            lsampler::announceToActiveScreenReader(importBrowserCell,
                "Slice " + juce::String(sliceNumber) + " slot " + juce::String(item.slot + 1) + " end " + juce::String(end, 3)
                + ", length " + juce::String(end - start, 3));
            return;
        }
    }

    if (!importSlicePending) { lsampler::announceToActiveScreenReader(importBrowserCell, "No slice start"); return; }
    if (e.file != importSliceFile) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slice source changed"); return; }
    double start = importSliceStart;
    if (end < start) std::swap(start, end);
    if (end - start <= 0.001) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slice end not valid"); return; }
    const int slot = nextImportFreeSlot();
    if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
    importPlan.push_back({ e.file, slot, true, start, end });
    importSlicePending = false;
    importLastSlicePlanIndex = static_cast<int>(importPlan.size()) - 1;
    int sliceNumber = 0; for (const auto& item : importPlan) if (item.slice) ++sliceNumber;

    if (importSliceEndRestarts)
        applyImportSliceLoop(importLastSlicePlanIndex, importPreviewEnabled);
    else
    {
        // Continuous mode deliberately leaves live playback untouched at the
        // just-marked end, so another Ctrl+Q can mark the next slice in motion.
        processor.requestImportPreviewLoop(0.0, 0.0, false);
        importCurrentSlicePlanIndex = -1;
    }

    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Slice " + juce::String(sliceNumber) + " slot " + juce::String(slot + 1) + " end " + juce::String(end, 3)
        + ", length " + juce::String(end - start, 3));
}

void LSampler24AudioProcessorEditor::toggleImportSliceEndMode()
{
    importSliceEndRestarts = !importSliceEndRestarts;
    lsampler::announceToActiveScreenReader(importBrowserCell,
        importSliceEndRestarts ? "Slice end starts slice" : "Slice end continues playback");
}

void LSampler24AudioProcessorEditor::toggleImportFileLoop()
{
    importFileLoopEnabled = !importFileLoopEnabled;
    prepareImportPreviewForCurrent();
    importCurrentSlicePlanIndex = -1;
    if (importFileLoopEnabled)
        processor.requestImportPreviewLoop(0.0, processor.getImportPreviewLengthSeconds(), true);
    else
        processor.requestImportPreviewLoop(0.0, 0.0, false);
    lsampler::announceToActiveScreenReader(importBrowserCell,
        importFileLoopEnabled ? "Browser file loop on" : "Browser file loop off");
}

void LSampler24AudioProcessorEditor::applyImportSliceLoop(int planIndex, bool startPlayback)
{
    if (planIndex < 0 || planIndex >= static_cast<int>(importPlan.size())) return;
    const auto& item = importPlan[static_cast<size_t>(planIndex)];
    if (!item.slice) return;
    prepareImportPreviewForCurrent();
    importFileLoopEnabled = false;
    importCurrentSlicePlanIndex = planIndex;
    processor.requestImportPreviewLoop(item.start, item.end, true);
    processor.requestImportPreviewSeek(item.start);
    if (startPlayback && !processor.isImportPreviewPlaying())
        processor.requestImportPreviewToggle();
}

void LSampler24AudioProcessorEditor::navigateImportSlice(int direction)
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) return;
    std::vector<int> slices;
    for (int i = 0; i < static_cast<int>(importPlan.size()); ++i)
        if (importPlan[static_cast<size_t>(i)].slice && importPlan[static_cast<size_t>(i)].file == e.file)
            slices.push_back(i);
    if (slices.empty())
    {
        lsampler::announceToActiveScreenReader(importBrowserCell,
            direction < 0 ? "No previous slice" : "No next slice");
        return;
    }

    int pos = -1;
    for (int i = 0; i < static_cast<int>(slices.size()); ++i)
        if (slices[static_cast<size_t>(i)] == importCurrentSlicePlanIndex) { pos = i; break; }
    if (pos < 0)
    {
        const auto cursor = processor.getImportPreviewPositionSeconds();
        for (int i = 0; i < static_cast<int>(slices.size()); ++i)
        {
            const auto& item = importPlan[static_cast<size_t>(slices[static_cast<size_t>(i)])];
            if (cursor >= item.start && cursor <= item.end) { pos = i; break; }
        }
    }
    if (pos < 0) pos = direction > 0 ? -1 : static_cast<int>(slices.size());
    const int next = pos + direction;
    if (next < 0 || next >= static_cast<int>(slices.size()))
    {
        lsampler::announceToActiveScreenReader(importBrowserCell,
            direction < 0 ? "No previous slice" : "No next slice");
        return; // horizontal slice navigation: no wrap
    }

    const int planIndex = slices[static_cast<size_t>(next)];
    importPreviewEnabled = true;
    applyImportSliceLoop(planIndex, true);
    const auto& item = importPlan[static_cast<size_t>(planIndex)];
    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Slice " + juce::String(next + 1) + " of " + juce::String(slices.size())
        + ", slot " + juce::String(item.slot + 1));
}

void LSampler24AudioProcessorEditor::seekOutsideImportSlice(int direction)
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) return;
    prepareImportPreviewForCurrent();
    const double length = processor.getImportPreviewLengthSeconds();
    const double cursor = processor.getImportPreviewPositionSeconds();
    int chosen = -1;
    for (int i = 0; i < static_cast<int>(importPlan.size()); ++i)
    {
        const auto& item = importPlan[static_cast<size_t>(i)];
        if (!item.slice || item.file != e.file) continue;
        if (i == importCurrentSlicePlanIndex || (cursor >= item.start && cursor <= item.end)) { chosen = i; break; }
    }
    double target = cursor + (direction < 0 ? -1.0 : 1.0);
    if (chosen >= 0)
    {
        const auto& item = importPlan[static_cast<size_t>(chosen)];
        target = direction < 0 ? juce::jmax(0.0, item.start - 0.080)
                               : juce::jmin(length, item.end + 0.080);
    }
    target = juce::jlimit(0.0, length, target);
    processor.requestImportPreviewLoop(0.0, 0.0, false);
    importCurrentSlicePlanIndex = -1;
    importFileLoopEnabled = false;
    importPlayStartFile = e.file;
    importPlayStartSeconds = target;
    processor.requestImportPreviewSeek(target);
}

void LSampler24AudioProcessorEditor::markImportPlayStart()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) { lsampler::announceToActiveScreenReader(importBrowserCell, "No audio file"); return; }
    prepareImportPreviewForCurrent();
    importPlayStartFile = e.file;
    importPlayStartSeconds = processor.getImportPreviewPositionSeconds();
    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Play start " + juce::String(importPlayStartSeconds, 3));
}

void LSampler24AudioProcessorEditor::toggleImportPreviewPlayPause()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) return;
    prepareImportPreviewForCurrent();
    if (!processor.isImportPreviewPlaying() && importPlayStartFile == e.file)
        processor.requestImportPreviewSeek(importPlayStartSeconds);
    processor.requestImportPreviewToggle();
}

void LSampler24AudioProcessorEditor::deleteImportPlanItemAtCursor()
{
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory) return;
    const double pos = processor.getImportPreviewPositionSeconds();
    for (auto it = importPlan.begin(); it != importPlan.end(); ++it)
        if (it->slice && it->file == e.file && pos >= it->start && pos <= it->end)
        {
            const int erased = static_cast<int>(std::distance(importPlan.begin(), it));
            importPlan.erase(it); importSlicePending = false;
            if (importLastSlicePlanIndex == erased) importLastSlicePlanIndex = -1;
            if (importCurrentSlicePlanIndex == erased) { importCurrentSlicePlanIndex = -1; processor.requestImportPreviewLoop(0.0, 0.0, false); }
            if (importLastSlicePlanIndex > erased) --importLastSlicePlanIndex;
            if (importCurrentSlicePlanIndex > erased) --importCurrentSlicePlanIndex;
            selectImportEntry(importEntryIndex, false);
            lsampler::announceToActiveScreenReader(importBrowserCell, "Slice deleted"); return;
        }
    for (auto it = importPlan.begin(); it != importPlan.end(); ++it)
        if (!it->slice && it->file == e.file)
        {
            importPlan.erase(it); selectImportEntry(importEntryIndex, true); return;
        }
}

void LSampler24AudioProcessorEditor::commitImportPlan()
{
    if (importEntries.empty()) return;
    const auto& current = importEntries[static_cast<size_t>(importEntryIndex)];
    if (current.directory) return;
    if (importForSampleSet)
    {
        const auto file = current.file;
        const int slot = importSampleSetSlot;
        const int sampleIndex = importSampleSetIndex;
        auto load = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
                     file, slot, sampleIndex]
        {
            if (safeThis == nullptr) return;
            safeThis->processor.requestImportPreviewStop();
            safeThis->runFileTask("Loading Sample Set entry", [file, slot, sampleIndex](LSampler24AudioProcessor& p) {
                FileTaskResult r; r.slot = slot;
                r.ok = p.loadSampleSetEntryToSlot(file, slot, sampleIndex, r.message);
                if (r.ok) { r.count = 1; r.message = "Sample " + juce::String(sampleIndex + 1) + " loaded"; }
                return r;
            }, [safeThis, sampleIndex](const FileTaskResult& r, bool focus) {
                if (safeThis == nullptr || !r.ok) return;
                safeThis->sampleSetIndex = sampleIndex;
                if (focus) { safeThis->leaveImportBrowser(false); safeThis->refreshSampleSetCell(true); }
            });
        };
        const auto occupied = processor.getSampleSetEntry(slot, sampleIndex);
        if (occupied.loaded)
            confirmOverwrite("Sample " + juce::String(sampleIndex + 1) + ", " + occupied.name
                + ", is occupied. Replace it with " + file.getFileNameWithoutExtension()
                + "? Press Y or N.", std::move(load),
                [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                {
                    if (safeThis != nullptr) safeThis->importBrowserCell.grabKeyboardFocus();
                });
        else load();
        return;
    }
    if (importPlan.empty())
    {
        // A single Alt+O load targets the slot the user opened it from.
        // Explicit multi-file assignments retain their existing destination mapping.
        importPlan.push_back({ current.file, importStartSlot, false, 0.0, 0.0 });
    }

    const auto work = importPlan;
    auto load = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), work]
    {
        if (safeThis == nullptr) return;
        safeThis->processor.requestImportPreviewStop();
        safeThis->importBrowserCell.grabKeyboardFocus();
        if (auto* handler = safeThis->importBrowserCell.getAccessibilityHandler()) handler->grabFocus();
        safeThis->runFileTask("Loading selected samples", [work](LSampler24AudioProcessor& p) {
            FileTaskResult r; r.ok = true;
            for (const auto& item : work) {
                if (p.shouldStopFileTask() || !p.importSampleToSlot(item.file, item.slot,
                        item.slice ? item.start : 0.0, item.slice ? item.end : 0.0, r.message)) { r.ok = false; break; }
                ++r.count; r.slot = juce::jmax(r.slot, item.slot);
            }
            if (r.ok) r.message = "Samples loaded";
            else r.message = "Loaded " + juce::String(r.count) + " samples. " + r.message;
            return r;
        }, [safeThis](const FileTaskResult& r, bool focus) {
            if (safeThis == nullptr) return;
            safeThis->importPlan.erase(safeThis->importPlan.begin(), safeThis->importPlan.begin()
                + juce::jmin(r.count, int(safeThis->importPlan.size())));
            safeThis->importSlicePending = false;
            safeThis->importLastSlicePlanIndex = -1;
            safeThis->importCurrentSlicePlanIndex = -1;
            if (r.ok) {
                if (r.slot >= 0) safeThis->processor.setCurrentSlot(r.slot);
                if (focus) {
                    safeThis->leaveImportBrowser(false);
                    safeThis->refreshSlotCells();
                    safeThis->returnToCurrentSlotAndAnnounce();
                }
            }
        });
    };
    int overwritten = 0;
    juce::String firstReplacement;
    for (const auto& item : work)
    {
        if (!processor.isSlotOccupied(item.slot)) continue;
        ++overwritten;
        if (firstReplacement.isEmpty())
            firstReplacement = "Slot " + juce::String(item.slot + 1) + ", "
                + replacementSlotName(item.slot) + ", is occupied. Replace it with "
                + item.file.getFileNameWithoutExtension() + "?";
    }
    if (overwritten > 0)
        confirmOverwrite(work.size() == 1 ? firstReplacement + " Press Y or N."
            : juce::String(overwritten) + " destination slots are occupied. "
                + firstReplacement + " Confirm replacing all? Press Y or N.", std::move(load),
            [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
            {
                if (safeThis == nullptr) return;
                safeThis->leaveImportBrowser(false, true);
                safeThis->returnToActionOrigin(safeThis->loadSample);
            });
    else load();
}

void LSampler24AudioProcessorEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load Sample", juce::File(), SamplePool::instance().getSupportedAudioWildcard());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                const int slot = safeThis->processor.getCurrentSlot();
                auto load = [safeThis, file, slot]
                {
                    if (safeThis == nullptr) return;
                    safeThis->runFileTask("Loading sample", [file, slot](LSampler24AudioProcessor& p) {
                        FileTaskResult r; r.ok = p.loadSampleToSlot(file, slot, r.message);
                        if (r.ok) r.message = "Sample loaded: " + file.getFileNameWithoutExtension();
                        return r;
                    }, [safeThis](const FileTaskResult&, bool focus) {
                        if (safeThis != nullptr && focus) safeThis->returnToCurrentSlotAndAnnounce();
                    });
                };
                if (safeThis->processor.isSlotOccupied(slot))
                    safeThis->confirmOverwrite("Slot " + juce::String(slot + 1) + ", "
                        + safeThis->replacementSlotName(slot) + ", is occupied. Replace it with "
                        + file.getFileNameWithoutExtension() + "? Press Y or N.", std::move(load),
                        [safeThis] { if (safeThis != nullptr) safeThis->returnToActionOrigin(safeThis->loadSample); });
                else load();
                return;
            }
            safeThis->returnToActionOrigin(safeThis->loadSample);
        });
}


void LSampler24AudioProcessorEditor::chooseImportFiles()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    chooser = std::make_unique<juce::FileChooser>("Import Files to Library", importDirectory, "*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            const auto files = fc.getResults();
            if (files.isEmpty()) { safeThis->returnToAdvancedAndAnnounce(); return; }
            safeThis->runFileTask("Import Files", [files](LSampler24AudioProcessor& p) {
                FileTaskResult r;
                juce::Array<juce::File> audioFiles;
                juce::String firstError;
                bool anyImported = false;
                // Export File packages contain a Slot preset plus its WAVs.
                // Accept them here as the inverse of Advanced > Export File.
                for (const auto& file : files)
                {
                    if (file.hasFileExtension(".ls24"))
                    {
                        int imported = 0, samples = 0, skipped = 0;
                        juce::String localError;
                        if (p.importLibraryArchive(file, imported, samples, skipped, localError))
                        {
                            r.count += imported;
                            r.skipped += skipped;
                            anyImported = true;
                        }
                        else
                        {
                            ++r.skipped;
                            if (firstError.isEmpty()) firstError = localError;
                        }
                    }
                    else audioFiles.add(file);
                }
                if (!audioFiles.isEmpty())
                {
                    int imported = 0, skipped = 0;
                    juce::String localError;
                    if (p.importFilesToLibrary(audioFiles, imported, skipped, localError))
                    {
                        r.count += imported;
                        r.skipped += skipped;
                        anyImported = true;
                    }
                    else
                    {
                        r.skipped += audioFiles.size();
                        if (firstError.isEmpty()) firstError = localError;
                    }
                }
                r.ok = anyImported;
                r.message = r.ok ? "Imported " + juce::String(r.count) + " slots, skipped " + juce::String(r.skipped)
                                 : firstError.isNotEmpty() ? firstError : "No valid files imported";
                return r;
            }, [safeThis, files](const FileTaskResult& r, bool) {
                if (r.ok && !files.isEmpty()) {
                    safeThis->importDirectory = files.getFirst().getParentDirectory();
                    safeThis->addImportRecentPath(safeThis->importDirectory);
                    safeThis->saveImportSettings();
                }
                safeThis->returnToAdvancedAndAnnounce();
            });
        });
}

void LSampler24AudioProcessorEditor::chooseImportFolder()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    chooser = std::make_unique<juce::FileChooser>("Import Folder", importDirectory, "*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            const auto folder = fc.getResult();
            if (folder.getFullPathName().isEmpty()) { safeThis->returnToAdvancedAndAnnounce(); return; }
            safeThis->runFileTask("Importing folder", [folder](LSampler24AudioProcessor& p) {
                FileTaskResult r;
                r.ok = p.importFolderToLibrary(folder, r.count, r.skipped, r.message);
                if (r.ok) r.message = "Imported " + juce::String(r.count) + " slots, skipped " + juce::String(r.skipped);
                return r;
            }, [safeThis, folder](const FileTaskResult& r, bool focus) {
                if (r.ok) { safeThis->importDirectory = folder; safeThis->addImportRecentPath(folder); safeThis->saveImportSettings(); }
                safeThis->returnToAdvancedAndAnnounce();
            });
        });
}

void LSampler24AudioProcessorEditor::chooseExportFolder()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    if (!slotLibraryActive || slotLibraryForSampleSet || slotLibraryForBank || slotLibraryEntries.empty()
        || slotLibraryEntryIndex < 0 || slotLibraryEntryIndex >= static_cast<int>(slotLibraryEntries.size())
        || !slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].directory)
    {
        lsampler::announceToActiveScreenReader(exportLibraryButton,
            "Select a folder in the Slot Library browser and press Enter");
        return;
    }

    const auto folder = slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].file;
    auto baseName = juce::File::createLegalFileName(folder.getFileName().trim());
    if (baseName.isEmpty()) baseName = "LSampler-24 Folder";
    auto initial = processor.getLibrary().root().getChildFile(baseName + ".lsampler-24.ls24");
    chooser = std::make_unique<juce::FileChooser>("Export Folder: " + folder.getFileName(), initial, "*");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), folder](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto chosen = fc.getResult();
            if (chosen.getFullPathName().isEmpty()) { safeThis->leaveSlotLibraryBrowser(false); safeThis->returnToAdvancedAndAnnounce(); return; }
            auto name = chosen.getFileName().trim();
            if (name.endsWithIgnoreCase(".lsampler-24.ls24"))
                name = name.dropLastCharacters((int) juce::String(".lsampler-24.ls24").length());
            else if (name.endsWithIgnoreCase(".ls24"))
                name = name.dropLastCharacters(5);
            name = juce::File::createLegalFileName(name.trim());
            if (name.isEmpty()) name = juce::File::createLegalFileName(folder.getFileName());
            auto target = chosen.getParentDirectory().getChildFile(name + ".lsampler-24.ls24");

            safeThis->runFileTask("Exporting folder", [folder, target](LSampler24AudioProcessor& p) {
                FileTaskResult r; int samples = 0;
                r.ok = p.exportLibraryFolderArchive(folder, target, r.count, samples, r.message);
                if (r.ok) r.message = "Exported folder " + folder.getFileName() + ": "
                    + juce::String(r.count) + " slots, " + juce::String(samples) + " samples";
                return r;
            }, [safeThis](const FileTaskResult&, bool) { safeThis->leaveSlotLibraryBrowser(false); safeThis->returnToAdvancedAndAnnounce(); });
        });
}

std::vector<juce::File> LSampler24AudioProcessorEditor::loadLibraryHistory() const
{
    std::vector<juce::File> result;
    auto file = importSettingsFile();
    if (!file.existsAsFile()) return result;
    juce::XmlDocument doc(file);
    auto xml = doc.getDocumentElement();
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings")) return result;
    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
        if (child->hasTagName("LibraryHistory"))
        {
            auto f = juce::File(child->getStringAttribute("path"));
            if (f.getFullPathName().isNotEmpty()) result.push_back(f);
        }
    return result;
}

void LSampler24AudioProcessorEditor::saveLibrarySettings(const juce::File& currentRoot,
                                                          const std::vector<juce::File>& history) const
{
    auto file = importSettingsFile();
    std::unique_ptr<juce::XmlElement> xml;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        xml = doc.getDocumentElement();
    }
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings"))
        xml = std::make_unique<juce::XmlElement>("LSampler24Settings");

    xml->setAttribute("libraryRoot", currentRoot.getFullPathName());
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->hasTagName("LibraryHistory")) xml->removeChildElement(child, true);
        child = next;
    }
    int count = 0;
    for (const auto& path : history)
    {
        if (path.getFullPathName().isEmpty() || count++ >= 20) continue;
        auto* child = xml->createNewChildElement("LibraryHistory");
        child->setAttribute("path", path.getFullPathName());
    }
    xml->writeTo(file);
}

void LSampler24AudioProcessorEditor::activateLibraryRoot(const juce::File& root, bool remember)
{
    if (root.getFullPathName().isEmpty()) return;
    root.createDirectory();
    processor.setLibraryRoot(root);

    auto history = loadLibraryHistory();
    const auto defaultRoot = LSampler24AudioProcessor::defaultLibraryRoot();
    if (remember && root != defaultRoot)
    {
        std::vector<juce::File> next { root };
        for (const auto& old : history)
            if (old != root && old != defaultRoot && next.size() < 20) next.push_back(old);
        history = std::move(next);
    }
    saveLibrarySettings(root, history);
    slotLibraryRoot = processor.getLibrary().slots();
    slotLibraryDirectory = slotLibraryRoot;
    if (slotLibraryActive) refreshSlotLibraryEntries();
    lsampler::announceToActiveScreenReader(advancedButton,
        "Library Folder. " + processor.getLibrary().root().getChildFile("Library").getFullPathName());
}

bool LSampler24AudioProcessorEditor::isLibraryAvailable() const
{
    // Check the configured root on demand. This intentionally allows an
    // external drive to become available again while the plug-in is open.
    const auto root = processor.getLibrary().root();
    return root.getFullPathName().isNotEmpty() && root.isDirectory();
}

juce::String LSampler24AudioProcessorEditor::unavailableLibraryMessage() const
{
    const auto path = processor.getLibrary().root().getChildFile("Library").getFullPathName();
    return "Library unavailable: " + path
        + "\n\nConnect the drive, or choose/restore the Library Folder in Advanced.";
}

bool LSampler24AudioProcessorEditor::requireLibraryAvailable(juce::Component* focusTarget,
                                                              bool openAdvancedAfterOk)
{
    if (isLibraryAvailable())
        return true;

    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("LSampler-24 Library unavailable")
        .withMessage(unavailableLibraryMessage())
        .withButton("OK");

    juce::AlertWindow::showAsync(options,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
         focus = juce::Component::SafePointer<juce::Component>(focusTarget),
         openAdvancedAfterOk](int)
        {
            if (safeThis == nullptr) return;
            if (openAdvancedAfterOk)
            {
                // The Alert can be shown while REAPER's FX Chain owns focus.
                // Opening a PopupMenu here creates a menu that exists visually
                // but is not the active keyboard/accessibility surface.  Defer
                // it until timerCallback sees keyboard focus inside the editor.
                safeThis->pendingAdvancedOpen = true;

                // If focus has already returned to the editor after dismissing
                // the Alert, the timer will open Advanced on its next tick.
                if (safeThis->hasKeyboardFocus(true))
                    safeThis->advancedButton.grabKeyboardFocus();
            }
            else if (focus != nullptr)
                focus->grabKeyboardFocus();
        });
    return false;
}

void LSampler24AudioProcessorEditor::showStartupLibraryWarningIfNeeded()
{
    if (!isLibraryAvailable())
        requireLibraryAvailable(&advancedButton, true);
}

void LSampler24AudioProcessorEditor::chooseLibraryFolder()
{
    auto currentLibrary = processor.getLibrary().root().getChildFile("Library");
    chooser = std::make_unique<juce::FileChooser>("Choose LSampler-24 Library location", currentLibrary.getParentDirectory(), "*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto selected = fc.getResult();
            if (selected.getFullPathName().isEmpty()) { safeThis->returnToAdvancedAndAnnounce(); return; }

            juce::File root;
            if (selected.getFileName().equalsIgnoreCase("Library")
                && selected.getParentDirectory().getFileName().equalsIgnoreCase("LSampler-24"))
                root = selected.getParentDirectory();
            else if (selected.getFileName().equalsIgnoreCase("LSampler-24"))
                root = selected;
            else
                root = selected.getChildFile("LSampler-24");

            safeThis->activateLibraryRoot(root, true);
            safeThis->returnToAdvancedAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::restoreDefaultLibraryFolder()
{
    activateLibraryRoot(LSampler24AudioProcessor::defaultLibraryRoot(), false);
}

void LSampler24AudioProcessorEditor::returnToButton(juce::TextButton& button)
{
    if (!isShowing() || !button.isShowing() || !button.isEnabled()) return;
    juce::AccessibilityHandler::clearCurrentlyFocusedHandler();
    button.grabKeyboardFocus();
    if (auto* handler = button.getAccessibilityHandler())
        handler->grabFocus();
}

void LSampler24AudioProcessorEditor::returnToActionOrigin(juce::TextButton& button)
{
    const bool shortcut = actionOpenedByShortcut;
    actionOpenedByShortcut = false;
    if (shortcut)
    {
        if (parameterPage && !globalOpen) leaveSlotParameters();
        returnToCurrentSlotAndAnnounce();
    }
    else
        returnToButton(button);
}

void LSampler24AudioProcessorEditor::returnToAdvancedAndAnnounce()
{
    returnToButton(advancedButton);
    // PopupMenu is destroyed as soon as a command is selected. Recreate it
    // after the file chooser/browser finishes, with the invoking item selected.
    if (advancedMenuReturnQueued) return;
    advancedMenuReturnQueued = true;
    juce::Timer::callAfterDelay(120, [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis == nullptr) return;
        safeThis->advancedMenuReturnQueued = false;
        if (safeThis->isShowing() && !safeThis->slotLibraryActive && !safeThis->fileUiBusy)
            safeThis->showAdvancedMenu();
    });
}

void LSampler24AudioProcessorEditor::enterExportFolderBrowser()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    slotLibraryForExport = true;
    slotLibraryForExportFile = false;
    slotLibraryForExportBank = false;
    enterSlotLibraryBrowser(false, false);
    if (!slotLibraryActive) slotLibraryForExport = false;
    else lsampler::announceToActiveScreenReader(slotLibraryCell,
        "Export Folder. Select a folder in the Slot Library and press Enter");
}

void LSampler24AudioProcessorEditor::enterExportFileBrowser()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    slotLibraryForExport = true;
    slotLibraryForExportFile = true;
    slotLibraryForExportBank = false;
    enterSlotLibraryBrowser(false, false);
    if (!slotLibraryActive) { slotLibraryForExport = false; slotLibraryForExportFile = false; }
    else lsampler::announceToActiveScreenReader(slotLibraryCell,
        "Export File. Select a slot in the Slot Library and press Enter");
}

void LSampler24AudioProcessorEditor::enterExportBankBrowser()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    slotLibraryForExportBank = true;
    slotLibraryForExport = false;
    slotLibraryForExportFile = false;
    enterSlotLibraryBrowser(false, true);
    if (!slotLibraryActive) slotLibraryForExportBank = false;
    else lsampler::announceToActiveScreenReader(slotLibraryCell,
        "Export Bank. Select a bank in the Bank Library and press Enter");
}

void LSampler24AudioProcessorEditor::showAdvancedMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(advancedMenuLookAndFeel.get());
    menu.addSectionHeader("Advanced");
    menu.addItem(1, "Choose Library Folder...");
    menu.addItem(2, "Open Library Folder");
    menu.addItem(3, "Restore Default Library Folder");

    juce::PopupMenu previous;
    const auto history = loadLibraryHistory();
    int id = 100;
    for (const auto& root : history)
    {
        const auto libraryPath = root.getChildFile("Library").getFullPathName();
        previous.addItem(id++, libraryPath + (root.isDirectory() ? juce::String() : " (unavailable)"), root.isDirectory());
    }
    if (history.empty()) previous.addItem(99, "No previous libraries", false);
    menu.addSubMenu("Previous Libraries", previous);

    menu.addSeparator();
    menu.addSectionHeader("Library Import and Export");
    menu.addItem(10, "Import File...");
    menu.addItem(15, "Export File...");
    menu.addItem(11, "Import Folder...");
    menu.addItem(13, "Export Folder...");
    menu.addItem(16, "Import Bank...");
    menu.addItem(17, "Export Bank...");
    menu.addItem(12, "Import All Library...");
    menu.addItem(14, "Export All Library...");

    auto options = juce::PopupMenu::Options().withTargetComponent(&advancedButton)
                         .withInitiallySelectedItem(advancedMenuReturnItem);
    menu.showMenuAsync(options,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), history](int result)
        {
            if (safeThis == nullptr) return;
            if (result == 0) { safeThis->returnToActionOrigin(safeThis->advancedButton); return; }
            safeThis->advancedMenuReturnItem = result;
            if (result == 1) safeThis->chooseLibraryFolder();
            else if (result == 2)
            {
                if (!safeThis->requireLibraryAvailable(&safeThis->advancedButton, false)) return;
                auto folder = safeThis->processor.getLibrary().root().getChildFile("Library");
                if (!folder.startAsProcess())
                    lsampler::announceToActiveScreenReader(safeThis->advancedButton, "Cannot open Library Folder");
                safeThis->returnToAdvancedAndAnnounce();
            }
            else if (result == 3) { safeThis->restoreDefaultLibraryFolder(); safeThis->returnToAdvancedAndAnnounce(); }
            else if (result == 10) safeThis->chooseImportFiles();
            else if (result == 11) safeThis->chooseImportFolder();
            else if (result == 12) safeThis->chooseImportLibrary();
            else if (result == 13) safeThis->enterExportFolderBrowser();
            else if (result == 14) safeThis->chooseExportLibrary();
            else if (result == 15) safeThis->enterExportFileBrowser();
            else if (result == 16) safeThis->chooseImportBank();
            else if (result == 17) safeThis->enterExportBankBrowser();
            else if (result >= 100 && result < 100 + static_cast<int>(history.size()))
            {
                safeThis->activateLibraryRoot(history[static_cast<size_t>(result - 100)], true);
                safeThis->returnToAdvancedAndAnnounce();
            }
            else safeThis->returnToAdvancedAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseExportFile()
{
    if (!slotLibraryActive || !slotLibraryForExportFile || slotLibraryEntries.empty()
        || slotLibraryEntryIndex < 0 || slotLibraryEntryIndex >= static_cast<int>(slotLibraryEntries.size())
        || slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].directory)
        return;

    const auto slotFile = slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].file;
    auto name = slotFile.getFileNameWithoutExtension();
    if (name.startsWithIgnoreCase("Slot_")) name = name.substring(5);
    const auto initial = processor.getLibrary().root().getChildFile(
        juce::File::createLegalFileName(name) + ".lsampler-24.ls24");
    chooser = std::make_unique<juce::FileChooser>("Export File: " + name, initial, "*");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), slotFile](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto chosen = fc.getResult();
            if (chosen.getFullPathName().isEmpty())
            {
                safeThis->leaveSlotLibraryBrowser(false);
                safeThis->returnToAdvancedAndAnnounce();
                return;
            }
            const auto suffix = juce::String(".lsampler-24.ls24");
            auto name = chosen.getFileName();
            if (name.endsWithIgnoreCase(suffix)) name = name.dropLastCharacters(suffix.length());
            else if (name.endsWithIgnoreCase(".ls24")) name = name.dropLastCharacters(5);
            name = juce::File::createLegalFileName(name.trim());
            if (name.isEmpty()) name = "LSampler-24 Slot";
            const auto target = chosen.getParentDirectory().getChildFile(name + suffix);
            safeThis->runFileTask("Exporting file", [slotFile, target](LSampler24AudioProcessor& p) {
                FileTaskResult r; int samples = 0;
                r.ok = p.exportLibraryFolderArchive(slotFile, target, r.count, samples, r.message);
                if (r.ok) r.message = "Exported " + juce::String(r.count) + " slot, " + juce::String(samples) + " samples";
                return r;
            }, [safeThis](const FileTaskResult&, bool)
            {
                if (safeThis == nullptr) return;
                safeThis->leaveSlotLibraryBrowser(false);
                safeThis->returnToAdvancedAndAnnounce();
            });
        });
}

void LSampler24AudioProcessorEditor::chooseExportBank()
{
    if (!slotLibraryActive || !slotLibraryForExportBank || slotLibraryEntries.empty()
        || slotLibraryEntryIndex < 0 || slotLibraryEntryIndex >= static_cast<int>(slotLibraryEntries.size())
        || slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].directory)
        return;

    const auto bank = slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].file;
    auto name = bank.getFileNameWithoutExtension();
    if (name.startsWithIgnoreCase("Bank_")) name = name.substring(5);
    const auto initial = processor.getLibrary().root().getChildFile(
        juce::File::createLegalFileName(name) + ".lsampler-24.ls24");
    chooser = std::make_unique<juce::FileChooser>("Export Bank: " + name, initial, "*");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), bank](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto chosen = fc.getResult();
            if (chosen.getFullPathName().isEmpty())
            {
                safeThis->leaveSlotLibraryBrowser(false);
                safeThis->returnToAdvancedAndAnnounce();
                return;
            }
            const auto suffix = juce::String(".lsampler-24.ls24");
            auto name = chosen.getFileName();
            if (name.endsWithIgnoreCase(suffix)) name = name.dropLastCharacters(suffix.length());
            else if (name.endsWithIgnoreCase(".ls24")) name = name.dropLastCharacters(5);
            name = juce::File::createLegalFileName(name.trim());
            if (name.isEmpty()) name = "LSampler-24 Bank";
            const auto target = chosen.getParentDirectory().getChildFile(name + suffix);
            safeThis->runFileTask("Exporting bank", [bank, target](LSampler24AudioProcessor& p) {
                FileTaskResult r; int samples = 0;
                r.ok = p.exportBankArchive(bank, target, samples, r.message);
                if (r.ok) r.message = "Bank exported with " + juce::String(samples) + " samples";
                return r;
            }, [safeThis](const FileTaskResult&, bool)
            {
                if (safeThis == nullptr) return;
                safeThis->leaveSlotLibraryBrowser(false);
                safeThis->returnToAdvancedAndAnnounce();
            });
        });
}

void LSampler24AudioProcessorEditor::chooseImportBank()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    chooser = std::make_unique<juce::FileChooser>("Import Bank", processor.getLibrary().banks(), "*.ls24");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            const auto source = fc.getResult();
            if (!source.existsAsFile()) { safeThis->returnToAdvancedAndAnnounce(); return; }
            safeThis->runFileTask("Importing bank", [source](LSampler24AudioProcessor& p) {
                FileTaskResult r; int count = 0; juce::File imported;
                r.ok = p.importBankArchive(source, imported, count, r.message);
                if (r.ok) r.message = "Bank imported: " + imported.getFileNameWithoutExtension()
                                       + ", " + juce::String(count) + " new samples";
                return r;
            }, [safeThis](const FileTaskResult&, bool) { if (safeThis != nullptr) safeThis->returnToAdvancedAndAnnounce(); });
        });
}

void LSampler24AudioProcessorEditor::chooseExportLibrary()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    auto initial = processor.getLibrary().root().getChildFile("LSampler-24 Full Library.lsampler-24.ls24");
    chooser = std::make_unique<juce::FileChooser>("Export All Library", initial, "*");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto chosen = fc.getResult();
            if (chosen.getFullPathName().isNotEmpty())
            {
                auto name = chosen.getFileName().trim();
                if (name.endsWithIgnoreCase(".lsampler-24.ls24"))
                    name = name.dropLastCharacters((int) juce::String(".lsampler-24.ls24").length());
                else if (name.endsWithIgnoreCase(".ls24"))
                    name = name.dropLastCharacters(5);
                name = juce::File::createLegalFileName(name.trim());
                if (name.isEmpty()) name = "LSampler-24 Full Library";
                auto target = chosen.getParentDirectory().getChildFile(name + ".lsampler-24.ls24");

                safeThis->runFileTask("Exporting all library", [target](LSampler24AudioProcessor& p) {
                    FileTaskResult r; int samples = 0;
                    r.ok = p.exportLibraryArchive(target, r.count, samples, r.message);
                    if (r.ok) r.message = "Exported " + juce::String(r.count) + " slots, " + juce::String(samples) + " samples";
                    return r;
                }, [safeThis](const FileTaskResult&, bool) { safeThis->returnToAdvancedAndAnnounce(); });
                return;
            }
            safeThis->returnToAdvancedAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseImportLibrary()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    chooser = std::make_unique<juce::FileChooser>("Import All Library", processor.getLibrary().root(), "*.ls24;*.lsampler-24.ls24");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            const auto file = fc.getResult();
            if (file.getFullPathName().isEmpty()) { safeThis->returnToAdvancedAndAnnounce(); return; }
            safeThis->runFileTask("Importing library", [file](LSampler24AudioProcessor& p) {
                FileTaskResult r; int samples = 0;
                r.ok = p.importLibraryArchive(file, r.count, samples, r.skipped, r.message);
                if (r.ok) r.message = "Imported library: " + juce::String(r.count) + " slots, "
                    + juce::String(samples) + " new samples, skipped " + juce::String(r.skipped);
                return r;
            }, [safeThis](const FileTaskResult&, bool focus) {
                if (safeThis->slotLibraryActive) safeThis->refreshSlotLibraryEntries();
                safeThis->returnToAdvancedAndAnnounce();
            });
        });
}

void LSampler24AudioProcessorEditor::saveSlotLibraryNavigationState()
{
    auto file = importSettingsFile();
    std::unique_ptr<juce::XmlElement> xml;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        xml = doc.getDocumentElement();
    }
    if (xml == nullptr || !xml->hasTagName("LSampler24Settings"))
        xml = std::make_unique<juce::XmlElement>("LSampler24Settings");

    // A Save As never overwrites a Load browser's remembered location.
    if (slotLibrarySaving) return;
    // Each browser remembers its own directory/file in user settings:
    // f4 = Load Slot, f5 = Load Bank, f6 = Alt+M Sample Set audio browser.
    if (!slotLibraryForBank && !slotLibraryForSampleSet)
        xml->setAttribute("f3Preview", slotLibraryPreviewEnabled);
    if (slotLibraryForSampleSet)
        xml->setAttribute("f6Preview", sampleSetBrowserPreviewEnabled);

    const auto prefix = slotLibraryForSampleSet ? juce::String("f6")
                       : slotLibraryForBank ? juce::String("f5") : juce::String("f4");
    if (slotLibraryDirectory.isDirectory())
        xml->setAttribute(prefix + "Directory", slotLibraryDirectory.getFullPathName());
    xml->setAttribute(prefix + "Index", slotLibraryEntryIndex);

    juce::String selectedPath;
    if (!slotLibraryEntries.empty() && slotLibraryEntryIndex >= 0
        && slotLibraryEntryIndex < static_cast<int>(slotLibraryEntries.size()))
        selectedPath = slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].file.getFullPathName();
    if (selectedPath.isNotEmpty())
        xml->setAttribute(prefix + "EntryPath", selectedPath);
    else
        xml->removeAttribute(prefix + "EntryPath");

    xml->writeTo(file);
}


void LSampler24AudioProcessorEditor::enterSlotLibraryBrowser(bool forSampleSet, bool forBank)
{
    juce::Component* libraryFocus = forSampleSet ? static_cast<juce::Component*>(&sampleSetCell)
                               : forBank ? static_cast<juce::Component*>(&loadBank)
                                         : static_cast<juce::Component*>(&loadSlot);
    if (!requireLibraryAvailable(libraryFocus, true)) return;
    if (slotLibraryActive) return;
    slotLibraryForSampleSet = forSampleSet;
    slotLibraryForBank = forBank;
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    processor.requestLibraryPreviewStop();
    if (!slotLibraryForSampleSet) parameterPage = false;
    importBrowserActive = false;
    slotLibraryActive = true;

    // Alt+O and Alt+S share the same persistent Preview On/Off preference.
    // Read only that preference here: the rest of the Alt+O browser state must
    // remain independent, especially after Escape has cancelled it.
    importPreviewEnabled = false;
    if (auto settings = importSettingsFile(); settings.existsAsFile())
    {
        juce::XmlDocument doc(settings);
        if (auto xml = doc.getDocumentElement(); xml != nullptr && xml->hasTagName("LSampler24Settings"))
            importPreviewEnabled = xml->getBoolAttribute("f3Preview", false);
    }
    slotLibraryPreviewEnabled = (slotLibrarySaving || slotLibraryForBank || slotLibraryForSampleSet || slotLibraryForExport) ? false : importPreviewEnabled;
    if (slotLibraryForSampleSet)
    {
        sampleSetBrowserPreviewEnabled = false;
        if (auto settings = importSettingsFile(); settings.existsAsFile())
        {
            juce::XmlDocument doc(settings);
            if (auto xml = doc.getDocumentElement(); xml != nullptr && xml->hasTagName("LSampler24Settings"))
                sampleSetBrowserPreviewEnabled = xml->getBoolAttribute("f6Preview", false);
        }
    }
    slotLibraryShiftSelectionActive = false;
    slotLibrarySelection.clear();
    slotLibraryStartSlot = processor.getCurrentSlot();
    slotLibraryRoot = slotLibraryForSampleSet ? processor.getLibrary().samples()
                      : slotLibraryForBank ? processor.getLibrary().banks() : processor.getLibrary().slots();
    slotLibraryRoot.createDirectory();

    juce::String rememberedSlotDirectory, rememberedSlotEntryPath;
    int rememberedSlotIndex = 0;
    if (auto settings = importSettingsFile(); settings.existsAsFile())
    {
        juce::XmlDocument doc(settings);
        if (auto xml = doc.getDocumentElement(); xml != nullptr && xml->hasTagName("LSampler24Settings"))
        {
            const auto prefix = slotLibraryForSampleSet ? juce::String("f6")
                               : slotLibraryForBank ? juce::String("f5") : juce::String("f4");
            rememberedSlotDirectory = xml->getStringAttribute(prefix + "Directory");
            rememberedSlotEntryPath = xml->getStringAttribute(prefix + "EntryPath");
            rememberedSlotIndex = xml->getIntAttribute(prefix + "Index", 0);
        }
    }

    if (rememberedSlotDirectory.isNotEmpty())
    {
        const juce::File candidate(rememberedSlotDirectory);
        // Never restore a folder outside this browser's Library root, including
        // an old external Library location that is no longer configured.
        if (candidate.isDirectory() && (candidate == slotLibraryRoot || candidate.isAChildOf(slotLibraryRoot)))
            slotLibraryDirectory = candidate;
    }
    if (!slotLibraryDirectory.isDirectory() || (slotLibraryDirectory != slotLibraryRoot && !slotLibraryDirectory.isAChildOf(slotLibraryRoot)))
        slotLibraryDirectory = slotLibraryRoot;
    // A Save As must never silently inherit an old subfolder destination.
    if (slotLibrarySaving) slotLibraryDirectory = slotLibraryRoot;
    slotLibraryEntryIndex = slotLibrarySaving ? 0 : rememberedSlotIndex;
    if (rememberedSlotEntryPath.isNotEmpty() && !slotLibrarySaving)
        slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = rememberedSlotEntryPath;

    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    sampleSetCell.setVisible(false); sampleSetCell.setWantsKeyboardFocus(false);
    importBrowserCell.setVisible(false); importSourceCombo.setVisible(false);
    exportLibraryButton.setVisible(false); exportAllLibraryButton.setVisible(false); importLibraryButton.setVisible(false);
    exportLibraryButton.setWantsKeyboardFocus(false);
    exportAllLibraryButton.setWantsKeyboardFocus(false);
    importLibraryButton.setWantsKeyboardFocus(false);
    slotLibraryCell.setVisible(true);
    slotLibraryCell.setWantsKeyboardFocus(true);
    refreshSlotLibraryEntries();
    resized();
    slotLibraryCell.grabKeyboardFocus();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr && safeThis->slotLibraryActive)
        {
            if (safeThis->slotLibrarySaving)
                lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell,
                    "Save " + juce::String(safeThis->slotLibraryForBank ? "Bank " : "Slot ")
                    + safeThis->slotLibrarySaveName + ". In "
                    + safeThis->slotLibraryDirectory.getFileName()
                    + ". Enter saves here; Right opens a subfolder; Backspace goes up; Shift+F10 opens menu.");
            else safeThis->announceSlotLibraryEntry();
        }
    });
}

void LSampler24AudioProcessorEditor::leaveSlotLibraryBrowser(bool announceSlot)
{
    if (!slotLibraryActive) return;
    const bool returningToSampleSet = slotLibraryForSampleSet && sampleSetActive;
    // Also save Alt+M on Escape, cancellation and successful loading.
    saveSlotLibraryNavigationState();
    processor.requestLibraryPreviewStop();
    processor.requestImportPreviewStop();
    // Keep the F1 Sample Browser preview preference; only stop its audio.
    slotLibraryPendingPreview = {};
    slotLibraryPreviewDelayTicks = 0;
    slotLibraryPreviewEnabled = false;
    slotLibraryActive = false;
    slotLibrarySaving = false;
    slotLibrarySaveName.clear();
    slotLibraryForSampleSet = false;
    slotLibraryForBank = false;
    slotLibraryForExport = false;
    slotLibraryForExportFile = false;
    slotLibraryForExportBank = false;
    slotLibraryCell.setVisible(false);
    slotLibraryCell.setWantsKeyboardFocus(false);
    exportLibraryButton.setVisible(false);
    exportLibraryButton.setWantsKeyboardFocus(false);
    exportAllLibraryButton.setVisible(false);
    exportAllLibraryButton.setWantsKeyboardFocus(false);
    importLibraryButton.setVisible(false);
    importLibraryButton.setWantsKeyboardFocus(false);

    if (returningToSampleSet)
    {
        for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
        loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
        loadBank.setVisible(false); saveBank.setVisible(false); help.setVisible(false); aboutButton.setVisible(false); advancedButton.setVisible(false);
        parameterSelector.setVisible(false); parameterValue.setVisible(false);
        sampleSetCell.setVisible(true); sampleSetCell.setWantsKeyboardFocus(true);
        resized();
        sampleSetCell.grabKeyboardFocus();
        refreshSlotCells();
        refreshSampleSetCell(true);
        return;
    }

    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[size_t(i)];
        cell.setVisible(true);
        cell.setWantsKeyboardFocus(i == processor.getCurrentSlot());
    }
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true); help.setVisible(true); aboutButton.setVisible(true); advancedButton.setVisible(true);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    resized();
    if (announceSlot) returnToCurrentSlotAndAnnounce();
}

static juce::String cleanLibrarySlotName(const juce::File& file)
{
    auto name = file.getFileName();
    const juce::String ext = LibraryManager::slotExtension;
    if (name.endsWithIgnoreCase(ext)) name = name.dropLastCharacters(ext.length());
    if (name.startsWithIgnoreCase("Slot_")) name = name.substring(5);
    return name;
}

static juce::String cleanLibraryBankName(const juce::File& file)
{
    auto name = file.getFileName();
    const juce::String ext = LibraryManager::bankExtension;
    if (name.endsWithIgnoreCase(ext)) name = name.dropLastCharacters(ext.length());
    if (name.startsWithIgnoreCase("Bank_")) name = name.substring(5);
    return name;
}

void LSampler24AudioProcessorEditor::refreshSlotLibraryEntries()
{
    slotLibraryEntries.clear();
    if (!slotLibraryDirectory.isDirectory()) slotLibraryDirectory = slotLibraryRoot;
    juce::Array<juce::File> dirs, files;
    slotLibraryDirectory.findChildFiles(dirs, juce::File::findDirectories, false);
    slotLibraryDirectory.findChildFiles(files, juce::File::findFiles, false,
        slotLibraryForSampleSet ? SamplePool::instance().getSupportedAudioWildcard()
            : "*" + juce::String(slotLibraryForBank ? LibraryManager::bankExtension : LibraryManager::slotExtension));
    for (const auto& f : dirs) slotLibraryEntries.push_back({ f, true });
    for (const auto& f : files) slotLibraryEntries.push_back({ f, false });
    slotLibraryEntryIndex = slotLibraryEntries.empty() ? 0 : juce::jlimit(0, int(slotLibraryEntries.size()) - 1, slotLibraryEntryIndex);
    const auto it = slotLibraryDirectorySelectionMemory.find(slotLibraryDirectory.getFullPathName());
    if (it != slotLibraryDirectorySelectionMemory.end())
        for (int i = 0; i < static_cast<int>(slotLibraryEntries.size()); ++i)
            if (slotLibraryEntries[size_t(i)].file.getFullPathName() == it->second) { slotLibraryEntryIndex = i; break; }
    if (slotLibraryEntries.empty()) slotLibraryCell.setBrowserText(
        slotLibrarySaving ? "Empty folder. Enter saves here" : "Empty folder");
    else selectSlotLibraryEntry(slotLibraryEntryIndex, false);
}

void LSampler24AudioProcessorEditor::selectSlotLibraryEntry(int index, bool announce)
{
    if (slotLibraryEntries.empty()) { slotLibraryCell.setBrowserText(
        slotLibrarySaving ? "Empty folder. Enter saves here" : "Empty folder");
        exportLibraryButton.setButtonText("Export Folder"); return; }
    slotLibraryEntryIndex = juce::jlimit(0, int(slotLibraryEntries.size()) - 1, index);
    const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
    juce::String text;
    if (e.directory)
    {
        text = "Folder " + e.file.getFileName();
        exportLibraryButton.setButtonText("Export Folder: " + e.file.getFileName());
    }
    else
    {
        exportLibraryButton.setButtonText("Export Folder");
        text = slotLibraryForSampleSet ? e.file.getFileName()
            : slotLibraryForBank ? cleanLibraryBankName(e.file) : cleanLibrarySlotName(e.file);
        if (!slotLibraryForBank)
        {
            int target = -1;
            for (const auto& selected : slotLibrarySelection) if (selected.file == e.file) { target = selected.slot; break; }
            if (target >= 0) text = "Selected. Slot " + juce::String(target + 1) + ", " + text;
        }
    }
    if (slotLibraryForSampleSet && !e.directory)
        text = "Sample " + juce::String(importSampleSetIndex + 1) + " source, " + text;
    slotLibraryCell.setBrowserText(text);
    slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = e.file.getFullPathName();
    slotLibraryShiftSelectionActive = false;
    saveSlotLibraryNavigationState();
    if (announce) announceSlotLibraryEntry();
    updateSlotLibraryPreviewForSelection();
}

void LSampler24AudioProcessorEditor::announceSlotLibraryEntry()
{
    const auto selected = slotLibraryEntries.empty()
        ? juce::String(slotLibrarySaving ? "Empty folder. Enter saves here" : "Empty folder")
        : slotLibraryCell.getBrowserText();
    lsampler::announceToActiveScreenReader(slotLibraryCell,
        slotLibrarySaving ? "In " + slotLibraryDirectory.getFileName() + ". " + selected : selected);
    slotLibraryCell.setDescription({});
}

void LSampler24AudioProcessorEditor::cycleSlotLibraryEntryByInitial(juce::juce_wchar initial, int direction)
{
    if (slotLibraryEntries.empty()) return;
    const auto target = juce::CharacterFunctions::toLowerCase(initial);
    slotLibraryLastInitial = target;
    const int count = int(slotLibraryEntries.size());
    direction = direction < 0 ? -1 : 1;
    for (int offset = 1; offset <= count; ++offset)
    {
        int index = (slotLibraryEntryIndex + direction * offset) % count;
        if (index < 0) index += count;
        auto name = slotLibraryEntries[size_t(index)].directory ? slotLibraryEntries[size_t(index)].file.getFileName()
                      : slotLibraryForSampleSet ? slotLibraryEntries[size_t(index)].file.getFileName()
                      : slotLibraryForBank ? cleanLibraryBankName(slotLibraryEntries[size_t(index)].file)
                                           : cleanLibrarySlotName(slotLibraryEntries[size_t(index)].file);
        if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase(name[0]) == target)
        { selectSlotLibraryEntry(index, true); return; }
    }
}

void LSampler24AudioProcessorEditor::updateSlotLibraryPreviewForSelection()
{
    processor.requestLibraryPreviewStop();
    slotLibraryPendingPreview = {};
    slotLibraryPreviewDelayTicks = 0;
    if (slotLibraryForSampleSet)
    {
        processor.requestImportPreviewStop();
        if (!sampleSetBrowserPreviewEnabled || slotLibraryEntries.empty()) return;
        const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (e.directory) return; // Keep On, but only files can make sound.
        slotLibraryPendingPreview = e.file;
        slotLibraryPreviewDelayTicks = 1; // Match Alt+S's short navigation debounce.
        return;
    }
    if (slotLibraryForBank) return;
    if (!slotLibraryPreviewEnabled || slotLibraryEntries.empty()) return;
    const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
    if (e.directory) return;
    slotLibraryPendingPreview = e.file;
    slotLibraryPreviewDelayTicks = 1; // 10 Hz timer: short debounce while rapidly browsing
}

bool LSampler24AudioProcessorEditor::slotLibraryDestinationReserved(int slot) const
{
    for (const auto& item : slotLibrarySelection) if (item.slot == slot) return true;
    return false;
}

int LSampler24AudioProcessorEditor::nextSlotLibraryFreeSlot(int from) const
{
    for (int slot = juce::jmax(0, from); slot < LSampler24AudioProcessor::slotCount; ++slot)
        if (!processor.isSlotOccupied(slot) && !slotLibraryDestinationReserved(slot)) return slot;
    return -1;
}

void LSampler24AudioProcessorEditor::moveSlotLibraryDestination(int direction)
{
    int slot = slotLibraryStartSlot;
    while (true)
    {
        slot += direction < 0 ? -1 : 1;
        if (slot < 0 || slot >= LSampler24AudioProcessor::slotCount) return; // silent border
        if (!processor.isSlotOccupied(slot) && !slotLibraryDestinationReserved(slot))
        {
            slotLibraryStartSlot = slot;
            processor.setCurrentSlot(slot);
            lsampler::announceToActiveScreenReader(slotLibraryCell, "Slot " + juce::String(slot + 1));
            return;
        }
    }
}

void LSampler24AudioProcessorEditor::toggleSlotLibrarySelection()
{
    if (slotLibraryEntries.empty()) return;
    const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
    if (e.directory) return;
    for (auto it = slotLibrarySelection.begin(); it != slotLibrarySelection.end(); ++it)
        if (it->file == e.file)
        {
            slotLibrarySelection.erase(it);
            selectSlotLibraryEntry(slotLibraryEntryIndex, true);
            return;
        }
    const int slot = nextSlotLibraryFreeSlot(slotLibraryStartSlot);
    if (slot < 0) { lsampler::announceToActiveScreenReader(slotLibraryCell, "No free slots"); return; }
    slotLibrarySelection.push_back({ e.file, slot });
    slotLibraryStartSlot = slot;
    selectSlotLibraryEntry(slotLibraryEntryIndex, true);
}

void LSampler24AudioProcessorEditor::shiftSelectSlotLibraryEntry(int direction)
{
    if (slotLibraryEntries.empty()) return;
    const auto current = slotLibraryEntryIndex;
    if (!slotLibraryShiftSelectionActive)
    {
        const auto& e = slotLibraryEntries[size_t(current)];
        if (!e.directory)
        {
            bool found = false; for (const auto& x : slotLibrarySelection) if (x.file == e.file) found = true;
            if (!found) toggleSlotLibrarySelection();
        }
        slotLibraryShiftSelectionActive = true;
    }
    const int next = juce::jlimit(0, int(slotLibraryEntries.size()) - 1, current + direction);
    if (next == current) return;
    slotLibraryEntryIndex = next;
    const auto& e = slotLibraryEntries[size_t(next)];
    if (!e.directory)
    {
        bool found = false; for (const auto& x : slotLibrarySelection) if (x.file == e.file) found = true;
        if (!found)
        {
            const int slot = nextSlotLibraryFreeSlot(slotLibraryStartSlot);
            if (slot >= 0) { slotLibrarySelection.push_back({ e.file, slot }); slotLibraryStartSlot = slot; }
        }
    }
    juce::String text = e.directory ? "Folder " + e.file.getFileName() : cleanLibrarySlotName(e.file);
    if (!e.directory)
        for (const auto& x : slotLibrarySelection) if (x.file == e.file) text = "Selected. Slot " + juce::String(x.slot + 1) + ", " + text;
    slotLibraryCell.setBrowserText(text);
    announceSlotLibraryEntry();
    updateSlotLibraryPreviewForSelection();
}

void LSampler24AudioProcessorEditor::copySlotLibraryEntry(bool cut)
{
    if (slotLibraryEntries.empty()) return;
    const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
    slotLibraryClipboardFile = e.file;
    slotLibraryClipboardCut = cut;
    lsampler::announceToActiveScreenReader(slotLibraryCell,
        juce::String(cut ? "Cut " : "Copied ") + (e.directory ? "Folder " + e.file.getFileName() : cleanLibrarySlotName(e.file)));
}

void LSampler24AudioProcessorEditor::pasteSlotLibraryEntry()
{
    if (slotLibraryClipboardFile.getFullPathName().isEmpty() || !slotLibraryClipboardFile.exists())
        return;

    const auto source = slotLibraryClipboardFile;
    if (!source.isAChildOf(slotLibraryRoot) && source != slotLibraryRoot)
    {
        lsampler::announceToActiveScreenReader(slotLibraryCell, "Clipboard item is outside Slots");
        return;
    }

    if (source.isDirectory() && (slotLibraryDirectory == source || slotLibraryDirectory.isAChildOf(source)))
    {
        lsampler::announceToActiveScreenReader(slotLibraryCell, "Cannot paste a folder inside itself");
        return;
    }

    auto makeUnique = [](const juce::File& folder, const juce::File& item)
    {
        auto candidate = folder.getChildFile(item.getFileName());
        if (!candidate.exists()) return candidate;
        const auto ext = item.isDirectory() ? juce::String{} : item.getFileExtension();
        const auto base = item.isDirectory() ? item.getFileName() : item.getFileNameWithoutExtension();
        for (int n = 2; n < 100000; ++n)
        {
            auto alt = folder.getChildFile(base + "_" + juce::String(n) + ext);
            if (!alt.exists()) return alt;
        }
        return folder.getChildFile(base + "_copy" + ext);
    };

    if (slotLibraryClipboardCut && source.getParentDirectory() == slotLibraryDirectory)
        return; // already in this folder: silent, Explorer-like no-op

    const auto destination = makeUnique(slotLibraryDirectory, source);
    bool ok = false;
    if (slotLibraryClipboardCut)
        ok = source.moveFileTo(destination);
    else if (source.isDirectory())
        ok = source.copyDirectoryTo(destination);
    else
        ok = source.copyFileTo(destination);

    if (!ok)
    {
        lsampler::announceToActiveScreenReader(slotLibraryCell, slotLibraryClipboardCut ? "Move failed" : "Copy failed");
        return;
    }

    if (slotLibraryClipboardCut)
    {
        slotLibraryClipboardFile = {};
        slotLibraryClipboardCut = false;
    }

    slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = destination.getFullPathName();
    refreshSlotLibraryEntries();
    for (int i = 0; i < int(slotLibraryEntries.size()); ++i)
        if (slotLibraryEntries[size_t(i)].file == destination)
        {
            selectSlotLibraryEntry(i, true);
            return;
        }
    announceSlotLibraryEntry();
}

void LSampler24AudioProcessorEditor::commitSlotLibrarySelection()
{
    if (slotLibraryEntries.empty()) return;
    processor.requestLibraryPreviewStop();

    if (slotLibraryForBank)
    {
        const auto& entry = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (entry.directory) return;
        const auto bankFile = entry.file;
        auto load = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), bankFile]
        {
            if (safeThis == nullptr) return;
            safeThis->slotLibraryCell.grabKeyboardFocus();
            if (auto* handler = safeThis->slotLibraryCell.getAccessibilityHandler()) handler->grabFocus();
            safeThis->runFileTask("Loading bank", [bankFile](LSampler24AudioProcessor& p) {
                FileTaskResult r;
                r.ok = p.loadBankPreset(bankFile, r.message);
                if (r.ok) r.message = "Bank loaded: " + bankFile.getFileNameWithoutExtension();
                return r;
            }, [safeThis](const FileTaskResult& r, bool focus) {
                if (safeThis == nullptr || !r.ok) return;
                safeThis->refreshSlotCells();
                if (focus) safeThis->leaveSlotLibraryBrowser(true);
            });
        };
        const int occupied = occupiedBankSlotCount();
        if (occupied > 0)
            confirmOverwrite("The current bank has " + juce::String(occupied)
                + " occupied slots. Replace it with " + cleanLibraryBankName(bankFile)
                + "? Press Y or N.", std::move(load),
                [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                {
                    if (safeThis != nullptr)
                    {
                        safeThis->leaveSlotLibraryBrowser(false);
                        safeThis->returnToActionOrigin(safeThis->loadBank);
                    }
                });
        else load();
        return;
    }

    if (slotLibraryForSampleSet)
    {
        const auto& entry = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (entry.directory) return;
        const auto audioFile = entry.file;
        const int targetSlot = importSampleSetSlot;
        const int targetSample = importSampleSetIndex;
        auto load = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
                     audioFile, targetSlot, targetSample]
        {
            if (safeThis == nullptr) return;
            safeThis->processor.requestImportPreviewStop();
            safeThis->slotLibraryCell.grabKeyboardFocus();
            if (auto* handler = safeThis->slotLibraryCell.getAccessibilityHandler()) handler->grabFocus();
            safeThis->runFileTask("Loading Sample Set entry", [audioFile, targetSlot, targetSample](LSampler24AudioProcessor& p) {
                FileTaskResult r; r.slot = targetSlot;
                r.ok = p.loadSampleSetEntryToSlot(audioFile, targetSlot, targetSample, r.message);
                if (r.ok) r.message = "Sample " + juce::String(targetSample + 1) + " loaded";
                return r;
            }, [safeThis](const FileTaskResult& r, bool focus) {
                if (!r.ok) return;
                safeThis->refreshSlotCells();
                if (focus) safeThis->leaveSlotLibraryBrowser(false);
            });
        };
        const auto occupied = processor.getSampleSetEntry(targetSlot, targetSample);
        if (occupied.loaded)
            confirmOverwrite("Sample " + juce::String(targetSample + 1) + ", " + occupied.name
                + ", is occupied. Replace it with " + audioFile.getFileNameWithoutExtension()
                + "? Press Y or N.", std::move(load), [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                {
                    if (safeThis != nullptr)
                        safeThis->leaveSlotLibraryBrowser(false); // Returns to F1 sample list.

                });
        else load();
        return;
    }

    std::vector<SlotLibrarySelection> work = slotLibrarySelection;
    if (work.empty())
    {
        const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (e.directory) return;
        int target = processor.getCurrentSlot();
        work.push_back({ e.file, target });
    }
    auto load = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), work]
    {
        if (safeThis == nullptr) return;
        safeThis->slotLibraryCell.grabKeyboardFocus();
        if (auto* handler = safeThis->slotLibraryCell.getAccessibilityHandler()) handler->grabFocus();
        safeThis->runFileTask("Loading selected slots", [work](LSampler24AudioProcessor& p) {
            FileTaskResult r; r.ok = true;
            for (size_t i = 0; i < work.size(); ++i) {
                const auto& item = work[i];
                const auto progress = [&p, i, count = work.size()](double fraction) {
                    p.setFileTaskProgress((double(i) + fraction) / double(count));
                };
                if (p.shouldStopFileTask() || !p.loadSlotPresetToSlot(item.file, item.slot, r.message, progress)) { r.ok = false; break; }
                ++r.count; r.slot = juce::jmax(r.slot, item.slot);
            }
            if (r.ok) r.message = "Slots loaded";
            else r.message = "Loaded " + juce::String(r.count) + " slots. " + r.message;
            return r;
        }, [safeThis](const FileTaskResult& r, bool focus) {
            if (safeThis == nullptr) return;
            safeThis->slotLibrarySelection.erase(safeThis->slotLibrarySelection.begin(), safeThis->slotLibrarySelection.begin()
                + juce::jmin(r.count, int(safeThis->slotLibrarySelection.size())));
            if (r.ok && r.slot >= 0) {
                safeThis->processor.setCurrentSlot(r.slot);
                if (focus) safeThis->leaveSlotLibraryBrowser(true);
            }
        });
    };
    int occupied = 0;
    juce::String description;
    for (const auto& item : work)
    {
        if (!processor.isSlotOccupied(item.slot)) continue;
        ++occupied;
        if (description.isEmpty())
            description = "Slot " + juce::String(item.slot + 1) + ", " + replacementSlotName(item.slot)
                + ", is occupied. Replace it with " + cleanLibrarySlotName(item.file) + "?";
    }
    if (occupied > 0)
        confirmOverwrite(work.size() == 1 ? description + " Press Y or N."
            : juce::String(occupied) + " selected destination slots are occupied. " + description
                + " Confirm replacing all? Press Y or N.", std::move(load),
            [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
            {
                if (safeThis != nullptr)
                {
                    safeThis->leaveSlotLibraryBrowser(false);
                    safeThis->returnToActionOrigin(safeThis->loadSlot);
                }
            });
    else load();
}

void LSampler24AudioProcessorEditor::renameCurrentSlot()
{
    const int slot = processor.getCurrentSlot();
    auto currentName = processor.getSlotName(slot);
    if (currentName.isEmpty()) currentName = "Slot " + juce::String(slot + 1);

    auto* window = new juce::AlertWindow("Rename Slot", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("slotName", currentName, "Name");
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prepareNameEntryDialog(*window, "slotName", "Slot name");

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), slot, window](int result)
        {
            if (safeThis != nullptr && result == 1)
            {
                auto name = window->getTextEditorContents("slotName").trim();
                if (name.isNotEmpty())
                {
                    safeThis->processor.setSlotName(slot, name);
                    safeThis->refreshSlotCells();
                }
            }
            if (safeThis != nullptr) safeThis->returnToCurrentSlotAndAnnounce();
            delete window;
        }), false);
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
                const int slot = safeThis->processor.getCurrentSlot();
                auto load = [safeThis, file, slot]
                {
                    if (safeThis == nullptr) return;
                    safeThis->runFileTask("Loading slot", [file, slot](LSampler24AudioProcessor& p) {
                        FileTaskResult r; r.ok = p.loadSlotPresetToSlot(file, slot, r.message);
                        if (r.ok) r.message = "Slot loaded: " + file.getFileNameWithoutExtension();
                        return r;
                    }, [safeThis](const FileTaskResult&, bool focus) {
                        if (safeThis != nullptr && focus) safeThis->returnToCurrentSlotAndAnnounce();
                    });
                };
                if (safeThis->processor.isSlotOccupied(slot))
                    safeThis->confirmOverwrite("Slot " + juce::String(slot + 1) + ", "
                        + safeThis->replacementSlotName(slot) + ", is occupied. Replace it with "
                        + file.getFileNameWithoutExtension() + "? Press Y or N.", std::move(load),
                        [safeThis] { if (safeThis != nullptr) safeThis->returnToActionOrigin(safeThis->loadSlot); });
                else load();
                return;
            }
            safeThis->returnToActionOrigin(safeThis->loadSlot);
        });
}

void LSampler24AudioProcessorEditor::chooseSaveSlot()
{
    promptLibrarySaveName(false);
}

// Two-stage, screen-reader-friendly Save As.  The name is committed before
// opening the *same* internal browser used by Alt+S / Alt+B.  No native OS
// file chooser can escape the two Library roots.
void LSampler24AudioProcessorEditor::promptLibrarySaveName(bool forBank)
{
    if (!requireLibraryAvailable(forBank ? static_cast<juce::Component*>(&saveBank)
                                         : static_cast<juce::Component*>(&saveSlot), true)) return;
    if (slotLibraryActive) return;
    auto name = juce::String();
    if (forBank)
    {
        name = processor.getCurrentBankName().trim();
        if (name.isEmpty()) name = LibraryManager::defaultName("Bank");
    }
    else
    {
        // Use the CURRENT SLOT PRESET filename first.  An edited slot can
        // retain that preset filename even when its display label differs.
        auto preset = processor.getCurrentSlotPresetFile();
        if (preset.getFullPathName().isNotEmpty())
            name = cleanLibrarySlotName(preset);
        if (name.isEmpty()) name = processor.getSlotName(processor.getCurrentSlot()).trim();
        if (name.isEmpty()) name = processor.getCurrentSampleFile().getFileNameWithoutExtension();
        if (name.isEmpty()) name = "Slot " + juce::String(processor.getCurrentSlot() + 1);
    }
    if (forBank && name.endsWithIgnoreCase(LibraryManager::bankExtension))
        name = name.dropLastCharacters(juce::String(LibraryManager::bankExtension).length());
    if (!forBank && name.startsWithIgnoreCase("Slot_")) name = name.substring(5);
    name = juce::File::createLegalFileName(name.trim());

    auto* dialog = new juce::AlertWindow(forBank ? "Save Bank" : "Save Slot",
                                          {}, juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("presetName", name, "File name");
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prepareNameEntryDialog(*dialog, "presetName", "File name");
    const int savingSlot = processor.getCurrentSlot();
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
         dialog, forBank, savingSlot](int result)
        {
            if (safeThis == nullptr) return;
            if (result != 1)
            {
                safeThis->returnToActionOrigin(forBank ? safeThis->saveBank : safeThis->saveSlot);
                return;
            }
            const auto typed = dialog->getTextEditorContents("presetName").trim();
            const auto clean = juce::File::createLegalFileName(typed).trim();
            if (clean.isEmpty() || clean == "." || clean == "..")
            {
                lsampler::announceToActiveScreenReader(safeThis->status, "Invalid file name");
                safeThis->returnToActionOrigin(forBank ? safeThis->saveBank : safeThis->saveSlot);
                return;
            }
            // Enter the browser after the modal Name window has been dismissed;
            // otherwise the disappearing dialog may steal NVDA keyboard focus.
            juce::MessageManager::callAsync([safeThis, forBank, savingSlot, clean]
            {
                if (safeThis == nullptr) return;
                safeThis->slotLibrarySaving = true;
                safeThis->slotLibrarySaveSlot = savingSlot;
                safeThis->slotLibrarySaveName = clean;
                safeThis->enterSlotLibraryBrowser(false, forBank);
                if (!safeThis->slotLibraryActive)
                {
                    safeThis->slotLibrarySaving = false;
                    safeThis->slotLibrarySaveName.clear();
                }
            });
        }), true);
}

void LSampler24AudioProcessorEditor::commitLibrarySave()
{
    if (!slotLibraryActive || !slotLibrarySaving) return;
    const bool bank = slotLibraryForBank;
    const auto folder = slotLibraryDirectory;
    if (!folder.isDirectory() || (folder != slotLibraryRoot && !folder.isAChildOf(slotLibraryRoot)))
    {
        lsampler::announceToActiveScreenReader(slotLibraryCell, "Save folder is outside the Library");
        return;
    }
    auto clean = juce::File::createLegalFileName(slotLibrarySaveName.trim());
    if (bank && clean.endsWithIgnoreCase(LibraryManager::bankExtension))
        clean = clean.dropLastCharacters(juce::String(LibraryManager::bankExtension).length());
    if (!bank && clean.startsWithIgnoreCase("Slot_")) clean = clean.substring(5);
    if (clean.isEmpty() || clean == "." || clean == "..")
    {
        lsampler::announceToActiveScreenReader(slotLibraryCell, "Invalid file name");
        return;
    }
    const auto filename = bank ? clean + LibraryManager::bankExtension
                               : "Slot_" + clean + LibraryManager::slotExtension;
    const auto file = folder.getChildFile(filename);
    const int slot = slotLibrarySaveSlot;
    const auto save = [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
                       bank, file, slot, clean]
    {
        if (safeThis == nullptr) return;
        safeThis->runFileTask(bank ? "Saving bank" : "Saving slot",
            [bank, file, slot, clean](LSampler24AudioProcessor& processor)
            {
                FileTaskResult r;
                if (bank) r.ok = processor.saveBankPreset(file, r.message);
                else
                {
                    processor.setSlotName(slot, clean);
                    r.ok = processor.saveSlotPresetAt(file, slot, r.message);
                }
                if (r.ok) r.message = juce::String(bank ? "Bank saved: " : "Slot saved: ")
                                       + file.getFileName();
                return r;
            }, [safeThis](const FileTaskResult& result, bool focus)
            {
                if (safeThis == nullptr || !result.ok) return;
                safeThis->leaveSlotLibraryBrowser(false);
                if (focus) safeThis->returnToCurrentSlotAndAnnounce();
            });
    };
    if (file.exists())
        confirmOverwrite((bank ? "Bank " : "Slot ") + clean
                             + " already exists. Overwrite? Press Y or N.", save,
                         [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
                         {
                             if (safeThis != nullptr && safeThis->slotLibraryActive)
                             {
                                 safeThis->slotLibraryCell.grabKeyboardFocus();
                                 safeThis->announceSlotLibraryEntry();
                             }
                         });
    else save();
}

void LSampler24AudioProcessorEditor::createSlotLibraryFolder()
{
    if (!slotLibraryActive || slotLibraryForSampleSet) return;
    const auto folder = slotLibraryDirectory;
    if (folder != slotLibraryRoot && !folder.isAChildOf(slotLibraryRoot)) return;
    auto* dialog = new juce::AlertWindow("New folder", {}, juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("folderName", "", "Folder name");
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prepareNameEntryDialog(*dialog, "folderName", "Folder name");
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), dialog, folder](int result)
        {
            if (safeThis == nullptr || !safeThis->slotLibraryActive) return;
            if (result == 1)
            {
                const auto name = juce::File::createLegalFileName(dialog->getTextEditorContents("folderName").trim());
                if (name.isEmpty() || name == "." || name == "..")
                    lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Invalid folder name");
                else
                {
                    const auto target = folder.getChildFile(name);
                    if (target.exists())
                        lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Folder already exists");
                    else if (!target.createDirectory().wasOk())
                        lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Cannot create folder");
                    else
                    {
                        safeThis->slotLibraryDirectorySelectionMemory[folder.getFullPathName()] = target.getFullPathName();
                        safeThis->refreshSlotLibraryEntries();
                        safeThis->announceSlotLibraryEntry();
                    }
                }
            }
            safeThis->slotLibraryCell.grabKeyboardFocus();
        }), true);
}

void LSampler24AudioProcessorEditor::renameSlotLibraryEntry()
{
    if (!slotLibraryActive || slotLibraryForSampleSet || slotLibraryEntries.empty()) return;
    const auto source = slotLibraryEntries[size_t(slotLibraryEntryIndex)].file;
    if (!source.exists() || source == slotLibraryRoot || !source.isAChildOf(slotLibraryRoot)) return;
    const bool directory = source.isDirectory();
    const bool bank = slotLibraryForBank;
    const auto initial = directory ? source.getFileName()
                        : bank ? cleanLibraryBankName(source) : cleanLibrarySlotName(source);
    auto* dialog = new juce::AlertWindow(directory ? "Rename folder" : "Rename file",
                                          {}, juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("newName", initial, "New name");
    dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prepareNameEntryDialog(*dialog, "newName", "New name");
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this),
         dialog, source, directory, bank](int result)
        {
            if (safeThis == nullptr || !safeThis->slotLibraryActive) return;
            if (result == 1)
            {
                auto name = juce::File::createLegalFileName(dialog->getTextEditorContents("newName").trim());
                if (!directory && bank && name.endsWithIgnoreCase(LibraryManager::bankExtension))
                    name = name.dropLastCharacters(juce::String(LibraryManager::bankExtension).length());
                if (!directory && !bank && name.startsWithIgnoreCase("Slot_")) name = name.substring(5);
                if (name.isEmpty() || name == "." || name == "..")
                    lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Invalid name");
                else
                {
                    const juce::String filename = directory ? name
                        : bank ? juce::String(source.getFileName().startsWithIgnoreCase("Bank_") ? "Bank_" : "")
                                 + name + LibraryManager::bankExtension
                        : juce::String("Slot_") + name + LibraryManager::slotExtension;
                    const auto target = source.getSiblingFile(filename);
                    if (target == source)
                        lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Name unchanged");
                    else if (target.exists())
                        lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Element already exists");
                    else if (!source.moveFileTo(target))
                        lsampler::announceToActiveScreenReader(safeThis->slotLibraryCell, "Rename failed");
                    else
                    {
                        safeThis->slotLibraryDirectorySelectionMemory[safeThis->slotLibraryDirectory.getFullPathName()]
                            = target.getFullPathName();
                        safeThis->refreshSlotLibraryEntries();
                        safeThis->announceSlotLibraryEntry();
                    }
                }
            }
            safeThis->slotLibraryCell.grabKeyboardFocus();
        }), true);
}

void LSampler24AudioProcessorEditor::deleteSlotLibraryEntry()
{
    if (!slotLibraryActive || slotLibraryForSampleSet || slotLibraryEntries.empty()) return;
    const auto source = slotLibraryEntries[size_t(slotLibraryEntryIndex)].file;
    if (!source.exists() || source == slotLibraryRoot || !source.isAChildOf(slotLibraryRoot)) return;
    const auto description = source.isDirectory() ? "folder " : "file ";
    confirmOverwrite(juce::String("Delete ") + description + source.getFileName()
                         + (source.isDirectory() ? " and everything inside it" : "")
                         + "? Press Y or N.",
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), source]
        {
            if (safeThis == nullptr) return;
            safeThis->runFileTask("Deleting Library element", [source](LSampler24AudioProcessor&)
            {
                FileTaskResult r;
                r.ok = source.isDirectory() ? source.deleteRecursively() : source.deleteFile();
                r.message = r.ok ? "Element deleted" : "Delete failed";
                return r;
            }, [safeThis, source](const FileTaskResult& result, bool focus)
            {
                if (safeThis == nullptr || !safeThis->slotLibraryActive || !result.ok) return;
                safeThis->slotLibraryDirectorySelectionMemory.erase(safeThis->slotLibraryDirectory.getFullPathName());
                safeThis->slotLibrarySelection.erase(
                    std::remove_if(safeThis->slotLibrarySelection.begin(), safeThis->slotLibrarySelection.end(),
                        [&source](const SlotLibrarySelection& item) { return item.file == source; }),
                    safeThis->slotLibrarySelection.end());
                safeThis->refreshSlotLibraryEntries();
                if (focus) { safeThis->slotLibraryCell.grabKeyboardFocus(); safeThis->announceSlotLibraryEntry(); }
            });
        }, [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
        {
            if (safeThis != nullptr && safeThis->slotLibraryActive)
            {
                safeThis->slotLibraryCell.grabKeyboardFocus();
                safeThis->announceSlotLibraryEntry();
            }
        }, "Confirm deletion");
}

void LSampler24AudioProcessorEditor::showSlotLibraryContextMenu()
{
    if (!slotLibraryActive || slotLibraryForSampleSet) return;
    juce::PopupMenu menu;
    menu.addSectionHeader("Library folder and file actions");
    menu.addItem(1, "New Folder");
    menu.addItem(2, slotLibraryEntries.empty() ? "Rename (F2)"
                    : slotLibraryEntries[size_t(slotLibraryEntryIndex)].file.isDirectory()
                        ? "Rename folder (F2)" : "Rename file (F2)",
                 !slotLibraryEntries.empty());
    menu.addItem(3, "Delete Element", !slotLibraryEntries.empty());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slotLibraryCell),
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](int result)
        {
            if (safeThis == nullptr || !safeThis->slotLibraryActive) return;
            if (result == 1) safeThis->createSlotLibraryFolder();
            else if (result == 2) safeThis->renameSlotLibraryEntry();
            else if (result == 3) safeThis->deleteSlotLibraryEntry();
            else safeThis->slotLibraryCell.grabKeyboardFocus();
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
                auto load = [safeThis, file]
                {
                    if (safeThis == nullptr) return;
                    safeThis->runFileTask("Loading bank", [file](LSampler24AudioProcessor& p) {
                        FileTaskResult r; r.ok = p.loadBankPreset(file, r.message);
                        if (r.ok) r.message = "Bank loaded: " + file.getFileNameWithoutExtension();
                        return r;
                    }, [safeThis](const FileTaskResult&, bool focus) {
                        if (safeThis != nullptr && focus) safeThis->returnToCurrentSlotAndAnnounce();
                    });
                };
                const int occupied = safeThis->occupiedBankSlotCount();
                if (occupied > 0)
                    safeThis->confirmOverwrite("The current bank contains " + juce::String(occupied)
                        + " occupied slots. Replace it with " + file.getFileNameWithoutExtension()
                        + "? Press Y or N.", std::move(load),
                        [safeThis] { if (safeThis != nullptr) safeThis->returnToActionOrigin(safeThis->loadBank); });
                else load();
                return;
            }
            safeThis->returnToActionOrigin(safeThis->loadBank);
        });
}

void LSampler24AudioProcessorEditor::chooseSaveBank()
{
    promptLibrarySaveName(true);
}

void LSampler24AudioProcessorEditor::openSliceEditor(bool sequencer)
{
    if(sliceEditor)return;
    const int slot=processor.getCurrentSlot();int start=0;double rate=0;
    if(processor.getSliceLayout(slot,start,rate).count==0) {
        lsampler::announceToActiveScreenReader(*this,"No sample loaded");return;
    }
    sliceReturnFocus=juce::Component::getCurrentlyFocusedComponent();
    processor.requestPreviewStop();processor.requestImportPreviewStop();processor.requestLibraryPreviewStop();
    sliceEditor=std::make_unique<SliceEditor>(processor,slot,sequencer);
    sliceEditor->onHostAutomationEnvelopeState = [safeThis=juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
        (juce::AudioProcessorParameter* parameter) -> int
    {
        return safeThis != nullptr ? safeThis->getHostAutomationEnvelopeState(parameter) : -1;
    };
    sliceEditor->onToggleHostAutomationEnvelope = [safeThis=juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
        (juce::AudioProcessorParameter* parameter, juce::Component* target)
    {
        return safeThis != nullptr && safeThis->toggleHostAutomationEnvelope(parameter, target);
    };
    sliceEditor->onRequestFX=[safeThis=juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)] {
        // Modal slice focus must be dismissed on the message queue, never
        // destroy SliceEditor from inside its own keyPressed callback.
        juce::MessageManager::callAsync([safeThis] {
            if(safeThis!=nullptr) { safeThis->closeSliceEditor(); safeThis->openFX(); }
        });
    };
    sliceEditor->onClose=[safeThis=juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)] {
        // Never delete the focused component in the middle of its keyPressed call.
        juce::MessageManager::callAsync([safeThis]{if(safeThis!=nullptr)safeThis->closeSliceEditor();});
    };
    addAndMakeVisible(*sliceEditor);sliceEditor->setBounds(getLocalBounds());sliceEditor->toFront(false);
    sliceEditor->enterModalState(true,nullptr,false);
    juce::MessageManager::callAsync([safeThis=juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)] {
        if(safeThis!=nullptr&&safeThis->sliceEditor)safeThis->sliceEditor->announceEntry();
    });
}
void LSampler24AudioProcessorEditor::closeSliceEditor()
{
    if(!sliceEditor)return;
    processor.stopSlicePreview();sliceEditor->exitModalState(0);sliceEditor.reset();
    if(sliceReturnFocus!=nullptr&&sliceReturnFocus->isShowing()) {
        sliceReturnFocus->grabKeyboardFocus();
        if(auto* h=sliceReturnFocus->getAccessibilityHandler())h->grabFocus();
    } else if(parameterPage)parameterSelector.grabKeyboardFocus();
    else returnToCurrentSlotAndAnnounce();
    sliceReturnFocus=nullptr;repaint();
}
