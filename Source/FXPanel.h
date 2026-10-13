#pragma once
#include "PluginProcessor.h"
#include "ScreenReaderAnnouncer.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

// TEST145: Match the sampler's existing Grid/Value keyboard and NVDA contract.
// No wrap/announcement at selection or value boundaries; Alt+F is an entry
// shortcut, never an exit shortcut. Escape is the keyboard exit path.
class LSamplerFXPanel final : public juce::Component {
public:
    struct Grid final : juce::ComboBox {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& k) override {
            if (onKey && onKey(k)) return true;
            if (k.getKeyCode() == juce::KeyPress::spaceKey)
                return false; // DAW Space when Alt+P slot audition is off
            return juce::ComboBox::keyPressed(k);
        }
        void focusGained(FocusChangeType cause) override {
            setTitle("FX Grid");
            setDescription({});
            juce::ComboBox::focusGained(cause);
            juce::Timer::callAfterDelay(1, [safe=juce::Component::SafePointer<Grid>(this)] {
                if (safe != nullptr && safe->hasKeyboardFocus(true))
                    if (auto* handler = safe->getAccessibilityHandler()) handler->grabFocus();
            });
            juce::Timer::callAfterDelay(220, [safe=juce::Component::SafePointer<Grid>(this)] {
                if (safe != nullptr) { safe->setTitle({}); safe->setDescription({}); }
            });
        }
    };
    struct Value final : juce::Slider {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& k) override {
            if (onKey && onKey(k)) return true;
            if (k.getKeyCode() == juce::KeyPress::spaceKey)
                return false; // DAW Space when Alt+P slot audition is off
            return juce::Slider::keyPressed(k);
        }
        void setLineReadingMode() {
            setTitle(parameterName);
            setName(parameterName);
            // NVDA+Up reads only parameter and value once the entry cue ends.
            setDescription({}); // no second, persistent shortcut announcement
        }
        void setEntryAccessibility() {
            setTitle("Value. " + parameterName);
            setName("Value. " + parameterName);
            // Same one-time focus cue as the main sampler Value control.
            // Removed from the accessible description after 220 ms, so
            // subsequent changes are announced once by the native slider.
            setDescription("Alt+V");
        }
        void setParameterName(const juce::String& name) {
            parameterName = name;
            if (!hasKeyboardFocus(true)) setLineReadingMode();
        }
        void focusGained(FocusChangeType cause) override {
            setEntryAccessibility();
            juce::Slider::focusGained(cause);
            juce::Timer::callAfterDelay(1, [safe=juce::Component::SafePointer<Value>(this)] {
                if (safe != nullptr && safe->hasKeyboardFocus(true))
                    if (auto* handler = safe->getAccessibilityHandler()) handler->grabFocus();
            });
            juce::Timer::callAfterDelay(220, [safe=juce::Component::SafePointer<Value>(this)] {
                if (safe != nullptr) safe->setLineReadingMode();
            });
        }
    private:
        juce::String parameterName;
    };

    // UI order is independent of persistent VST3 parameter IDs.
    // Keep delay and reverb additions in their respective groups.
    static constexpr std::array<int, lsampler::fx::count> displayOrder {{
        0, 1, 2, 3, 4,
        5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        lsampler::fx::delayGain,
        17, 18, 19, 20, 21, 22, 23, 24, 25,
        lsampler::fx::reverbXOver, lsampler::fx::reverbBass, lsampler::fx::reverbGain
    }};
    static int displayPosition(int parameter) noexcept {
        for (int i=0; i<lsampler::fx::count; ++i)
            if (displayOrder[size_t(i)]==parameter) return i;
        return 0;
    }
    explicit LSamplerFXPanel(LSampler24AudioProcessor& owner):processor(owner) {
        setTitle("FX Grid");
        grid.setWantsKeyboardFocus(true);
        grid.setDescription({});
        for (int i=0;i<lsampler::fx::count;++i) grid.addItem(textFor(displayOrder[size_t(i)]),i+1);
        grid.setSelectedItemIndex(0,juce::dontSendNotification);
        grid.onChange = [this] {
            const int item=grid.getSelectedItemIndex();
            if(item>=0) { cursor=displayOrder[size_t(item)]; configureValue(); }
        };
        grid.onKey=[this](const juce::KeyPress& k){return handle(k,false);};
        addAndMakeVisible(grid);
        value.setSliderStyle(juce::Slider::LinearHorizontal);
        value.setTextBoxStyle(juce::Slider::TextBoxRight,false,175,30);
        value.setWantsKeyboardFocus(true);
        value.onKey=[this](const juce::KeyPress& k){return handle(k,true);};
        value.onValueChange=[this] {
            if(synchronising)return;
            const auto before=processor.getFXParameter(cursor);
            processor.setFXParameter(cursor,value.getValue());
            if(std::abs(before-processor.getFXParameter(cursor))>1.0e-9) refreshRow(cursor);
            // Slider already publishes its own accessible value change. Do not
            // also raise an explicit UIA notification on the slider: NVDA
            // used to say "FX Master On / FX Master, On" twice.
        };
        addAndMakeVisible(value);
        closeButton.setButtonText("Back");
        closeButton.setDescription("Escape");
        closeButton.setWantsKeyboardFocus(true);
        closeButton.onClick=[this]{if(onClose)onClose();};
        addAndMakeVisible(closeButton);
        configureValue();
    }

    std::function<void()> onClose;
    // Preview uses the editor's *same* Alt+P / Space logic as the Slots page.
    // FX has no separate audition engine or mode state.
    std::function<void(juce::Component&)> onToggleSlotPreview;
    std::function<bool()> onPreviewSpace;
    void enter() {
        for(int i=0;i<lsampler::fx::count;++i) refreshRow(i);
        grid.setSelectedItemIndex(displayPosition(cursor),juce::dontSendNotification);
        configureValue();
        setVisible(true);
        toFront(true);
        grid.grabKeyboardFocus(); // focus event supplies ONE native grid entry
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1d2427));
        g.setColour(juce::Colour(0xffe8b76b));
        g.setFont(juce::Font(juce::FontOptions(24.f)));
        g.drawText("LSAMPLER-24 / MASTER FX",32,25,getWidth()-64,38,juce::Justification::centredLeft);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(juce::FontOptions(14.f)));
        g.drawText("Grid  |  FX Master  •  Chorus  •  Dual Delay  •  Reverb",32,80,getWidth()-64,25,juce::Justification::centredLeft);
        g.drawText("Alt+L: Grid    Alt+V: Value    Esc: Grid / Close    Tab: Grid / Value",32,380,getWidth()-64,28,juce::Justification::centredLeft);
        g.drawText("Alt+arrows: edit/step   Alt+PgUp/PgDn: coarse   Alt+Home/End: limits",32,420,getWidth()-64,28,juce::Justification::centredLeft);
        g.drawText("Alt+P: Slot preview on/off   Space: preview when enabled",32,457,getWidth()-64,24,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffe8b76b));
        g.drawRoundedRectangle(juce::Rectangle<float>(24,145,float(getWidth()-48),220),5,2);
        g.setFont(juce::Font(juce::FontOptions(19.f)));
        g.drawText(juce::String(lsampler::fx::parameters[size_t(cursor)].category),42,153,getWidth()-84,40,juce::Justification::centredLeft);
    }
    void resized() override {
        const int w=getWidth();
        grid.setBounds(42,213,w-84,45);
        value.setBounds(42,282,w-84,45);
        closeButton.setBounds(w-196,getHeight()-73,148,36);
    }
    void refreshIfAutomated() {
        if (!hasKeyboardFocus(true))return;
        const double v=processor.getFXParameter(cursor);
        if(std::abs(value.getValue()-v)>0.00001) {
            configureValue();
            refreshRow(cursor);
        }
    }
    bool keyPressed(const juce::KeyPress& k) override { return handle(k,false); }
private:
    static constexpr std::array<int,5> stepWidths {1,5,10,15,20};
    static constexpr int pageStep = 40;
    LSampler24AudioProcessor& processor;
    Grid grid;
    Value value;
    juce::TextButton closeButton;
    int cursor=0,stepWidthIndex=0;
    bool synchronising=false;

    juce::String textFor(int i) const {
        const auto& d=lsampler::fx::parameters[size_t(i)];
        const auto x=processor.getFXParameter(i);
        const auto labels=juce::StringArray::fromTokens(d.labels,"|","");
        const auto text=labels.isEmpty()?juce::String(x,d.decimals)+juce::String(d.unit)
            :labels[juce::jlimit(0,labels.size()-1,int(std::round(x-d.minimum)))];
        return juce::String(d.name)+", "+text;
    }
    void refreshRow(int i) { grid.changeItemText(displayPosition(i)+1,textFor(i)); }
    void configureValue() {
        const auto& d=lsampler::fx::parameters[size_t(cursor)];
        synchronising=true;
        value.setRange(d.minimum,d.maximum,d.kind==lsampler::Kind::continuous ? d.step : 1.0);
        value.setParameterName(d.name);
        value.textFromValueFunction=[this](double v) {
            const auto& desc=lsampler::fx::parameters[size_t(cursor)];
            const auto labels=juce::StringArray::fromTokens(desc.labels,"|","");
            if(!labels.isEmpty())return labels[juce::jlimit(0,labels.size()-1,int(std::round(v-desc.minimum)))];
            return juce::String(v,desc.decimals)+juce::String(desc.unit);
        };
        value.valueFromTextFunction=[this](const juce::String& s) {
            const auto& desc=lsampler::fx::parameters[size_t(cursor)];
            const auto labels=juce::StringArray::fromTokens(desc.labels,"|","");
            const int index=labels.indexOf(s.trim(),true);
            return index>=0?double(index)+desc.minimum:s.getDoubleValue();
        };
        value.setValue(processor.getFXParameter(cursor),juce::dontSendNotification);
        if (value.hasKeyboardFocus(true)) value.setLineReadingMode();
        synchronising=false;
        repaint();
    }
    void choose(int index) {
        index=juce::jlimit(0,lsampler::fx::count-1,index);
        if (index==cursor) return; // silent selection edge
        cursor=index;
        grid.setSelectedItemIndex(displayPosition(cursor),juce::sendNotificationSync);
        refreshRow(cursor);
        // ComboBox selection changes are announced natively. No duplicate
        // notification (and no stale accessibility description).
    }
    int groupBegin(int position) const {
        const auto* category=lsampler::fx::parameters[size_t(displayOrder[size_t(position)])].category;
        while(position>0 && std::strcmp(lsampler::fx::parameters[size_t(displayOrder[size_t(position-1)])].category,category)==0)--position;
        return position;
    }
    int groupEnd(int position) const {
        const auto* category=lsampler::fx::parameters[size_t(displayOrder[size_t(position)])].category;
        while(position+1<lsampler::fx::count && std::strcmp(lsampler::fx::parameters[size_t(displayOrder[size_t(position+1)])].category,category)==0)++position;
        return position;
    }
    void choosePosition(int position) { choose(displayOrder[size_t(juce::jlimit(0,lsampler::fx::count-1,position))]); }
    void announceGrid(const juce::String& message) {
        // One transient UIA notification. Never leave a duplicate description
        // that NVDA will read again when the control is focused.
        lsampler::announceToActiveScreenReader(grid,message);
        grid.setDescription({});
    }
    void changeStepWidth(int direction) {
        const int next=juce::jlimit(0,int(stepWidths.size())-1,stepWidthIndex+direction);
        if (next==stepWidthIndex) return; // silent step edge
        stepWidthIndex=next;
        // Announce on the grid, not the focused slider, to keep its title clean.
        announceGrid("Step "+juce::String(stepWidths[size_t(stepWidthIndex)]));
    }
    void changeValue(int direction,bool coarse,bool valueFocused) {
        const auto& d=lsampler::fx::parameters[size_t(cursor)];
        const double base=d.kind==lsampler::Kind::continuous?d.step:1.0;
        const double current=processor.getFXParameter(cursor);
        const double next=juce::jlimit(d.minimum,d.maximum,
            current+direction*base*double(stepWidths[size_t(stepWidthIndex)])*(coarse?pageStep:1));
        if(std::abs(next-current)<1.0e-9)return; // silent value edge
        processor.setFXParameter(cursor,next);
        configureValue();refreshRow(cursor);
        if (valueFocused) {
            // Exactly one native slider value-change event, no UIA text
            // notification that would repeat "FX Master, On".
            if(auto* handler=value.getAccessibilityHandler())
                handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        } else {
            announceGrid(textFor(cursor));
        }
    }
    void setBoundary(bool maximum,bool valueFocused) {
        const auto& d=lsampler::fx::parameters[size_t(cursor)];
        const double next=maximum?d.maximum:d.minimum;
        if(std::abs(processor.getFXParameter(cursor)-next)<1.0e-9)return;
        processor.setFXParameter(cursor,next);
        configureValue();refreshRow(cursor);
        if (valueFocused) {
            if(auto* handler=value.getAccessibilityHandler())
                handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        } else announceGrid(textFor(cursor));
    }
    bool handle(const juce::KeyPress& k,bool valueFocused) {
        const auto mods=k.getModifiers();
        const auto code=k.getKeyCode();
        const auto shortcut=juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code));
        const bool alt=mods.isAltDown()&&!mods.isCtrlDown()&&!mods.isCommandDown()&&!mods.isShiftDown();
        if(mods.isCtrlDown() && mods.isShiftDown() && !mods.isAltDown() && shortcut=='a') {
            auto* parameter=processor.getFXHostParameter(cursor);
            if(parameter != nullptr) {
                const int active=processor.getReaperEnvelopeActive(parameter);
                if(active>=0 && processor.toggleReaperEnvelopeActive(parameter)) {
                    announceGrid(
                        juce::String(lsampler::fx::parameters[size_t(cursor)].name)
                        +(active==0 ? ", automation On" : ", automation Off"));
                } else {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->getValue());
                    parameter->endChangeGesture();
                    announceGrid(
                        "Host automation. Last touched: "+juce::String(lsampler::fx::parameters[size_t(cursor)].name));
                }
            }
            return true;
        }
        if(alt && shortcut=='f') return true; // Alt+F only opens FX; Esc closes it
        if(alt && shortcut=='p') {
            if (onToggleSlotPreview) {
                onToggleSlotPreview(valueFocused ? static_cast<juce::Component&>(value)
                                                 : static_cast<juce::Component&>(grid));
                // The announcement utility temporarily sets the component's
                // accessible description. Clear it immediately so that NVDA
                // does not repeat "Slot preview On" on the next value change.
                if (valueFocused) value.setLineReadingMode();
                else grid.setDescription({});
            }
            return true;
        }
        // Mirror the Slots page: Space auditions only while Alt+P preview is
        // enabled; otherwise it goes to the DAW (never opens the FX ComboBox).
        if(code==juce::KeyPress::spaceKey && !mods.isAltDown() && !mods.isCtrlDown()
           && !mods.isShiftDown() && !mods.isCommandDown())
            return onPreviewSpace ? onPreviewSpace() : false;
        if(alt && shortcut=='l') {grid.grabKeyboardFocus();return true;}
        if(alt && shortcut=='v') {value.grabKeyboardFocus();return true;}
        if(code==juce::KeyPress::escapeKey) {
            if(valueFocused)grid.grabKeyboardFocus();
            else if(onClose)onClose();
            return true;
        }
        if(code==juce::KeyPress::tabKey) {
            if(valueFocused)grid.grabKeyboardFocus(); else value.grabKeyboardFocus();
            return true;
        }
        if(code==juce::KeyPress::returnKey) {
            if(valueFocused)value.showTextBox(); else value.grabKeyboardFocus();
            return true;
        }
        if(code==juce::KeyPress::backspaceKey) {
            const auto& d=lsampler::fx::parameters[size_t(cursor)];
            if(std::abs(processor.getFXParameter(cursor)-d.initial)<1.0e-9)return true;
            processor.setFXParameter(cursor,d.initial);
            configureValue();refreshRow(cursor);
            announceGrid("Default. "+textFor(cursor));
            return true;
        }
        if(alt) {
            if(code==juce::KeyPress::upKey) {changeValue(1,false,valueFocused);return true;}
            if(code==juce::KeyPress::downKey) {changeValue(-1,false,valueFocused);return true;}
            if(code==juce::KeyPress::leftKey) {changeStepWidth(-1);return true;}
            if(code==juce::KeyPress::rightKey) {changeStepWidth(1);return true;}
            if(code==juce::KeyPress::pageUpKey) {changeValue(1,true,valueFocused);return true;}
            if(code==juce::KeyPress::pageDownKey) {changeValue(-1,true,valueFocused);return true;}
            if(code==juce::KeyPress::homeKey) {setBoundary(true,valueFocused);return true;}
            if(code==juce::KeyPress::endKey) {setBoundary(false,valueFocused);return true;}
        }
        if(!mods.isAltDown()&&!mods.isCtrlDown()&&!mods.isShiftDown()&&!mods.isCommandDown()) {
            if(valueFocused) {
                if(code==juce::KeyPress::upKey) {changeValue(1,false,true);return true;}
                if(code==juce::KeyPress::downKey) {changeValue(-1,false,true);return true;}
                if(code==juce::KeyPress::leftKey) {changeStepWidth(-1);return true;}
                if(code==juce::KeyPress::rightKey) {changeStepWidth(1);return true;}
                if(code==juce::KeyPress::pageUpKey) {changeValue(1,true,true);return true;}
                if(code==juce::KeyPress::pageDownKey) {changeValue(-1,true,true);return true;}
                if(code==juce::KeyPress::homeKey) {setBoundary(true,true);return true;}
                if(code==juce::KeyPress::endKey) {setBoundary(false,true);return true;}
            } else {
                if(code==juce::KeyPress::upKey) {choosePosition(std::max(groupBegin(displayPosition(cursor)),displayPosition(cursor)-1));return true;}
                if(code==juce::KeyPress::downKey) {choosePosition(std::min(groupEnd(displayPosition(cursor)),displayPosition(cursor)+1));return true;}
                if(code==juce::KeyPress::leftKey || code==juce::KeyPress::rightKey) {
                    const int pos=displayPosition(cursor),begin=groupBegin(pos),row=pos-begin;
                    const bool back=code==juce::KeyPress::leftKey;
                    const int next=back ? (begin>0 ? groupBegin(begin-1) : begin)
                                        : (groupEnd(pos)+1<lsampler::fx::count ? groupEnd(pos)+1 : begin);
                    choosePosition(std::min(next+row,groupEnd(next)));return true;
                }
                if(code==juce::KeyPress::pageUpKey) {choosePosition(std::max(groupBegin(displayPosition(cursor)),displayPosition(cursor)-8));return true;}
                if(code==juce::KeyPress::pageDownKey) {choosePosition(std::min(groupEnd(displayPosition(cursor)),displayPosition(cursor)+8));return true;}
                if(code==juce::KeyPress::homeKey) {choosePosition(groupBegin(displayPosition(cursor)));return true;}
                if(code==juce::KeyPress::endKey) {choosePosition(groupEnd(displayPosition(cursor)));return true;}
            }
        }
        return false;
    }
};
