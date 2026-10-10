#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "RackGui.h"
#include <bitset>
#include <deque>

// One owned, focusable modal surface: all unassigned keys and key-up events
// terminate here, before main-grid shortcuts or host dispatch.
class SliceEditor final : public juce::Component {
public:
    SliceEditor(LSampler24AudioProcessor&,int slot,bool sequencePage);
    ~SliceEditor() override;
    std::function<void()> onClose;
    std::function<bool(juce::AudioProcessorParameter*, juce::Component*)> onToggleHostAutomationEnvelope;
    std::function<int(juce::AudioProcessorParameter*)> onHostAutomationEnvelopeState;
    void refreshAutomationIndicator();
    juce::String automationAnnouncement() const { return currentLine(); }
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override {return true;}
    void paint(juce::Graphics&) override;
    void resized() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void announceEntry();
    void refreshVisuals();
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
private:
    rackgui::Waveform waveform;
    rackgui::SliceOverview overview;
    rackgui::CurrentParameter currentEdit;
    enum class Page { globals, steps, boundaries };
    class NumberEditor final : public juce::TextEditor {
    public:
        std::function<void(bool)> finish;
        bool keyPressed(const juce::KeyPress& k) override {
            if(k.getKeyCode()==juce::KeyPress::returnKey||k.getKeyCode()==juce::KeyPress::escapeKey) {
                if(finish)finish(k.getKeyCode()==juce::KeyPress::returnKey);return true;
            }
            if(k.getKeyCode()==juce::KeyPress::tabKey)return true;
            juce::TextEditor::keyPressed(k);return true;
        }
        bool keyStateChanged(bool) override {return true;}
    } number;
    LSampler24AudioProcessor& processor;
    const int slot;
    Page page=Page::globals;
    int item=0,property=0,global=0,boundarySide=1,anchor=-1;
    int stepWidthIndex=0;
    bool numeric=false;
    bool valueMode=false;
    bool manualSpacePreviewPlaying=false; // toggled only by manual Space
    int lastPreviewItem=-1;
    std::bitset<128> selected;
    std::vector<lsampler::SliceStep> clipboard;
    std::deque<lsampler::SliceState> undo,redo;
    juce::String line;
    bool automationActive = false;
    int count() const;
    juce::String currentLine() const;
    void speak(const juce::String& prefix={});
    void pushUndo();
    void commit(lsampler::SliceState&);
    void changeValue(int direction,bool coarse);
    void changeStepWidth(int direction);
    void setValueBoundary(bool maximum);
    void setValue(double value);
    void editNumber();
    void moveItem(int target,bool range);
    void preview(bool whole,bool slice,bool toggle=true);
    void auditionEdit();
    void setPage(Page);
    bool sequencerEnabled() const;
    void copy();
    void paste();
    void restore(bool forward);
};
