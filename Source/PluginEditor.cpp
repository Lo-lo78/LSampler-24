#include "PluginEditor.h"
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
    importBrowserCell.addKeyListener(this);
    addChildComponent(importBrowserCell);
    importBrowserCell.setVisible(false);

    loadSample.onClick = [this] { enterImportBrowser(); };
    loadSlot.onClick   = [this] { chooseLoadSlot(); };
    saveSlot.onClick   = [this] { chooseSaveSlot(); };
    loadBank.onClick   = [this] { chooseLoadBank(); };
    saveBank.onClick   = [this] { chooseSaveBank(); };

    refreshSlotCells();
    leaveSlotParameters();
    startTimerHz(5);
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

    if (importBrowserActive)
    {
        importBrowserCell.setBounds(area.removeFromTop(42));
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
void LSampler24AudioProcessorEditor::setSelectedParameterValue(double value) {processor.setSlotParameter(selectedParameter,value,selectedLoop);}
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

    if (importBrowserActive)
    {
        if (code == juce::KeyPress::escapeKey) { leaveImportBrowser(true); return true; }
        if (code == juce::KeyPress::upKey && !mods.isShiftDown() && !mods.isCtrlDown()) { selectImportEntry(importEntryIndex - 1, true); return true; }
        if (code == juce::KeyPress::downKey && !mods.isShiftDown() && !mods.isCtrlDown()) { selectImportEntry(importEntryIndex + 1, true); return true; }
        if (code == juce::KeyPress::pageUpKey) { selectImportEntry(importEntryIndex - 10, true); return true; }
        if (code == juce::KeyPress::pageDownKey) { selectImportEntry(importEntryIndex + 10, true); return true; }
        if (code == juce::KeyPress::homeKey && !mods.isCtrlDown()) { selectImportEntry(0, true); return true; }
        if (code == juce::KeyPress::endKey && !mods.isCtrlDown()) { selectImportEntry(int(importEntries.size()) - 1, true); return true; }
        if (code == juce::KeyPress::backspaceKey)
        {
            if (importDriveList) return true;
            auto parent = importDirectory.getParentDirectory();
            if (parent != importDirectory)
            {
                importDirectory = parent;
                refreshImportEntries();
                selectImportEntry(0, true);
            }
            else
            {
                importDriveList = true;
                refreshImportEntries();
                selectImportEntry(0, true);
            }
            return true;
        }
        if (code == juce::KeyPress::spaceKey && !mods.isShiftDown())
        {
            importPreviewEnabled = !importPreviewEnabled;
            if (importPreviewEnabled) updateImportPreviewForSelection();
            else processor.requestImportPreviewStop();
            lsampler::announceToActiveScreenReader(importBrowserCell, importPreviewEnabled ? "Preview On" : "Preview Off");
            return true;
        }
        if (code == juce::KeyPress::spaceKey && mods.isShiftDown()) { toggleImportFileSelection(); return true; }
        const auto logicalKey = juce::CharacterFunctions::toLowerCase(static_cast<juce_wchar>(code));
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'q') { markImportSliceStart(); return true; }
        if (mods.isCtrlDown() && !mods.isAltDown() && logicalKey == 'w') { markImportSliceEnd(); return true; }
        if (mods.isCtrlDown() && (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey))
        {
            prepareImportPreviewForCurrent();
            processor.requestImportPreviewSeek(code == juce::KeyPress::homeKey ? 0.0 : processor.getImportPreviewLengthSeconds());
            return true;
        }
        if (!mods.isCtrlDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
        {
            seekImportPreview(code == juce::KeyPress::leftKey ? -1.0 : 1.0);
            return true;
        }
        if (code == juce::KeyPress::deleteKey) { deleteImportPlanItemAtCursor(); return true; }
        if (code == juce::KeyPress::returnKey)
        {
            if (importEntries.empty()) return true;
            const auto& entry = importEntries[static_cast<size_t>(importEntryIndex)];
            if (entry.directory)
            {
                importDirectory = entry.file;
                importDriveList = false;
                refreshImportEntries();
                selectImportEntry(0, true);
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

    // Ctrl+Left/Right scrubs Sample Play Start without moving Grid/Value focus.
    // If Space preview is already playing, restart it from the new start position.
    if (parameterPage && mods.isCtrlDown() && !mods.isAltDown() && !mods.isShiftDown()
        && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        int sampleStartIndex = -1;
        for (int i = 0; i < static_cast<int>(lsampler::grid.size()); ++i)
            if (lsampler::grid[static_cast<size_t>(i)].parameter == int(lsampler::P::sample_play_start))
            {
                sampleStartIndex = i;
                break;
            }

        if (sampleStartIndex >= 0)
        {
            const auto& d = descriptor(lsampler::grid[static_cast<size_t>(sampleStartIndex)]);
            const double step = d.step * static_cast<double>(stepWidths[static_cast<size_t>(stepWidthIndex)]);
            const double current = processor.getSlotParameter(sampleStartIndex);
            const double next = juce::jlimit(d.minimum, d.maximum,
                                             current + (code == juce::KeyPress::rightKey ? step : -step));
            if (std::abs(next - current) >= 1.0e-9)
            {
                processor.setSlotParameter(sampleStartIndex, next);
                refreshParameterGrid();
                processor.requestPreviewRestartIfPlaying();
                // Deliberately silent for screen readers: Ctrl+Left/Right is an auditory scrub.
                // Normal editing of Sample Play Start still announces values through the regular parameter path.
            }
            return true;
        }
    }

    if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())
    {
        if (ch == 'o' && !mods.isShiftDown()) { enterImportBrowser(); return true; }
        if (ch == 's' && !mods.isShiftDown()) { chooseLoadSlot(); return true; }
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
    importPreviewEnabled = false;
    importDriveList = false;
    parameterPage = false;
    importPlan.clear();
    importSlicePending = false;
    importPreviewFile = {};
    importStartSlot = processor.getCurrentSlot();

    const auto current = processor.getCurrentSampleFile();
    importDirectory = current.existsAsFile() ? current.getParentDirectory()
                                             : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (!importDirectory.isDirectory())
        importDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    for (auto& cell : slotCells) { cell.setVisible(false); cell.setWantsKeyboardFocus(false); }
    loadSample.setVisible(false); loadSlot.setVisible(false); saveSlot.setVisible(false);
    loadBank.setVisible(false); saveBank.setVisible(false);
    parameterSelector.setVisible(false); parameterValue.setVisible(false);
    importBrowserCell.setVisible(true);
    importBrowserCell.setWantsKeyboardFocus(true);
    refreshImportEntries();
    resized();
    importBrowserCell.grabKeyboardFocus();
}

void LSampler24AudioProcessorEditor::leaveImportBrowser(bool announceSlot)
{
    processor.requestImportPreviewStop();
    importPreviewEnabled = false;
    importBrowserActive = false;
    importBrowserCell.setVisible(false);
    importBrowserCell.setWantsKeyboardFocus(false);
    for (auto& cell : slotCells) { cell.setVisible(true); cell.setWantsKeyboardFocus(true); }
    loadSample.setVisible(true); loadSlot.setVisible(true); saveSlot.setVisible(true);
    loadBank.setVisible(true); saveBank.setVisible(true);
    resized();
    if (announceSlot) returnToCurrentSlotAndAnnounce();
}

void LSampler24AudioProcessorEditor::refreshImportEntries()
{
    importEntries.clear();
    if (importDriveList)
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
    if (importEntries.empty()) importBrowserCell.setBrowserText(importDriveList ? "No drives" : "Empty folder");
    else selectImportEntry(importEntryIndex, false);
}

void LSampler24AudioProcessorEditor::selectImportEntry(int index, bool announce)
{
    if (importEntries.empty()) { importEntryIndex = 0; importBrowserCell.setBrowserText("Empty folder"); return; }
    importEntryIndex = juce::jlimit(0, int(importEntries.size()) - 1, index);
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (importPreviewFile.getFullPathName().isNotEmpty() && importPreviewFile != e.file)
        importPreviewFile = {};
    juce::String text = e.file.getFileName();
    if (text.isEmpty()) text = e.file.getFullPathName();
    if (e.directory) text += importDriveList ? ", drive" : ", folder";
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
    importBrowserCell.setBrowserText(text);
    if (announce) announceImportEntry();
    updateImportPreviewForSelection();
}

void LSampler24AudioProcessorEditor::cycleImportEntryByInitial(juce::juce_wchar initial)
{
    if (importEntries.empty()) return;
    const auto target = juce::CharacterFunctions::toLowerCase(initial);
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
        processor.requestImportPreviewToggle();
}

void LSampler24AudioProcessorEditor::announceImportEntry()
{
    if (importEntries.empty())
    {
        lsampler::announceToActiveScreenReader(importBrowserCell, "Empty folder");
        return;
    }
    lsampler::announceToActiveScreenReader(importBrowserCell, importBrowserCell.getTitle());
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
    importSliceFile = e.file;
    importSliceStart = processor.getImportPreviewPositionSeconds();
    int sliceNumber = 1;
    for (const auto& item : importPlan) if (item.slice) ++sliceNumber;
    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Slice " + juce::String(sliceNumber) + " slot " + juce::String(slot + 1) + " start " + juce::String(importSliceStart, 3));
}

void LSampler24AudioProcessorEditor::markImportSliceEnd()
{
    if (!importSlicePending) { lsampler::announceToActiveScreenReader(importBrowserCell, "No slice start"); return; }
    if (importEntries.empty()) return;
    const auto& e = importEntries[static_cast<size_t>(importEntryIndex)];
    if (e.directory || e.file != importSliceFile) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slice source changed"); return; }
    double end = processor.getImportPreviewPositionSeconds();
    double start = importSliceStart;
    if (end < start) std::swap(start, end);
    if (end - start <= 0.001) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slice end not valid"); return; }
    const int slot = nextImportFreeSlot();
    if (slot < 0) { lsampler::announceToActiveScreenReader(importBrowserCell, "Slots occupied"); return; }
    importPlan.push_back({ e.file, slot, true, start, end });
    importSlicePending = false;
    int sliceNumber = 0; for (const auto& item : importPlan) if (item.slice) ++sliceNumber;
    selectImportEntry(importEntryIndex, false);
    lsampler::announceToActiveScreenReader(importBrowserCell,
        "Slice " + juce::String(sliceNumber) + " slot " + juce::String(slot + 1) + " end " + juce::String(end, 3)
        + ", length " + juce::String(end - start, 3));
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
            importPlan.erase(it); importSlicePending = false; selectImportEntry(importEntryIndex, false);
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
    importPlan.clear(); importSlicePending = false;
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
