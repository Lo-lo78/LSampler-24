#include "PluginEditor.h"
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
    // Show keyboard equivalents directly on the mouse buttons so sighted users
    // discover the accessible workflow while exploring the interface.
    loadSample.setButtonText("Load Sample  Alt+O");
    loadSlot.setButtonText("Load Slot  Alt+S");
    saveSlot.setButtonText("Save Slot  Alt+Shift+S");
    loadBank.setButtonText("Load Bank  Alt+B");
    saveBank.setButtonText("Save Bank  Alt+Shift+B");
    help.setButtonText("Help  Alt+H");
    help.onClick = [this] { showHelpLanguageMenu(); };
    aboutButton.setButtonText("About  Alt+A");
    aboutButton.setDescription("Alt+A");
    aboutButton.onClick = [this] { openAbout(); };
    advancedButton.setButtonText("Advanced  Alt+V");
    advancedButton.setName("Advanced");
    advancedButton.setDescription("Advanced settings. Shortcut Alt+V on the Slot page.");
    advancedButton.onClick = [this] { showAdvancedMenu(); };

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
    selectedParameter = juce::jlimit(0, normalGridSize - 1,
                                     processor.getSlotGridPosition(processor.getCurrentSlot()));
    for (int i = 0; i < normalGridSize; ++i)
        parameterSelector.addItem(parameterCellText(i), i + 1);
    parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
    parameterSelector.onChange = [this]
    {
        const int index = parameterSelector.getSelectedItemIndex();
        if (index >= 0)
        {
            const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
            selectedParameter = globalOpen ? normalGridSize + index : index;
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
    parameterValue.setShortcutHandler([this](const juce::KeyPress& key, juce::Component* source)
    {
        return handleKeyPress(key, source);
    });
    parameterValue.addKeyListener(this);
    parameterValue.onValueChange = [this]
    {
        if (!parameterPage) return;
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

    loadSample.onClick = [this] { enterImportBrowser(); };
    loadSlot.onClick   = [this] { enterSlotLibraryBrowser(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { enterSlotLibraryBrowser(false, true); };
    saveBank.onClick   = [this] { chooseSaveBank(); };
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
        g.drawText(globalOpen?"Enter: Confirm  /  Esc: Cancel  /  Alt+V: Value":parameterPage?"Alt+V: Value  /  Alt+L: Loop On-Off  /  Alt+M: Sample Set  /  Alt+E: Slice":"Enter: Edit selected slot  /  Alt+V: Advanced  /  Alt+M: Sample Set  /  Alt+E: Slice",384,588,getWidth()-400,24,juce::Justification::centredLeft);
    }
}

void LSampler24AudioProcessorEditor::resized()
{
    if(sliceEditor)sliceEditor->setBounds(getLocalBounds());
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
        auto buttons=area.removeFromTop(36);
        if(importBrowserActive) {importSourceCombo.setBounds(buttons.removeFromLeft(250));buttons.removeFromLeft(10);}
        exportLibraryButton.setBounds(buttons.removeFromLeft(220));buttons.removeFromLeft(10);
        exportAllLibraryButton.setBounds(buttons.removeFromLeft(220));buttons.removeFromLeft(10);
        importLibraryButton.setBounds(buttons.removeFromLeft(220));
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
    if (slotPreviewMode && !parameterPage && !propertiesOpen)
        processor.requestPreviewStart();
}


void LSampler24AudioProcessorEditor::returnToCurrentSlotAndAnnounce()
{
    const int slot = processor.getCurrentSlot();
    refreshSlotCells();

    // Moving keyboard/accessibility focus to the slot already makes NVDA announce
    // the slot title.  Do not send a second explicit announcement here: doing both
    // caused the same slot message to overlap itself after Alt+L and after closing
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
    auto options = juce::PopupMenu::Options();
    if (popupTarget != nullptr)
        options = options.withTargetComponent(popupTarget);

    menu.showMenuAsync(options,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), returnFocus](int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 0)
            {
                juce::Timer::callAfterDelay(50,
                    [returnFocus]
                    {
                        if (returnFocus == nullptr)
                            return;
                        returnFocus->grabKeyboardFocus();
                        if (auto* handler = returnFocus->getAccessibilityHandler())
                            handler->grabFocus();
                    });
                return;
            }

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

const GridEntry& LSampler24AudioProcessorEditor::selectedEntry() const {return lsampler::grid[size_t(selectedParameter)];}
int LSampler24AudioProcessorEditor::categoryBegin(int index) const {
    const auto* name=lsampler::grid[size_t(index)].category;
    while(index>0 && std::strcmp(lsampler::grid[size_t(index-1)].category,name)==0)--index;
    return index;
}
int LSampler24AudioProcessorEditor::categoryEnd(int index) const {
    const auto* name=lsampler::grid[size_t(index)].category;
    while(index+1<int(lsampler::grid.size()) && std::strcmp(lsampler::grid[size_t(index+1)].category,name)==0)++index;
    return index;
}
bool LSampler24AudioProcessorEditor::selectedRateIsSynced() const {
    const auto id=selectedEntry().parameter;
    if(id!=int(P::lfo1_rate)&&id!=int(P::lfo2_rate))return false;
    const auto sync=id==int(P::lfo1_rate)?P::lfo1_bpm_sync:P::lfo2_bpm_sync;
    for(size_t i=0;i<lsampler::grid.size();++i)if(lsampler::grid[i].parameter==int(sync))return processor.getSlotParameter(int(i))!=0;
    return false;
}
juce::String LSampler24AudioProcessorEditor::selectedParameterName() const {
    const auto& e=selectedEntry();
    const juce::String name = e.parameter == int(P::global_one_shot) ? "Main Playback Mode" : juce::String(descriptor(e).name);
    return (std::strcmp(e.category,"Loops")==0?"Loop "+juce::String(selectedLoop+1)+" ":juce::String())+name;
}
double LSampler24AudioProcessorEditor::getSelectedParameterValue() const {return processor.getSlotParameter(selectedParameter,selectedLoop);}
void LSampler24AudioProcessorEditor::setSelectedParameterValue(double value)
{
    const auto parameter = selectedEntry().parameter;
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
juce::String LSampler24AudioProcessorEditor::selectedParameterValueText() const {return formatParameter(selectedParameter,getSelectedParameterValue());}
juce::String LSampler24AudioProcessorEditor::parameterCellText(int index) const {
    const auto& e=lsampler::grid[size_t(index)];
    const juce::String prefix=std::strcmp(e.category,"Loops")==0?"Loop "+juce::String(selectedLoop+1)+" ":juce::String();
    const juce::String name = e.parameter == int(P::global_one_shot) ? "Main Playback Mode" : juce::String(descriptor(e).name);
    return prefix+name+", "+formatParameter(index,processor.getSlotParameter(index,selectedLoop));
}
void LSampler24AudioProcessorEditor::refreshParameterGrid() {
    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
    parameterSelector.clear(juce::dontSendNotification);
    if (globalOpen)
    {
        for (int i = 0; i < lsampler::globalParameterCount; ++i)
            parameterSelector.addItem(parameterCellText(normalGridSize + i), i + 1);
        const int localIndex = juce::jlimit(0, lsampler::globalParameterCount - 1, selectedParameter - normalGridSize);
        parameterSelector.setSelectedItemIndex(localIndex, juce::dontSendNotification);
    }
    else
    {
        for (int i = 0; i < normalGridSize; ++i)
            parameterSelector.addItem(parameterCellText(i), i + 1);
        selectedParameter = juce::jlimit(0, normalGridSize - 1, selectedParameter);
        parameterSelector.setSelectedItemIndex(selectedParameter,juce::dontSendNotification);
    }
    configureValueForSelectedParameter();
    // Navigation changes the displayed parameter/loop without changing audio state.
    refreshVisuals();
}
void LSampler24AudioProcessorEditor::configureValueForSelectedParameter() {
    const auto& d=descriptor(selectedEntry());
    parameterValue.setRange(selectedRateIsSynced()?.125:d.minimum,d.maximum,
        d.kind==Kind::integer||d.kind==Kind::enumeration||d.kind==Kind::note||d.kind==Kind::action?1.0:0.0);
    parameterValue.textFromValueFunction=[this](double v){return formatParameter(selectedParameter,v);};
    parameterValue.valueFromTextFunction=[this](const juce::String& text) {
        const auto& desc=descriptor(selectedEntry());
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
        editor->setText(juce::String(getSelectedParameterValue(),descriptor(selectedEntry()).decimals),false);
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
    selectedParameter = juce::jlimit(0, normalGridSize - 1,
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
    if (parameterPage)
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
        selectedParameter = juce::jlimit(0, normalGridSize - 1, globalReturnSelectedParameter);
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
    const int maximumIndex = globalOpen ? static_cast<int>(lsampler::grid.size()) - 1 : normalGridSize - 1;
    index = juce::jlimit(minimumIndex, maximumIndex, index);
    if(index==selectedParameter)return;
    selectedParameter = index;
    if (!globalOpen)
        processor.setSlotGridPosition(processor.getCurrentSlot(), selectedParameter);
    parameterSelector.setSelectedItemIndex(globalOpen ? selectedParameter - normalGridSize : selectedParameter,
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
    const auto& d=descriptor(selectedEntry());
    const double baseStep = d.step;
    const double multiplier = static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)])
                            * (coarse ? static_cast<double>(valuePageStep) : 1.0);
    const double step = selectedRateIsSynced()?.125:baseStep*multiplier;
    const double maximum = d.maximum;
    const auto current = getSelectedParameterValue();
    const auto next = juce::jlimit(selectedRateIsSynced()?.125:d.minimum, maximum, current + direction * step);
    if (std::abs(next - current) < 1.0e-9)
        return;
    setSelectedParameterValue(next);
    refreshParameterGrid();
    announceSelectedValue();
}

void LSampler24AudioProcessorEditor::setSelectedParameterBoundary(bool maximum)
{
    const auto& d=descriptor(selectedEntry());
    const double value = maximum?d.maximum:(selectedRateIsSynced()?.125:d.minimum);
    if (std::abs(value - getSelectedParameterValue()) < 1.0e-9)
        return;
    setSelectedParameterValue(value);
    refreshParameterGrid();
    announceSelectedValue();
}

void LSampler24AudioProcessorEditor::setMainControlsEnabled(bool enabled)
{
    for (auto& cell : slotCells) cell.setEnabled(enabled);
    for (auto* control : std::array<juce::Component*, 10> {
             &loadSample, &loadSlot, &saveSlot, &loadBank, &saveBank, &help, &aboutButton, &advancedButton,
             &parameterSelector, &parameterValue })
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
        || component == &loadBank || component == &saveBank || component == &help || component == &aboutButton || component == &advancedButton;
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
    if (fileUiBusy || processor.isFileTaskRunning())
        return key.getKeyCode() != juce::KeyPress::escapeKey;

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

        if (code == juce::KeyPress::tabKey)
        {
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

        if (code == juce::KeyPress::escapeKey) { const bool wasExport = slotLibraryForExport; leaveSlotLibraryBrowser(!wasExport); if (wasExport) returnToAdvancedAndAnnounce(); return true; }

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
        if (!slotLibraryForSampleSet && !slotLibraryForBank && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown())
        {
            const auto ctrlChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (ctrlChar == 'c') { copySlotLibraryEntry(false); return true; }
            if (ctrlChar == 'x') { copySlotLibraryEntry(true); return true; }
            if (ctrlChar == 'v') { pasteSlotLibraryEntry(); return true; }
        }
        if (!slotLibraryForSampleSet && !slotLibraryForBank && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            moveSlotLibraryDestination(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (!slotLibraryForSampleSet && !slotLibraryForBank && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()
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
        if (!slotLibraryForSampleSet && !slotLibraryForBank && code == juce::KeyPress::spaceKey && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        { toggleSlotLibrarySelection(); return true; }
        if (code == juce::KeyPress::spaceKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
            if (slotLibraryForBank) return true; // Banks have no audio preview.
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
            if (slotLibraryEntries.empty()) return true;
            const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
            if (e.directory)
            {
                if (slotLibraryForExport) { chooseExportFolder(); return true; }
                slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = e.file.getFullPathName();
                slotLibraryDirectory = e.file; refreshSlotLibraryEntries(); selectSlotLibraryEntry(slotLibraryEntryIndex, true); return true;
            }
            if (slotLibraryForExport) { lsampler::announceToActiveScreenReader(slotLibraryCell, "Select a folder to export"); return true; }
            commitSlotLibrarySelection(); return true;
        }
        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            const auto typed = juce::juce_wchar(code);
            if (typed >= 33 && typed <= 126)
            {
                cycleSlotLibraryEntryByInitial(typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }
        return true;
    }

    if (importBrowserActive)
    {
        const bool onImportSource = source == &importSourceCombo;
        const bool onExportLibrary = source == &exportLibraryButton;
        const bool onExportAllLibrary = source == &exportAllLibraryButton;
        const bool onImportLibrary = source == &importLibraryButton;
        const auto focusImportBrowser = [this]
        {
            importBrowserCell.grabKeyboardFocus();
            juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
            {
                if (safeThis == nullptr || !safeThis->importBrowserActive) return;
                lsampler::announceToActiveScreenReader(safeThis->importBrowserCell, safeThis->importForSampleSet
                    ? "Sample Browser. Enter loads the selected file into the current Sample Set position."
                    : "Sample Browser. Enter loads selected items into slots.");
            });
        };
        if (code == juce::KeyPress::tabKey)
        {
            if (importForSampleSet) { importBrowserCell.grabKeyboardFocus(); return true; }
            if (mods.isShiftDown())
            {
                if (onImportSource) focusImportBrowser();
                else if (onExportLibrary) importSourceCombo.grabKeyboardFocus();
                else if (onExportAllLibrary) exportLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) exportAllLibraryButton.grabKeyboardFocus();
                else importLibraryButton.grabKeyboardFocus();
            }
            else
            {
                if (onImportSource) exportLibraryButton.grabKeyboardFocus();
                else if (onExportLibrary) exportAllLibraryButton.grabKeyboardFocus();
                else if (onExportAllLibrary) importLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) focusImportBrowser();
                else importSourceCombo.grabKeyboardFocus();
            }
            return true;
        }
        if (onImportSource || onExportLibrary || onExportAllLibrary || onImportLibrary)
        {
            if (onImportSource && !mods.isCtrlDown() && !mods.isAltDown()
                && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
                    || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                    || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey))
                return false; // Let the ComboBox perform normal accessible selection.

            if (code == juce::KeyPress::escapeKey) { leaveImportBrowser(true, true); return true; }
            if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
            {
                if (onImportSource)
                {
                    if (importSourceCombo.getSelectedId() == 2) chooseImportFolder(); else chooseImportFiles();
                }
                else if (onExportLibrary) chooseExportFolder();
                else if (onExportAllLibrary) chooseExportLibrary();
                else chooseImportLibrary();
                return true;
            }
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            if (importRecentPathsMode) { leaveImportRecentPaths(); return true; }
            leaveImportBrowser(true, true); return true;
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
            const auto typed = juce::juce_wchar(code);
            if (typed >= 33 && typed <= 126)
            {
                cycleImportEntryByInitial(typed, mods.isShiftDown() ? -1 : 1);
                return true;
            }
        }
        return true;
    }

    if (sampleSetActive)
    {
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
        if (code == juce::KeyPress::tabKey && !mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown())
        {
            sampleSetValueFocus = false;
            sampleSetGridFocus = !sampleSetGridFocus;
            refreshSampleSetCell(false);
            if (sampleSetGridFocus)
                lsampler::announceToActiveScreenReader(sampleSetCell, "Grid. " + sampleSetParameterText());
            else
            {
                const auto info = processor.getSampleSetEntry(processor.getCurrentSlot(), sampleSetIndex);
                const auto sampleName = info.loaded ? (info.name.isNotEmpty() ? info.name : juce::String("Loaded")) : juce::String("Empty");
                lsampler::announceToActiveScreenReader(sampleSetCell,
                    "Sample " + juce::String(sampleSetIndex + 1) + " of " + juce::String(LSampler24AudioProcessor::sampleSetSize)
                    + ", " + sampleName + ", Velocity " + juce::String(info.velocityLow) + " to " + juce::String(info.velocityHigh));
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
                        processor.requestImportPreviewStop();
                        sampleSetIndex = 0;
                        refreshSampleSetCell(true);
                    }
                    return true;
                }
                if (code == juce::KeyPress::endKey)
                {
                    const int last = LSampler24AudioProcessor::sampleSetSize - 1;
                    if (sampleSetIndex != last)
                    {
                        processor.requestImportPreviewStop();
                        sampleSetIndex = last;
                        refreshSampleSetCell(true);
                    }
                    return true;
                }
                if (code == juce::KeyPress::deleteKey)
                {
                    processor.requestImportPreviewStop();
                    processor.clearSampleSetEntry(processor.getCurrentSlot(), sampleSetIndex);
                    refreshSlotCells(); refreshSampleSetCell(true); return true;
                }
                if (code == juce::KeyPress::spaceKey) { previewSampleSetEntry(); return true; }
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
                    constexpr int total = 3;
                    const int columnStart = (sampleSetField / rows) * rows;
                    const int columnEnd = juce::jmin(total - 1, columnStart + rows - 1);
                    if (sampleSetField != columnEnd) { sampleSetField = columnEnd; refreshSampleSetCell(true); }
                    return true;
                }
            }
        }
        return true; // Sample Set is modal: never leak shortcuts to REAPER.
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'v' && !parameterPage)
    {
        advancedButton.grabKeyboardFocus();
        if (auto* handler = advancedButton.getAccessibilityHandler()) handler->grabFocus();
        showAdvancedMenu();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'h')
    {
        showHelpLanguageMenu();
        return true;
    }

    if (!mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown()
        && mods.isAltDown() && ch == 'a')
    {
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
            // On the slot page the default is DAW Space. Alt+P enables the
            // selection-following configured-slot audition mode.
            slotPreviewMode = !slotPreviewMode;
            if (slotPreviewMode)
            {
                processor.requestPreviewStart();
                lsampler::announceToActiveScreenReader(*announceSource, "Slot preview On");
            }
            else
            {
                processor.requestPreviewStop();
                lsampler::announceToActiveScreenReader(*announceSource, "DAW space On");
            }
        }
        return true;
    }

    if(!mods.isCtrlDown()&&!mods.isShiftDown()&&!mods.isCommandDown()
       && mods.isAltDown()&&(ch=='e'||code=='E')) {
        openSliceEditor(false);return true;
    }

    const auto moveParameterPage = [this](int direction)
    {
        const int begin = categoryBegin(selectedParameter);
        const int end = categoryEnd(selectedParameter);
        const int row = selectedParameter - begin;
        if (direction < 0 && begin > 0)
        {
            const int previous = categoryBegin(begin - 1);
            selectParameter(juce::jmin(previous + row, begin - 1), true);
        }
        else if (direction > 0 && end + 1 < static_cast<int>(lsampler::grid.size()))
        {
            const int next = end + 1;
            selectParameter(juce::jmin(next + row, categoryEnd(next)), true);
        }
    };

    if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown() && ch == 's')
    {
        if (mods.isShiftDown())
            chooseSaveBank();
        else
            chooseSaveSlot();
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown() && ch == 'g')
    {
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

    // Page/category navigation remains available, now on Alt+Shift+Left/Right.
    // Ctrl+Left/Right is reserved for Sample Play Start scrubbing.
    if (parameterPage && !globalOpen && !sourceIsValueEditor && mods.isAltDown() && mods.isShiftDown()
        && !mods.isCtrlDown() && !mods.isCommandDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }

    // Ctrl+Up/Down changes the current loaded slot while keeping the parameter grid open.
    // Empty slots are skipped, there is no wrap, and boundaries stay silent when
    // more than one loaded slot exists.  If this is the only loaded slot, announce
    // that fact so the user knows there is nowhere else to navigate.
    if (parameterPage && !globalOpen && !sourceIsValueEditor && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
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
            selectedParameter = juce::jlimit(0, normalGridSize - 1, processor.getSlotGridPosition(found));
            parameterSelector.setSelectedItemIndex(selectedParameter, juce::dontSendNotification);
            refreshSlotCells();
            refreshParameterGrid();
            configureValueForSelectedParameter();
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
    if (parameterPage && !globalOpen && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
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
        if (ch == 'o' && !mods.isShiftDown()) { enterImportBrowser(); return true; }
        if (ch == 's' && !mods.isShiftDown()) { enterSlotLibraryBrowser(); return true; }
        if (ch == 'b' && !mods.isShiftDown()) { enterSlotLibraryBrowser(false, true); return true; }
        if (ch == 's' && mods.isShiftDown()) { chooseSaveSlot(); return true; }
        if (ch == 'b' && mods.isShiftDown()) { chooseSaveBank(); return true; }
        if (ch == 'v' && parameterPage && !sourceIsValueEditor) { focusValue(); return true; }
        if (ch == 'l' && parameterPage && !globalOpen)
        {
            for (int i = 0; i < static_cast<int>(lsampler::grid.size()); ++i)
                if (lsampler::grid[static_cast<size_t>(i)].parameter == int(lsampler::P::global_one_shot)
                    && std::strcmp(lsampler::grid[static_cast<size_t>(i)].category, "Sample Window") == 0)
                {
                    const double current = processor.getSlotParameter(i);
                    const bool loopWillBeOn = current != 0.0;
                    processor.setSlotParameter(i, loopWillBeOn ? 0.0 : 1.0);
                    refreshParameterGrid();
                    auto* announceSource = source != nullptr ? source : static_cast<juce::Component*>(&status);
                    lsampler::announceToActiveScreenReader(*announceSource, loopWillBeOn ? "Loop On" : "Loop Off");
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
                const auto& d=descriptor(selectedEntry());
                setSelectedParameterValue(juce::jlimit(d.minimum,d.maximum,typed));
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
            else closeEditorThen([this] { focusParameterGrid(); }, false);
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
        if(mods.isShiftDown()&&!mods.isCtrlDown()&&!mods.isAltDown()
            &&std::strcmp(selectedEntry().category,"Loops")==0
            &&(code==juce::KeyPress::upKey||code==juce::KeyPress::downKey)) {
            const int next=juce::jlimit(0,9,selectedLoop+(code==juce::KeyPress::upKey?-1:1));
            if(next!=selectedLoop) {selectedLoop=next;refreshParameterGrid();
                lsampler::announceToActiveScreenReader(*source,parameterCellText(selectedParameter));}
            return true;
        }
        if(code==juce::KeyPress::backspaceKey) {processor.resetSlotParameter(selectedParameter,selectedLoop);refreshParameterGrid();announceSelectedValue();return true;}
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

            if(code==juce::KeyPress::tabKey) {if(mods.isShiftDown())openValueEditor();else focusValue();return true;}
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
                selectParameter(globalOpen ? static_cast<int>(lsampler::grid.size()) - 1 : normalGridSize - 1, true);
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
                moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1);
                return true;
            }
            // Type-ahead navigation: plain character searches forward, Shift+character
            // searches backward.  Use the key code so Shift does not turn e.g. 1 into !;
            // letters, digits and punctuation can all be used as initials.
            if(!mods.isCtrlDown()&&!mods.isAltDown()&&!mods.isCommandDown()) {
                const auto initial=juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code));
                if(initial>=33&&initial<=126) {
                    const int normalGridSize = static_cast<int>(lsampler::grid.size()) - lsampler::globalParameterCount;
                    const int first = globalOpen ? normalGridSize : 0;
                    const int count = globalOpen ? lsampler::globalParameterCount : normalGridSize;
                    const int local = selectedParameter - first;
                    const int direction = mods.isShiftDown() ? -1 : 1;
                    for(int distance=1;distance<=count;++distance) {
                        int relative=(local+direction*distance)%count;
                        if(relative<0)relative+=count;
                        const int next=first+relative;
                        const auto name=juce::String(descriptor(lsampler::grid[size_t(next)]).name);
                        if(name.isNotEmpty()&&juce::CharacterFunctions::toLowerCase(name[0])==initial){selectParameter(next,true);break;}
                    }
                    return true;
                }
            }
        }
        return true; // The parameter surface owns unmatched host shortcuts, including Ctrl keys.
    }

    if (isActionButton(source))
    {
        if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isShiftDown() && !mods.isCommandDown() && ch == 'l')
        {
            returnToCurrentSlotAndAnnounce();
            return true;
        }

        if (code == juce::KeyPress::tabKey)
        {
            std::array<juce::Component*, 8> buttons {
                &loadSample, &loadSlot, &saveSlot, &loadBank, &saveBank, &help, &aboutButton, &advancedButton
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
        if (slotPreviewMode)
        {
            processor.requestPreviewToggle();
            return true;
        }
        return false;
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
            if (processor.pasteCurrentSlot())
                announceCurrentSlot();
            return true;
        }
    }

    if (code == juce::KeyPress::deleteKey)
    {
        if (mods.isAltDown())
        {
            processor.clearBank();
            refreshSlotCells();
            lsampler::announceToActiveScreenReader(slotCells[static_cast<size_t>(currentSlot)], "Bank cleared");
        }
        else
        {
            processor.clearCurrentSlot();
            refreshSlotCells();
            lsampler::announceToActiveScreenReader(slotCells[static_cast<size_t>(currentSlot)],
                                                   "Slot " + juce::String(currentSlot + 1) + " cleared");
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
        status.setName(fileTaskTitle + ", 0 percent");
        if (isShowing() && (fileReturnFocus == nullptr || fileReturnFocus == this || isParentOf(fileReturnFocus.getComponent())))
            status.grabKeyboardFocus();
    } else {
        for (auto& child : fileDisabledComponents)
            if (child.first != nullptr) child.first->setEnabled(child.second);
        fileDisabledComponents.clear();
        status.setWantsKeyboardFocus(false);
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
    lastFileTaskProgress = -1;
    setFileUiBusy(true);
    status.setText(title + ", 0 percent", juce::dontSendNotification);
    status.setName(title + ", 0 percent");
    lsampler::announceToActiveScreenReader(status, title);
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
    if (processor.isFileTaskRunning()) {
        if (!fileUiBusy) { fileTaskTitle = "File operation"; setFileUiBusy(true); }
        const int percent = juce::jlimit(0, 100, int(std::round(processor.getFileTaskProgress() * 100.0)));
        if (percent != lastFileTaskProgress)
        {
            lastFileTaskProgress = percent;
            const auto progressText = fileTaskTitle + ", " + juce::String(percent) + " percent";
            status.setText(progressText, juce::dontSendNotification);
            status.setName(progressText);
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
        if (slotLibraryPreviewDelayTicks == 0 && slotLibraryPreviewEnabled && slotLibraryPendingPreview.existsAsFile()) {
            juce::String error;
            if (processor.prepareLibrarySlotPreview(slotLibraryPendingPreview, error)) processor.requestLibraryPreviewToggle();
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

void LSampler24AudioProcessorEditor::leaveSampleSetEditor()
{
    if (!sampleSetActive) return;
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
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    const auto sampleName = info.loaded ? (info.name.isNotEmpty() ? info.name : juce::String("Loaded")) : juce::String("Empty");

    if (!sampleSetGridFocus)
    {
        sampleSetCell.setBrowserText("Sample " + juce::String(sampleSetIndex + 1) + " of "
            + juce::String(LSampler24AudioProcessor::sampleSetSize) + ", " + sampleName
            + ", Velocity " + juce::String(info.velocityLow) + " to " + juce::String(info.velocityHigh));

        if (announce)
            lsampler::announceToActiveScreenReader(sampleSetCell,
                "Sample " + juce::String(sampleSetIndex + 1) + ", " + sampleName);
        return;
    }

    sampleSetCell.setBrowserText(sampleSetParameterText());
    if (announce)
        lsampler::announceToActiveScreenReader(sampleSetCell, sampleSetParameterText());
}

juce::String LSampler24AudioProcessorEditor::sampleSetParameterText() const
{
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    switch (juce::jlimit(0, 2, sampleSetField))
    {
        case 0: return "Velocity Low, " + juce::String(info.velocityLow);
        case 1: return "Velocity High, " + juce::String(info.velocityHigh);
        default: return "Variation Mode, " + LSampler24AudioProcessor::variationModeName(processor.getVariationMode(slot));
    }
}

void LSampler24AudioProcessorEditor::moveSampleSetEntry(int direction)
{
    if (sampleSetGridFocus) return;
    const int next = juce::jlimit(0, LSampler24AudioProcessor::sampleSetSize - 1, sampleSetIndex + (direction < 0 ? -1 : 1));
    if (next == sampleSetIndex) return;
    processor.requestImportPreviewStop();
    sampleSetIndex = next;
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::moveSampleSetField(int direction)
{
    if (!sampleSetGridFocus || sampleSetValueFocus) return;
    constexpr int rows = 8;
    constexpr int total = 3;
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
    if (!sampleSetGridFocus || sampleSetValueFocus) return;
    constexpr int rows = 8;
    constexpr int total = 3;
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
    if (sampleSetField == 0)
    {
        processor.setSampleSetVelocityRange(slot, sampleSetIndex,
            maximum ? info.velocityHigh : 1, info.velocityHigh);
    }
    else if (sampleSetField == 1)
    {
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, info.velocityLow,
            maximum ? 127 : info.velocityLow);
    }
    else
    {
        processor.setVariationMode(slot, maximum
            ? LSampler24AudioProcessor::variationRandomNoRepeat
            : LSampler24AudioProcessor::variationOff);
    }
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::changeSampleSetValue(int direction, bool coarse)
{
    if (!sampleSetGridFocus) return;
    const int slot = processor.getCurrentSlot();
    auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    const int amount = stepWidths[static_cast<size_t>(stepWidthIndex)] * (coarse ? valuePageStep : 1);
    if (sampleSetField == 0)
    {
        const int next = juce::jlimit(1, info.velocityHigh, info.velocityLow + (direction < 0 ? -amount : amount));
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, next, info.velocityHigh);
    }
    else if (sampleSetField == 1)
    {
        const int next = juce::jlimit(info.velocityLow, 127, info.velocityHigh + (direction < 0 ? -amount : amount));
        processor.setSampleSetVelocityRange(slot, sampleSetIndex, info.velocityLow, next);
    }
    else
    {
        const int current = processor.getVariationMode(slot);
        processor.setVariationMode(slot, juce::jlimit<int>(LSampler24AudioProcessor::variationOff,
            LSampler24AudioProcessor::variationRandomNoRepeat, current + (direction < 0 ? -1 : 1)));
    }
    refreshSampleSetCell(true);
}

void LSampler24AudioProcessorEditor::previewSampleSetEntry()
{
    const int slot = processor.getCurrentSlot();
    const auto info = processor.getSampleSetEntry(slot, sampleSetIndex);
    if (!info.loaded)
    {
        lsampler::announceToActiveScreenReader(sampleSetCell, "Empty sample");
        return;
    }
    // Audition the selected Sample Set entry through the configured slot engine,
    // not as a raw file. This keeps Original Pitch, Start/End, ADSR and the other
    // slot playback settings identical to the sound the region will actually play.
    processor.requestImportPreviewStop();
    processor.requestSampleSetPreview(slot, sampleSetIndex);
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
    importSourceCombo.setVisible(!importForSampleSet);
    importSourceCombo.setWantsKeyboardFocus(!importForSampleSet);
    exportLibraryButton.setVisible(!importForSampleSet);
    exportLibraryButton.setWantsKeyboardFocus(!importForSampleSet);
    exportAllLibraryButton.setVisible(!importForSampleSet);
    exportAllLibraryButton.setWantsKeyboardFocus(!importForSampleSet);
    importLibraryButton.setVisible(!importForSampleSet);
    importLibraryButton.setWantsKeyboardFocus(!importForSampleSet);
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

    juce::String savedSlotDirectory, savedSlotEntryPath, savedLibraryRoot;
    int savedSlotIndex = 0;
    std::vector<juce::String> savedLibraryHistory;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        if (auto oldXml = doc.getDocumentElement(); oldXml != nullptr && oldXml->hasTagName("LSampler24Settings"))
        {
            savedSlotDirectory = oldXml->getStringAttribute("f4Directory");
            savedSlotEntryPath = oldXml->getStringAttribute("f4EntryPath");
            savedSlotIndex = oldXml->getIntAttribute("f4Index", 0);
            savedLibraryRoot = oldXml->getStringAttribute("libraryRoot");
            for (auto* child = oldXml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
                if (child->hasTagName("LibraryHistory")) savedLibraryHistory.push_back(child->getStringAttribute("path"));
        }
    }

    juce::XmlElement xml("LSampler24Settings");
    xml.setAttribute("f3Directory", importDirectory.getFullPathName());
    xml.setAttribute("f3Index", importEntryIndex);
    xml.setAttribute("f3Preview", importPreviewEnabled);
    juce::String selectedPath;
    if (!importRecentPathsMode && !importEntries.empty() && importEntryIndex >= 0 && importEntryIndex < static_cast<int>(importEntries.size()))
        selectedPath = importEntries[static_cast<size_t>(importEntryIndex)].file.getFullPathName();
    xml.setAttribute("f3EntryPath", selectedPath);
    xml.setAttribute("f3Position", resetPreviewPosition ? 0.0 : processor.getImportPreviewPositionSeconds());
    xml.setAttribute("f3LastInitial", static_cast<int>(importLastInitial));
    int count = 0;
    for (const auto& p : importRecentPaths)
    {
        if (!p.isDirectory() || count++ >= 20) continue;
        auto* child = xml.createNewChildElement("Recent"); child->setAttribute("path", p.getFullPathName());
    }
    if (savedSlotDirectory.isNotEmpty()) xml.setAttribute("f4Directory", savedSlotDirectory);
    if (savedSlotEntryPath.isNotEmpty()) xml.setAttribute("f4EntryPath", savedSlotEntryPath);
    xml.setAttribute("f4Index", savedSlotIndex);
    if (savedLibraryRoot.isNotEmpty()) xml.setAttribute("libraryRoot", savedLibraryRoot);
    for (const auto& path : savedLibraryHistory)
    {
        auto* child = xml.createNewChildElement("LibraryHistory");
        child->setAttribute("path", path);
    }
    xml.writeTo(file);
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
        processor.requestImportPreviewStop();
        runFileTask("Loading Sample Set entry", [file, slot, sampleIndex](LSampler24AudioProcessor& p) {
            FileTaskResult r; r.slot = slot; r.ok = p.loadSampleSetEntryToSlot(file, slot, sampleIndex, r.message);
            if (r.ok) { r.count = 1; r.message = "Sample " + juce::String(sampleIndex + 1) + " loaded"; }
            return r;
        }, [this, sampleIndex](const FileTaskResult& r, bool focus) {
            if (!r.ok) return;
            sampleSetIndex = sampleIndex;
            if (focus) { leaveImportBrowser(false); refreshSampleSetCell(true); }
        });
        return;
    }
    if (importPlan.empty())
    {
        const int slot = nextImportFreeSlot();
        if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
        importPlan.push_back({ current.file, slot, false, 0.0, 0.0 });
    }

    const auto work = importPlan;
    processor.requestImportPreviewStop();
    runFileTask("Loading selected samples", [work](LSampler24AudioProcessor& p) {
        FileTaskResult r; r.ok = true;
        for (const auto& item : work) {
            if (p.shouldStopFileTask() || !p.importSampleToSlot(item.file, item.slot,
                    item.slice ? item.start : 0.0, item.slice ? item.end : 0.0, r.message)) { r.ok = false; break; }
            ++r.count; r.slot = juce::jmax(r.slot, item.slot);
        }
        if (r.ok) r.message = "Samples loaded";
        else r.message = "Loaded " + juce::String(r.count) + " samples. " + r.message;
        return r;
    }, [this](const FileTaskResult& r, bool focus) {
        importPlan.erase(importPlan.begin(), importPlan.begin() + juce::jmin(r.count, int(importPlan.size())));
        importSlicePending = false; importLastSlicePlanIndex = -1; importCurrentSlicePlanIndex = -1;
        if (r.ok) {
            if (r.slot >= 0) processor.setCurrentSlot(r.slot);
            if (focus) { leaveImportBrowser(false); refreshSlotCells(); returnToCurrentSlotAndAnnounce(); }
        }
    });
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
                safeThis->runFileTask("Loading sample", [file, slot = safeThis->processor.getCurrentSlot()](LSampler24AudioProcessor& p) {
                    FileTaskResult r; r.ok = p.loadSampleToSlot(file, slot, r.message);
                    if (r.ok) r.message = "Sample loaded: " + file.getFileNameWithoutExtension();
                    return r;
                }, [safeThis](const FileTaskResult&, bool focus) { if (focus) safeThis->returnToCurrentSlotAndAnnounce(); });
                return;
            }
            safeThis->returnToCurrentSlotAndAnnounce();
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
                r.ok = p.importFilesToLibrary(files, r.count, r.skipped, r.message);
                if (r.ok) r.message = "Imported " + juce::String(r.count) + " slots, skipped " + juce::String(r.skipped);
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
            if (selected.getFullPathName().isEmpty()) { safeThis->advancedButton.grabKeyboardFocus(); return; }

            juce::File root;
            if (selected.getFileName().equalsIgnoreCase("Library")
                && selected.getParentDirectory().getFileName().equalsIgnoreCase("LSampler-24"))
                root = selected.getParentDirectory();
            else if (selected.getFileName().equalsIgnoreCase("LSampler-24"))
                root = selected;
            else
                root = selected.getChildFile("LSampler-24");

            safeThis->activateLibraryRoot(root, true);
            safeThis->advancedButton.grabKeyboardFocus();
        });
}

void LSampler24AudioProcessorEditor::restoreDefaultLibraryFolder()
{
    activateLibraryRoot(LSampler24AudioProcessor::defaultLibraryRoot(), false);
}

void LSampler24AudioProcessorEditor::returnToAdvancedAndAnnounce()
{
    if (!isShowing()) return;
    advancedButton.grabKeyboardFocus();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
    {
        if (safeThis != nullptr && safeThis->advancedButton.hasKeyboardFocus(true))
            lsampler::announceToActiveScreenReader(safeThis->advancedButton, "Advanced");
    });
}

void LSampler24AudioProcessorEditor::enterExportFolderBrowser()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    slotLibraryForExport = true;
    enterSlotLibraryBrowser(false, false);
    if (!slotLibraryActive) slotLibraryForExport = false;
    else lsampler::announceToActiveScreenReader(slotLibraryCell,
        "Export Folder. Select a folder in the Slot Library and press Enter");
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
    menu.addItem(10, "Import Files...");
    menu.addItem(11, "Import Folder...");
    menu.addItem(12, "Import Library...");
    menu.addItem(13, "Export Folder...");
    menu.addItem(14, "Export All...");

    auto options = juce::PopupMenu::Options().withTargetComponent(&advancedButton);
    menu.showMenuAsync(options,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), history](int result)
        {
            if (safeThis == nullptr) return;
            if (result == 1) safeThis->chooseLibraryFolder();
            else if (result == 2)
            {
                if (!safeThis->requireLibraryAvailable(&safeThis->advancedButton, false)) return;
                auto folder = safeThis->processor.getLibrary().root().getChildFile("Library");
                if (!folder.startAsProcess())
                    lsampler::announceToActiveScreenReader(safeThis->advancedButton, "Cannot open Library Folder");
                safeThis->returnToAdvancedAndAnnounce();
            }
            else if (result == 3) safeThis->restoreDefaultLibraryFolder();
            else if (result == 10) safeThis->chooseImportFiles();
            else if (result == 11) safeThis->chooseImportFolder();
            else if (result == 12) safeThis->chooseImportLibrary();
            else if (result == 13) safeThis->enterExportFolderBrowser();
            else if (result == 14) safeThis->chooseExportLibrary();
            else if (result >= 100 && result < 100 + static_cast<int>(history.size()))
                safeThis->activateLibraryRoot(history[static_cast<size_t>(result - 100)], true);
            else safeThis->returnToAdvancedAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseExportLibrary()
{
    if (!requireLibraryAvailable(&advancedButton, true)) return;
    auto initial = processor.getLibrary().root().getChildFile("LSampler-24 Full Library.lsampler-24.ls24");
    chooser = std::make_unique<juce::FileChooser>("Export All", initial, "*");
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
    chooser = std::make_unique<juce::FileChooser>("Import Library", processor.getLibrary().root(), "*.ls24;*.lsampler-24.ls24");
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

    if (!slotLibraryForBank)
        xml->setAttribute("f3Preview", slotLibraryPreviewEnabled);

    const auto prefix = slotLibraryForBank ? juce::String("f5") : juce::String("f4");
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
    juce::Component* libraryFocus = forBank ? static_cast<juce::Component*>(&loadBank)
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
    slotLibraryPreviewEnabled = slotLibraryForBank ? false : importPreviewEnabled;
    slotLibraryShiftSelectionActive = false;
    slotLibrarySelection.clear();
    slotLibraryStartSlot = processor.getCurrentSlot();
    slotLibraryRoot = slotLibraryForBank ? processor.getLibrary().banks() : processor.getLibrary().slots();
    slotLibraryRoot.createDirectory();

    juce::String rememberedSlotDirectory, rememberedSlotEntryPath;
    int rememberedSlotIndex = 0;
    if (auto settings = importSettingsFile(); settings.existsAsFile())
    {
        juce::XmlDocument doc(settings);
        if (auto xml = doc.getDocumentElement(); xml != nullptr && xml->hasTagName("LSampler24Settings"))
        {
            const auto prefix = slotLibraryForBank ? juce::String("f5") : juce::String("f4");
            rememberedSlotDirectory = xml->getStringAttribute(prefix + "Directory");
            rememberedSlotEntryPath = xml->getStringAttribute(prefix + "EntryPath");
            rememberedSlotIndex = xml->getIntAttribute(prefix + "Index", 0);
        }
    }

    if (rememberedSlotDirectory.isNotEmpty())
    {
        const juce::File candidate(rememberedSlotDirectory);
        if (candidate.isDirectory() && (candidate == slotLibraryRoot || candidate.isAChildOf(slotLibraryRoot)))
            slotLibraryDirectory = candidate;
    }
    if (!slotLibraryDirectory.isDirectory() || (slotLibraryDirectory != slotLibraryRoot && !slotLibraryDirectory.isAChildOf(slotLibraryRoot)))
        slotLibraryDirectory = slotLibraryRoot;
    slotLibraryEntryIndex = rememberedSlotIndex;
    if (rememberedSlotEntryPath.isNotEmpty())
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
            safeThis->announceSlotLibraryEntry();
    });
}

void LSampler24AudioProcessorEditor::leaveSlotLibraryBrowser(bool announceSlot)
{
    if (!slotLibraryActive) return;
    const bool returningToSampleSet = slotLibraryForSampleSet && sampleSetActive;
    saveSlotLibraryNavigationState();
    processor.requestLibraryPreviewStop();
    slotLibraryPendingPreview = {};
    slotLibraryPreviewDelayTicks = 0;
    slotLibraryPreviewEnabled = false;
    slotLibraryActive = false;
    slotLibraryForSampleSet = false;
    slotLibraryForBank = false;
    slotLibraryForExport = false;
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
        "*" + juce::String(slotLibraryForBank ? LibraryManager::bankExtension : LibraryManager::slotExtension));
    for (const auto& f : dirs) slotLibraryEntries.push_back({ f, true });
    for (const auto& f : files) slotLibraryEntries.push_back({ f, false });
    slotLibraryEntryIndex = slotLibraryEntries.empty() ? 0 : juce::jlimit(0, int(slotLibraryEntries.size()) - 1, slotLibraryEntryIndex);
    const auto it = slotLibraryDirectorySelectionMemory.find(slotLibraryDirectory.getFullPathName());
    if (it != slotLibraryDirectorySelectionMemory.end())
        for (int i = 0; i < static_cast<int>(slotLibraryEntries.size()); ++i)
            if (slotLibraryEntries[size_t(i)].file.getFullPathName() == it->second) { slotLibraryEntryIndex = i; break; }
    if (slotLibraryEntries.empty()) slotLibraryCell.setBrowserText("Empty folder");
    else selectSlotLibraryEntry(slotLibraryEntryIndex, false);
}

void LSampler24AudioProcessorEditor::selectSlotLibraryEntry(int index, bool announce)
{
    if (slotLibraryEntries.empty()) { slotLibraryCell.setBrowserText("Empty folder"); exportLibraryButton.setButtonText("Export Folder"); return; }
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
        text = slotLibraryForBank ? cleanLibraryBankName(e.file) : cleanLibrarySlotName(e.file);
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
    if (slotLibraryEntries.empty())
        lsampler::announceToActiveScreenReader(slotLibraryCell, "Empty folder");
    else
        lsampler::announceToActiveScreenReader(slotLibraryCell, slotLibraryCell.getBrowserText());
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
                                                                  : (slotLibraryForBank ? cleanLibraryBankName(slotLibraryEntries[size_t(index)].file)
                                                                                        : cleanLibrarySlotName(slotLibraryEntries[size_t(index)].file));
        if (name.isNotEmpty() && juce::CharacterFunctions::toLowerCase(name[0]) == target)
        { selectSlotLibraryEntry(index, true); return; }
    }
}

void LSampler24AudioProcessorEditor::updateSlotLibraryPreviewForSelection()
{
    processor.requestLibraryPreviewStop();
    slotLibraryPendingPreview = {};
    slotLibraryPreviewDelayTicks = 0;
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
        runFileTask("Loading bank", [bankFile](LSampler24AudioProcessor& p) {
            FileTaskResult r;
            r.ok = p.loadBankPreset(bankFile, r.message);
            if (r.ok) r.message = "Bank loaded: " + bankFile.getFileNameWithoutExtension();
            return r;
        }, [this](const FileTaskResult& r, bool focus) {
            if (r.ok)
            {
                refreshSlotCells();
                if (focus) leaveSlotLibraryBrowser(true);
            }
        });
        return;
    }

    if (slotLibraryForSampleSet)
    {
        const auto& entry = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (entry.directory) return;
        const auto presetFile = entry.file;
        const int targetSlot = importSampleSetSlot;
        const int targetSample = importSampleSetIndex;
        runFileTask("Loading Sample Set entry", [presetFile, targetSlot, targetSample](LSampler24AudioProcessor& p) {
            FileTaskResult r;
            r.ok = p.loadSampleSetEntryFromSlotPreset(presetFile, targetSlot, targetSample, r.message);
            if (r.ok)
            {
                r.slot = targetSlot;
                r.message = "Sample " + juce::String(targetSample + 1) + " loaded";
            }
            return r;
        }, [this](const FileTaskResult& r, bool focus) {
            if (!r.ok) return;
            refreshSlotCells();
            if (focus) leaveSlotLibraryBrowser(false);
        });
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
    runFileTask("Loading selected slots", [work](LSampler24AudioProcessor& p) {
        FileTaskResult r; r.ok = true;
        for (const auto& item : work) {
            if (p.shouldStopFileTask() || !p.loadSlotPresetToSlot(item.file, item.slot, r.message)) { r.ok = false; break; }
            ++r.count; r.slot = juce::jmax(r.slot, item.slot);
        }
        if (r.ok) r.message = "Slots loaded";
        else r.message = "Loaded " + juce::String(r.count) + " slots. " + r.message;
        return r;
    }, [this](const FileTaskResult& r, bool focus) {
        slotLibrarySelection.erase(slotLibrarySelection.begin(), slotLibrarySelection.begin() + juce::jmin(r.count, int(slotLibrarySelection.size())));
        if (r.ok && r.slot >= 0) { processor.setCurrentSlot(r.slot); if (focus) leaveSlotLibraryBrowser(true); }
    });
}

void LSampler24AudioProcessorEditor::renameCurrentSlot()
{
    const int slot = processor.getCurrentSlot();
    auto currentName = processor.getSlotName(slot);
    if (currentName.isEmpty()) currentName = "Slot " + juce::String(slot + 1);

    auto* window = new juce::AlertWindow("Rename Slot", "Slot name", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("slotName", currentName, "Name");
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (auto* editor = window->getTextEditor("slotName"))
    {
        editor->selectAll();
        editor->grabKeyboardFocus();
    }

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
                safeThis->runFileTask("Loading slot", [file, slot = safeThis->processor.getCurrentSlot()](LSampler24AudioProcessor& p) {
                    FileTaskResult r; r.ok = p.loadSlotPresetToSlot(file, slot, r.message);
                    if (r.ok) r.message = "Slot loaded: " + file.getFileNameWithoutExtension();
                    return r;
                }, [safeThis](const FileTaskResult&, bool focus) { if (focus) safeThis->returnToCurrentSlotAndAnnounce(); });
                return;
            }
            safeThis->returnToCurrentSlotAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseSaveSlot()
{
    if (!requireLibraryAvailable(&saveSlot, true)) return;
    // The native Save dialog shows only the editable logical slot name.
    // Prefix and LSampler extension are added after the user confirms.
    auto logicalName = processor.getSlotName(processor.getCurrentSlot());
    if (logicalName.isEmpty())
        logicalName = LibraryManager::defaultName("Slot");

    auto initial = processor.getLibrary().slots().getChildFile(logicalName);
    chooser = std::make_unique<juce::FileChooser>("Save Slot", initial, "*");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto chosen = fc.getResult();
            if (chosen.getFullPathName().isNotEmpty())
            {
                auto enteredName = chosen.getFileName();
                if (enteredName.endsWithIgnoreCase(LibraryManager::slotExtension))
                    enteredName = enteredName.dropLastCharacters((int) juce::String(LibraryManager::slotExtension).length());
                if (enteredName.startsWithIgnoreCase("Slot_"))
                    enteredName = enteredName.substring(5);

                enteredName = juce::File::createLegalFileName(enteredName.trim());
                if (enteredName.isEmpty())
                    enteredName = "Slot";

                auto file = chosen.getParentDirectory().getChildFile("Slot_" + enteredName + LibraryManager::slotExtension);
                safeThis->processor.setSlotName(safeThis->processor.getCurrentSlot(), enteredName);
                safeThis->runFileTask("Saving slot", [file, slot = safeThis->processor.getCurrentSlot()](LSampler24AudioProcessor& p) {
                    FileTaskResult r; r.ok = p.saveSlotPresetAt(file, slot, r.message);
                    if (r.ok) r.message = "Slot saved: " + file.getFileNameWithoutExtension();
                    return r;
                }, [safeThis](const FileTaskResult&, bool focus) { if (focus) safeThis->returnToCurrentSlotAndAnnounce(); });
                return;
            }
            safeThis->returnToCurrentSlotAndAnnounce();
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
                safeThis->runFileTask("Loading bank", [file](LSampler24AudioProcessor& p) {
                    FileTaskResult r; r.ok = p.loadBankPreset(file, r.message);
                    if (r.ok) r.message = "Bank loaded: " + file.getFileNameWithoutExtension();
                    return r;
                }, [safeThis](const FileTaskResult&, bool focus) { if (focus) safeThis->returnToCurrentSlotAndAnnounce(); });
                return;
            }
            safeThis->returnToCurrentSlotAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseSaveBank()
{
    if (!requireLibraryAvailable(&saveBank, true)) return;
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
                safeThis->runFileTask("Saving bank", [file](LSampler24AudioProcessor& p) {
                    FileTaskResult r; r.ok = p.saveBankPreset(file, r.message);
                    if (r.ok) r.message = "Bank saved: " + file.getFileNameWithoutExtension();
                    return r;
                }, [safeThis](const FileTaskResult&, bool focus) { if (focus) safeThis->returnToCurrentSlotAndAnnounce(); });
                return;
            }
            safeThis->returnToCurrentSlotAndAnnounce();
        });
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
