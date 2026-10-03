#include "PluginEditor.h"
#include <limits>
#include <algorithm>
#include "ScreenReaderAnnouncer.h"
#include <cmath>
#include <utility>
#include <cstring>
using namespace lsampler;

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
    status.setAccessible(false);
    status.setTitle({});
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
    for (int i = 0; i < static_cast<int>(lsampler::grid.size()); ++i)
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

    importLibraryButton.setWantsKeyboardFocus(true);
    importLibraryButton.setExplicitFocusOrder(4);
    importLibraryButton.addKeyListener(this);
    addChildComponent(importLibraryButton);
    importLibraryButton.setVisible(false);

    loadSample.onClick = [this] { enterImportBrowser(); };
    loadSlot.onClick   = [this] { enterSlotLibraryBrowser(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };
    exportLibraryButton.onClick = [this] { chooseExportLibrary(); };
    importLibraryButton.onClick = [this] { chooseImportLibrary(); };

    refreshSlotCells();
    leaveSlotParameters();
    startTimerHz(10);
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

    if (slotLibraryActive)
    {
        slotLibraryCell.setBounds(area.removeFromTop(42));
        area.removeFromTop(10);
        auto libraryButtons = area.removeFromTop(36);
        const int gap = 8;
        const int w = (libraryButtons.getWidth() - gap) / 2;
        exportLibraryButton.setBounds(libraryButtons.removeFromLeft(w));
        libraryButtons.removeFromLeft(gap);
        importLibraryButton.setBounds(libraryButtons);
        return;
    }

    if (importBrowserActive)
    {
        importBrowserCell.setBounds(area.removeFromTop(42));
        area.removeFromTop(10);
        auto importButtons = area.removeFromTop(36);
        const int gap = 8;
        const int w = (importButtons.getWidth() - gap * 2) / 3;
        importSourceCombo.setBounds(importButtons.removeFromLeft(w));
        importButtons.removeFromLeft(gap);
        exportLibraryButton.setBounds(importButtons.removeFromLeft(w));
        importButtons.removeFromLeft(gap);
        importLibraryButton.setBounds(importButtons);
        return;
    }

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


void LSampler24AudioProcessorEditor::returnToCurrentSlotAndAnnounce()
{
    const int slot = processor.getCurrentSlot();
    refreshSlotCells();

    juce::Timer::callAfterDelay(60,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), slot]
        {
            if (safeThis == nullptr) return;
            auto& cell = safeThis->slotCells[static_cast<size_t>(slot)];
            cell.grabKeyboardFocus();

            juce::Timer::callAfterDelay(90,
                [safeThis, slot]
                {
                    if (safeThis == nullptr) return;
                    auto& currentCell = safeThis->slotCells[static_cast<size_t>(slot)];
                    lsampler::announceToActiveScreenReader(currentCell, safeThis->processor.getSlotLabel(slot));
                });
        });
}

void LSampler24AudioProcessorEditor::refreshSlotCells()
{
    const int selected = processor.getCurrentSlot();
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[static_cast<size_t>(i)];
        const auto label = processor.getSlotLabel(i);
        cell.setSlotText(label);
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
    return (std::strcmp(e.category,"Loops")==0?"Loop "+juce::String(selectedLoop+1)+" ":juce::String())+descriptor(e).name;
}
double LSampler24AudioProcessorEditor::getSelectedParameterValue() const {return processor.getSlotParameter(selectedParameter,selectedLoop);}
void LSampler24AudioProcessorEditor::setSelectedParameterValue(double value)
{
    const auto parameter = selectedEntry().parameter;
    processor.setSlotParameter(selectedParameter, value, selectedLoop);
    if (parameter == int(P::sample_start))
        processor.requestSampleBoundaryAudition(false);
    else if (parameter == int(P::sample_end))
        processor.requestSampleBoundaryAudition(true);
}
juce::String LSampler24AudioProcessorEditor::formatParameter(int index,double value) const {
    const auto& e=lsampler::grid[size_t(index)];const auto& d=descriptor(e);
    if(d.kind==Kind::note)return value<0?juce::String("Off"):midiNoteText(juce::roundToInt(value));
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
    return prefix+descriptor(e).name+", "+formatParameter(index,processor.getSlotParameter(index,selectedLoop));
}
void LSampler24AudioProcessorEditor::refreshParameterGrid() {
    for(int i=0;i<int(lsampler::grid.size());++i)parameterSelector.changeItemText(i+1,parameterCellText(i));
    parameterSelector.setSelectedItemIndex(selectedParameter,juce::dontSendNotification);
    configureValueForSelectedParameter();
}
void LSampler24AudioProcessorEditor::configureValueForSelectedParameter() {
    const auto& d=descriptor(selectedEntry());
    parameterValue.setRange(selectedRateIsSynced()?.125:d.minimum,d.maximum,
        d.kind==Kind::integer||d.kind==Kind::enumeration||d.kind==Kind::note||d.kind==Kind::action?1.0:0.0);
    parameterValue.textFromValueFunction=[this](double v){return formatParameter(selectedParameter,v);};
    parameterValue.valueFromTextFunction=[this](const juce::String& text) {
        const auto& desc=descriptor(selectedEntry());
        if(desc.kind==Kind::enumeration||desc.kind==Kind::action) {
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
    parameterPage = true;
    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    parameterSelector.setVisible(true);
    parameterValue.setVisible(true);
    selectedParameter = juce::jlimit(0, int(lsampler::grid.size())-1, selectedParameter);
    refreshParameterGrid();
    resized();
    parameterSelector.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveSlotParameters()
{
    processor.requestPreviewStop();
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
    index = juce::jlimit(0, static_cast<int>(lsampler::grid.size()) - 1, index);
    if(index==selectedParameter)return;
    selectedParameter = index;
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
    // Modal editors own every key before main grid or host shortcut dispatch.
    if(activeModalSurface!=nullptr){activeModalSurface->handleKey(key);return true;}
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    const int sourceSlot = slotCellIndex(source);
    const bool sourceIsSlot = sourceSlot >= 0;
    const bool sourceIsValueEditor = dynamic_cast<juce::TextEditor*>(source) != nullptr
                                  && parameterValue.isParentOf(source);

    if (slotLibraryActive)
    {
        const bool onExportLibrary = source == &exportLibraryButton;
        const bool onImportLibrary = source == &importLibraryButton;
        const bool onLibraryAction = onExportLibrary || onImportLibrary;
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
            if (mods.isShiftDown())
            {
                if (onExportLibrary) focusSlotLibrary();
                else if (onImportLibrary) exportLibraryButton.grabKeyboardFocus();
                else importLibraryButton.grabKeyboardFocus();
            }
            else
            {
                if (onExportLibrary) importLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) focusSlotLibrary();
                else exportLibraryButton.grabKeyboardFocus();
            }
            return true;
        }

        if (code == juce::KeyPress::escapeKey) { leaveSlotLibraryBrowser(true); return true; }

        if (onLibraryAction)
        {
            if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
            {
                if (onExportLibrary) chooseExportLibrary(); else chooseImportLibrary();
                return true;
            }
            return true;
        }
        if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown())
        {
            const auto ctrlChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (ctrlChar == 'c') { copySlotLibraryEntry(false); return true; }
            if (ctrlChar == 'x') { copySlotLibraryEntry(true); return true; }
            if (ctrlChar == 'v') { pasteSlotLibraryEntry(); return true; }
        }
        if (mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
            && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        {
            moveSlotLibraryDestination(code == juce::KeyPress::upKey ? -1 : 1); return true;
        }
        if (mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()
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
        if (code == juce::KeyPress::backspaceKey)
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
        if (code == juce::KeyPress::spaceKey && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        { toggleSlotLibrarySelection(); return true; }
        if (code == juce::KeyPress::spaceKey && !mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown())
        {
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
                slotLibraryDirectorySelectionMemory[slotLibraryDirectory.getFullPathName()] = e.file.getFullPathName();
                slotLibraryDirectory = e.file; refreshSlotLibraryEntries(); selectSlotLibraryEntry(slotLibraryEntryIndex, true); return true;
            }
            commitSlotLibrarySelection(); return true;
        }
        if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isCommandDown() && !mods.isShiftDown())
        {
            auto typed = key.getTextCharacter();
            if (typed >= 33 && typed != 127) { cycleSlotLibraryEntryByInitial(typed); return true; }
        }
        return true;
    }

    if (importBrowserActive)
    {
        const bool onImportSource = source == &importSourceCombo;
        const bool onExportLibrary = source == &exportLibraryButton;
        const bool onImportLibrary = source == &importLibraryButton;
        const auto focusImportBrowser = [this]
        {
            importBrowserCell.grabKeyboardFocus();
            juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)]
            {
                if (safeThis == nullptr || !safeThis->importBrowserActive) return;
                lsampler::announceToActiveScreenReader(safeThis->importBrowserCell,
                    "Sample Browser. Enter loads selected items into slots.");
            });
        };
        if (code == juce::KeyPress::tabKey)
        {
            if (mods.isShiftDown())
            {
                if (onImportSource) focusImportBrowser();
                else if (onExportLibrary) importSourceCombo.grabKeyboardFocus();
                else if (onImportLibrary) exportLibraryButton.grabKeyboardFocus();
                else importLibraryButton.grabKeyboardFocus();
            }
            else
            {
                if (onImportSource) exportLibraryButton.grabKeyboardFocus();
                else if (onExportLibrary) importLibraryButton.grabKeyboardFocus();
                else if (onImportLibrary) focusImportBrowser();
                else importSourceCombo.grabKeyboardFocus();
            }
            return true;
        }
        if (onImportSource || onExportLibrary || onImportLibrary)
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
                else if (onExportLibrary) chooseExportLibrary();
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
        if (code == juce::KeyPress::backspaceKey)
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
        if (!mods.isCtrlDown() && !mods.isAltDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            seekImportPreview(code == juce::KeyPress::leftKey ? -1.0 : 1.0);
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
        if (!mods.isCtrlDown() && !mods.isAltDown() && ch >= 32 && ch != ' ')
        {
            cycleImportEntryByInitial(ch);
            return true;
        }
        return true;
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

    if (code == juce::KeyPress::spaceKey && parameterPage)
    {
        processor.requestPreviewToggle();
        return true;
    }

    // Page/category navigation remains available, now on Alt+Shift+Left/Right.
    // Ctrl+Left/Right is reserved for Sample Play Start scrubbing.
    if (parameterPage && !sourceIsValueEditor && mods.isAltDown() && mods.isShiftDown()
        && !mods.isCtrlDown() && !mods.isCommandDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }

    // Ctrl+Up/Down changes the current slot while keeping the parameter grid open.
    if (parameterPage && !sourceIsValueEditor && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
        && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        const int current = processor.getCurrentSlot();
        const int next = current + (code == juce::KeyPress::upKey ? -1 : 1);
        if (next >= 0 && next < LSampler24AudioProcessor::slotCount)
        {
            processor.setCurrentSlot(next);
            refreshSlotCells();
            refreshParameterGrid();
            configureValueForSelectedParameter();
            lsampler::announceToActiveScreenReader(parameterSelector, processor.getSlotLabel(next) + ". " + parameterCellText(selectedParameter));
        }
        return true;
    }

    // Ctrl+Left/Right scrubs Sample Play Start without exposing it in the Grid.
    // Use the same coarse multiplier as Page Up/Down so sample navigation is fast,
    // while Alt+Left/Right still selects the step-width multiplier.
    if (parameterPage && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        const auto& d = lsampler::parameters[static_cast<size_t>(lsampler::P::sample_play_start)];
        const double step = d.step
                          * static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)])
                          * static_cast<double>(valuePageStep);
        const double current = processor.getSamplePlayStart();
        const double next = juce::jlimit(d.minimum, d.maximum,
                                         current + (code == juce::KeyPress::rightKey ? step : -step));
        if (std::abs(next - current) >= 1.0e-9)
        {
            processor.setSamplePlayStart(next);
            processor.requestPreviewRestartIfPlaying();
            // Deliberately silent: Ctrl+Left/Right is an auditory scrub.
        }
        return true;
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'o' && !mods.isShiftDown()) { enterImportBrowser(); return true; }
        if (ch == 's' && !mods.isShiftDown()) { enterSlotLibraryBrowser(); return true; }
        if (ch == 'b' && !mods.isShiftDown()) { chooseLoadBank(); return true; }
        if (ch == 's' && mods.isShiftDown()) { chooseSaveSlot(); return true; }
        if (ch == 'b' && mods.isShiftDown()) { chooseSaveBank(); return true; }
        if (ch == 'v' && parameterPage && !sourceIsValueEditor) { focusValue(); return true; }
        if (ch == 'l' && parameterPage)
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
            if (mods.isCtrlDown() && code == juce::KeyPress::homeKey) { selectParameter(0, true); return true; }
            if (mods.isCtrlDown() && code == juce::KeyPress::endKey)
            {
                selectParameter(static_cast<int>(lsampler::grid.size()) - 1, true);
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
            if (!mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown() && !mods.isCommandDown()
                && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
            {
                moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1);
                return true;
            }
            if(!mods.isCtrlDown()&&!mods.isAltDown()&&!mods.isCommandDown()&&juce::CharacterFunctions::isLetterOrDigit(ch)) {
                for(int distance=1;distance<=int(lsampler::grid.size());++distance) {
                    const int next=(selectedParameter+distance)%int(lsampler::grid.size());
                    const auto name=juce::String(descriptor(lsampler::grid[size_t(next)]).name);
                    if(juce::CharacterFunctions::toLowerCase(name[0])==ch){selectParameter(next,true);break;}
                }
                return true;
            }
        }
        return true; // The parameter surface owns unmatched host shortcuts, including Ctrl keys.
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

    const int currentSlot = processor.getCurrentSlot();
    auto announceCurrentSlot = [this, currentSlot]()
    {
        refreshSlotCells();
        auto& cell = slotCells[static_cast<size_t>(currentSlot)];
        lsampler::announceToActiveScreenReader(cell, processor.getSlotLabel(currentSlot));
    };

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
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
            processor.clearBank();
        else
            processor.clearCurrentSlot();
        announceCurrentSlot();
        return true;
    }

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
    if (slotLibraryActive && slotLibraryPreviewDelayTicks > 0)
    {
        --slotLibraryPreviewDelayTicks;
        if (slotLibraryPreviewDelayTicks == 0 && slotLibraryPreviewEnabled && slotLibraryPendingPreview.existsAsFile())
        {
            juce::String error;
            if (processor.prepareLibrarySlotPreview(slotLibraryPendingPreview, error))
                processor.requestLibraryPreviewToggle();
        }
    }
}

void LSampler24AudioProcessorEditor::showResult(bool ok, const juce::String& error, const juce::String& okMessage)
{
    status.setText(ok ? okMessage : error, juce::sendNotificationAsync);
    refreshSlotCells();
    if (parameterPage) refreshParameterGrid();
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
    parameterPage = false;
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

    const auto current = processor.getCurrentSampleFile();
    if (!importDirectory.isDirectory())
        importDirectory = current.existsAsFile() ? current.getParentDirectory()
                                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (!importDirectory.isDirectory())
        importDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    addImportRecentPath(importDirectory);

    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    importBrowserCell.setVisible(true);
    importBrowserCell.setWantsKeyboardFocus(true);
    importSourceCombo.setVisible(true);
    importSourceCombo.setWantsKeyboardFocus(true);
    exportLibraryButton.setVisible(true);
    exportLibraryButton.setWantsKeyboardFocus(true);
    importLibraryButton.setVisible(true);
    importLibraryButton.setWantsKeyboardFocus(true);
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
        lsampler::announceToActiveScreenReader(safeThis->importBrowserCell,
            "Sample Browser. Enter loads selected items into slots.");
    });
}

void LSampler24AudioProcessorEditor::leaveImportBrowser(bool announceSlot, bool resetPreviewPosition)
{
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
    importLibraryButton.setVisible(false);
    importLibraryButton.setWantsKeyboardFocus(false);
    for (auto& cell : slotCells) { cell.setVisible(true); cell.setWantsKeyboardFocus(true); }
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
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
            if (SamplePool::instance().canReadFile(f))
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

void LSampler24AudioProcessorEditor::cycleImportEntryByInitial(juce::juce_wchar initial)
{
    if (importEntries.empty()) return;
    const auto target = juce::CharacterFunctions::toLowerCase(initial);
    importLastInitial = target;
    const int count = static_cast<int>(importEntries.size());
    for (int offset = 1; offset <= count; ++offset)
    {
        const int index = (importEntryIndex + offset) % count;
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

    juce::String savedSlotDirectory, savedSlotEntryPath;
    int savedSlotIndex = 0;
    if (file.existsAsFile())
    {
        juce::XmlDocument doc(file);
        if (auto oldXml = doc.getDocumentElement(); oldXml != nullptr && oldXml->hasTagName("LSampler24Settings"))
        {
            savedSlotDirectory = oldXml->getStringAttribute("f4Directory");
            savedSlotEntryPath = oldXml->getStringAttribute("f4EntryPath");
            savedSlotIndex = oldXml->getIntAttribute("f4Index", 0);
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
    if (importPlan.empty())
    {
        const int slot = nextImportFreeSlot();
        if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
        importPlan.push_back({ current.file, slot, false, 0.0, 0.0 });
    }

    int highest = -1;
    for (const auto& item : importPlan)
    {
        juce::String error;
        if (!processor.importSampleToSlot(item.file, item.slot, item.slice ? item.start : 0.0, item.slice ? item.end : 0.0, error))
        {
            lsampler::announceToActiveScreenReader(importBrowserCell, error.isNotEmpty() ? error : "Load failed");
            return;
        }
        highest = juce::jmax(highest, item.slot);
    }
    importPlan.clear(); importSlicePending = false; importLastSlicePlanIndex = -1; importCurrentSlicePlanIndex = -1;
    if (highest >= 0) processor.setCurrentSlot(highest);
    leaveImportBrowser(false);
    refreshSlotCells();
    returnToCurrentSlotAndAnnounce();
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
                juce::String error;
                const bool ok = safeThis->processor.loadSample(file, error);
                safeThis->showResult(ok, error, safeThis->processor.getSampleStatus());
            }
            safeThis->returnToCurrentSlotAndAnnounce();
        });
}


void LSampler24AudioProcessorEditor::chooseImportFiles()
{
    // Files mode deliberately uses the internal Alt+O browser instead of a native
    // multi-file dialog. This preserves preview/audition while the user chooses
    // samples. Whole-file selections in importPlan are treated as the selection;
    // if none exist, the current browser file is imported. Slice entries are not
    // interpreted as separate library files here: they still refer to the same
    // source audio.
    juce::Array<juce::File> files;

    auto addUnique = [&files](const juce::File& file)
    {
        if (!file.existsAsFile() || !SamplePool::instance().canReadFile(file)) return;
        for (const auto& existing : files)
            if (existing == file) return;
        files.add(file);
    };

    for (const auto& item : importPlan)
        if (!item.slice)
            addUnique(item.file);

    if (files.isEmpty() && !importEntries.empty())
    {
        const auto& current = importEntries[static_cast<size_t>(importEntryIndex)];
        if (!current.directory)
            addUnique(current.file);
    }

    juce::String message;
    if (files.isEmpty())
    {
        message = "No supported audio files selected";
    }
    else
    {
        int imported = 0, skipped = 0;
        juce::String error;
        const bool ok = processor.importFilesToLibrary(files, imported, skipped, error);
        if (ok)
        {
            message = "Imported " + juce::String(imported) + (imported == 1 ? " slot" : " slots");
            if (skipped > 0) message += ", skipped " + juce::String(skipped);
            importDirectory = files.getFirst().getParentDirectory();
            addImportRecentPath(importDirectory);
            saveImportSettings();
        }
        else
        {
            message = error.isNotEmpty() ? error : "No supported audio files selected";
        }
    }

    importSourceCombo.grabKeyboardFocus();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this), message]()
    {
        if (safeThis == nullptr) return;
        lsampler::announceToActiveScreenReader(safeThis->importSourceCombo, message);
        const auto mode = safeThis->importSourceCombo.getText();
        if (mode.isNotEmpty())
            lsampler::announceToActiveScreenReader(safeThis->importSourceCombo, "Import to Library, combo box, " + mode);
    });
}

void LSampler24AudioProcessorEditor::chooseImportFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Import Folder", importDirectory, "*");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto folder = fc.getResult();
            juce::String message;

            if (!folder.isDirectory())
            {
                message = "No supported audio files found";
            }
            else
            {
                int imported = 0, skipped = 0;
                juce::String error;
                const bool ok = safeThis->processor.importFolderToLibrary(folder, imported, skipped, error);
                if (ok)
                {
                    message = "Imported " + juce::String(imported) + (imported == 1 ? " slot" : " slots");
                    if (skipped > 0) message += ", skipped " + juce::String(skipped);
                    safeThis->importDirectory = folder;
                    safeThis->addImportRecentPath(folder);
                    safeThis->saveImportSettings();
                }
                else
                {
                    message = "No supported audio files found";
                }
            }

            safeThis->importSourceCombo.grabKeyboardFocus();
            juce::MessageManager::callAsync([safeThis, message]()
            {
                if (safeThis == nullptr) return;
                lsampler::announceToActiveScreenReader(safeThis->importSourceCombo, message);
                const auto mode = safeThis->importSourceCombo.getText();
                if (mode.isNotEmpty())
                    lsampler::announceToActiveScreenReader(safeThis->importSourceCombo, "Import to Library, combo box, " + mode);
            });
        });
}

void LSampler24AudioProcessorEditor::chooseExportLibrary()
{
    auto initial = processor.getLibrary().root().getChildFile("LSampler-24 Library");
    chooser = std::make_unique<juce::FileChooser>("Export Library", initial, "*");
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
                if (name.isEmpty()) name = "LSampler-24 Library";
                auto target = chosen.getParentDirectory().getChildFile(name + ".lsampler-24.ls24");

                int slots = 0, samples = 0;
                juce::String error;
                const bool ok = safeThis->processor.exportLibraryArchive(target, slots, samples, error);
                const auto message = ok
                    ? ("Exported " + juce::String(slots) + (slots == 1 ? " slot, " : " slots, ")
                       + juce::String(samples) + (samples == 1 ? " sample" : " samples"))
                    : (error.isNotEmpty() ? error : juce::String("Export failed"));
                lsampler::announceToActiveScreenReader(safeThis->exportLibraryButton, message);
            }
            safeThis->exportLibraryButton.grabKeyboardFocus();
        });
}

void LSampler24AudioProcessorEditor::chooseImportLibrary()
{
    chooser = std::make_unique<juce::FileChooser>("Import Library", processor.getLibrary().root(), "*.ls24;*.lsampler-24.ls24");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<LSampler24AudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                int slots = 0, samples = 0, skipped = 0;
                juce::String error;
                const bool ok = safeThis->processor.importLibraryArchive(file, slots, samples, skipped, error);
                juce::String message;
                if (ok)
                {
                    message = "Imported library: " + juce::String(slots) + (slots == 1 ? " slot, " : " slots, ")
                            + juce::String(samples) + (samples == 1 ? " new sample" : " new samples");
                    if (skipped > 0) message += ", skipped " + juce::String(skipped);
                }
                else
                    message = error.isNotEmpty() ? error : "Library import failed";
                lsampler::announceToActiveScreenReader(safeThis->importLibraryButton, message);
            }
            if (safeThis->slotLibraryActive)
                safeThis->refreshSlotLibraryEntries();
            safeThis->importLibraryButton.grabKeyboardFocus();
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

    xml->setAttribute("f3Preview", slotLibraryPreviewEnabled);
    if (slotLibraryDirectory.isDirectory())
        xml->setAttribute("f4Directory", slotLibraryDirectory.getFullPathName());
    xml->setAttribute("f4Index", slotLibraryEntryIndex);

    juce::String selectedPath;
    if (!slotLibraryEntries.empty() && slotLibraryEntryIndex >= 0
        && slotLibraryEntryIndex < static_cast<int>(slotLibraryEntries.size()))
        selectedPath = slotLibraryEntries[static_cast<size_t>(slotLibraryEntryIndex)].file.getFullPathName();
    if (selectedPath.isNotEmpty())
        xml->setAttribute("f4EntryPath", selectedPath);
    else
        xml->removeAttribute("f4EntryPath");

    xml->writeTo(file);
}


void LSampler24AudioProcessorEditor::enterSlotLibraryBrowser()
{
    if (slotLibraryActive) return;
    processor.requestPreviewStop();
    processor.requestImportPreviewStop();
    processor.requestLibraryPreviewStop();
    parameterPage = false;
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
    slotLibraryPreviewEnabled = importPreviewEnabled;
    slotLibraryShiftSelectionActive = false;
    slotLibrarySelection.clear();
    slotLibraryStartSlot = processor.getCurrentSlot();
    slotLibraryRoot = processor.getLibrary().slots();
    slotLibraryRoot.createDirectory();

    juce::String rememberedSlotDirectory, rememberedSlotEntryPath;
    int rememberedSlotIndex = 0;
    if (auto settings = importSettingsFile(); settings.existsAsFile())
    {
        juce::XmlDocument doc(settings);
        if (auto xml = doc.getDocumentElement(); xml != nullptr && xml->hasTagName("LSampler24Settings"))
        {
            rememberedSlotDirectory = xml->getStringAttribute("f4Directory");
            rememberedSlotEntryPath = xml->getStringAttribute("f4EntryPath");
            rememberedSlotIndex = xml->getIntAttribute("f4Index", 0);
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
    loadBank.setVisible(false); saveBank.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    importBrowserCell.setVisible(false); importSourceCombo.setVisible(false);
    exportLibraryButton.setVisible(true); importLibraryButton.setVisible(true);
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
    saveSlotLibraryNavigationState();
    processor.requestLibraryPreviewStop();
    slotLibraryPendingPreview = {};
    slotLibraryPreviewDelayTicks = 0;
    slotLibraryPreviewEnabled = false;
    slotLibraryActive = false;
    slotLibraryCell.setVisible(false);
    slotLibraryCell.setWantsKeyboardFocus(false);
    exportLibraryButton.setVisible(false);
    importLibraryButton.setVisible(false);
    for (int i = 0; i < static_cast<int>(slotCells.size()); ++i)
    {
        auto& cell = slotCells[size_t(i)];
        cell.setVisible(true);
        cell.setWantsKeyboardFocus(i == processor.getCurrentSlot());
    }
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
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

void LSampler24AudioProcessorEditor::refreshSlotLibraryEntries()
{
    slotLibraryEntries.clear();
    if (!slotLibraryDirectory.isDirectory()) slotLibraryDirectory = slotLibraryRoot;
    juce::Array<juce::File> dirs, files;
    slotLibraryDirectory.findChildFiles(dirs, juce::File::findDirectories, false);
    slotLibraryDirectory.findChildFiles(files, juce::File::findFiles, false,
        "*" + juce::String(LibraryManager::slotExtension));
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
    if (slotLibraryEntries.empty()) { slotLibraryCell.setBrowserText("Empty folder"); return; }
    slotLibraryEntryIndex = juce::jlimit(0, int(slotLibraryEntries.size()) - 1, index);
    const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
    juce::String text;
    if (e.directory) text = "Folder " + e.file.getFileName();
    else
    {
        text = cleanLibrarySlotName(e.file);
        int target = -1;
        for (const auto& selected : slotLibrarySelection) if (selected.file == e.file) { target = selected.slot; break; }
        if (target >= 0) text = "Selected. Slot " + juce::String(target + 1) + ", " + text;
    }
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

void LSampler24AudioProcessorEditor::cycleSlotLibraryEntryByInitial(juce::juce_wchar initial)
{
    if (slotLibraryEntries.empty()) return;
    const auto target = juce::CharacterFunctions::toLowerCase(initial);
    slotLibraryLastInitial = target;
    const int count = int(slotLibraryEntries.size());
    for (int offset = 1; offset <= count; ++offset)
    {
        const int index = (slotLibraryEntryIndex + offset) % count;
        auto name = slotLibraryEntries[size_t(index)].directory ? slotLibraryEntries[size_t(index)].file.getFileName()
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
    std::vector<SlotLibrarySelection> work = slotLibrarySelection;
    if (work.empty())
    {
        const auto& e = slotLibraryEntries[size_t(slotLibraryEntryIndex)];
        if (e.directory) return;
        int target = processor.getCurrentSlot();
        work.push_back({ e.file, target });
    }
    juce::String firstError;
    int highest = -1;
    bool any = false;
    for (const auto& item : work)
    {
        juce::String error;
        if (processor.loadSlotPresetToSlot(item.file, item.slot, error))
        {
            highest = juce::jmax(highest, item.slot); any = true;
        }
        else if (firstError.isEmpty()) firstError = error;
    }
    if (!any)
    {
        if (firstError.isNotEmpty()) lsampler::announceToActiveScreenReader(slotLibraryCell, firstError);
        return;
    }
    processor.setCurrentSlot(highest);
    leaveSlotLibraryBrowser(true);
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
            }
            safeThis->returnToCurrentSlotAndAnnounce();
        });
}

void LSampler24AudioProcessorEditor::chooseSaveSlot()
{
    // The native Save dialog shows only the editable logical slot name.
    // Prefix and LSampler extension are added after the user confirms.
    auto logicalName = processor.getCurrentSampleFile().getFileNameWithoutExtension();
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
                juce::String error;
                const bool ok = safeThis->processor.saveSlotPreset(file, error);
                safeThis->showResult(ok, error, "Slot saved: " + file.getFileNameWithoutExtension());
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
                juce::String error;
                const bool ok = safeThis->processor.loadBankPreset(file, error);
                safeThis->showResult(ok, error, "Bank loaded: " + file.getFileNameWithoutExtension());
            }
            safeThis->returnToCurrentSlotAndAnnounce();
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
            safeThis->returnToCurrentSlotAndAnnounce();
        });
}
