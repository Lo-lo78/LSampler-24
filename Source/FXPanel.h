#pragma once
#include "PluginProcessor.h"
#include "ScreenReaderAnnouncer.h"
#include <functional>

// Standalone, accessible Grid/Value FX page. Keeps its cursor when closed.
// All navigation works without a mouse or an NVDA object-navigation workaround.
class LSamplerFXPanel final : public juce::Component {
public:
    struct Grid : juce::ComboBox {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& k) override {
            return onKey && onKey(k) ? true : juce::ComboBox::keyPressed(k);
        }
    };
    struct Value : juce::Slider {
        std::function<bool(const juce::KeyPress&)> onKey;
        bool keyPressed(const juce::KeyPress& k) override {
            return onKey && onKey(k) ? true : juce::Slider::keyPressed(k);
        }
    };
    explicit LSamplerFXPanel(LSampler24AudioProcessor& owner):processor(owner) {
        setTitle("FX Grid");
        grid.setWantsKeyboardFocus(true);
        grid.setTitle("FX Grid");
        grid.setDescription("Up and Down parameters, Left and Right effect groups, Alt+V Value, Escape close");
        for (int i=0;i<lsampler::fx::count;++i)
            grid.addItem(textFor(i),i+1);
        grid.setSelectedItemIndex(0,juce::dontSendNotification);
        grid.onChange = [this] {
            const int item=grid.getSelectedItemIndex();
            if(item>=0) { cursor=item; configureValue(); }
        };
        grid.onKey=[this](const juce::KeyPress& k){return handle(k,false);};
        addAndMakeVisible(grid);
        value.setSliderStyle(juce::Slider::LinearHorizontal);
        value.setTextBoxStyle(juce::Slider::TextBoxRight,false,175,30);
        value.setWantsKeyboardFocus(true);
        value.onKey=[this](const juce::KeyPress& k){return handle(k,true);};
        value.onValueChange=[this] {
            if(synchronising)return;
            processor.setFXParameter(cursor,value.getValue());
            refreshRow(cursor);
            lsampler::announceToActiveScreenReader(value,textFor(cursor));
        };
        addAndMakeVisible(value);
        closeButton.setButtonText("Back");
        closeButton.setDescription("Escape / Alt+F");
        closeButton.setWantsKeyboardFocus(true);
        closeButton.onClick=[this]{if(onClose)onClose();};
        addAndMakeVisible(closeButton);
        configureValue();
    }
    std::function<void()> onClose;
    void enter() {
        for(int i=0;i<lsampler::fx::count;++i)refreshRow(i);
        configureValue();
        setVisible(true);toFront(true);
        grid.grabKeyboardFocus();
        lsampler::announceToActiveScreenReader(grid,"FX Grid. "+textFor(cursor));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1d2427));
        g.setColour(juce::Colour(0xffe8b76b));
        g.setFont(juce::Font(juce::FontOptions(24.f)));
        g.drawText("LSAMPLER-24 / MASTER FX",32,25,getWidth()-64,38,juce::Justification::centredLeft);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(juce::FontOptions(14.f)));
        g.drawText("Grid  |  FX Master  •  Chorus  •  Dual Delay  •  Reverb",32,80,getWidth()-64,25,juce::Justification::centredLeft);
        g.drawText("Alt+F: Close    Alt+L: Grid    Alt+V: Value    Esc: Back    Tab: Grid / Value",32,380,getWidth()-64,28,juce::Justification::centredLeft);
        g.drawText("Up/Down: parameters   Left/Right: FX groups   Alt+arrows: change value   Backspace: default",32,420,getWidth()-64,28,juce::Justification::centredLeft);
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
        // An active REAPER automation envelope may change the focused value.
        if (!hasKeyboardFocus(true))return;
        const double v=processor.getFXParameter(cursor);
        if(std::abs(value.getValue()-v)>0.00001)configureValue();
    }
    bool keyPressed(const juce::KeyPress& k) override { return handle(k,false); }
private:
    LSampler24AudioProcessor& processor;
    Grid grid;
    Value value;
    juce::TextButton closeButton;
    int cursor=0;
    bool synchronising=false;
    juce::String textFor(int i) const {
        const auto& d=lsampler::fx::parameters[size_t(i)];
        const auto x=processor.getFXParameter(i);
        const auto labels=juce::StringArray::fromTokens(d.labels,"|","");
        const auto text=labels.isEmpty()?juce::String(x,d.decimals)+juce::String(d.unit)
            :labels[juce::jlimit(0,labels.size()-1,int(x-d.minimum))];
        return juce::String(d.name)+", "+text;
    }
    void refreshRow(int i) {
        grid.changeItemText(i+1,textFor(i));
    }
    void configureValue() {
        const auto& d=lsampler::fx::parameters[size_t(cursor)];
        synchronising=true;
        value.setRange(d.minimum,d.maximum,d.kind==lsampler::Kind::continuous ? d.step : 1.0);
        value.setName(d.name);
        value.setTitle(d.name);
        value.setDescription("FX Value. Alt+L Grid, Escape Grid");
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
        synchronising=false;
        repaint();
    }
    void choose(int index) {
        index=juce::jlimit(0,lsampler::fx::count-1,index);
        cursor=index;
        grid.setSelectedItemIndex(cursor,juce::sendNotificationSync);
        refreshRow(cursor);
        lsampler::announceToActiveScreenReader(grid,textFor(cursor));
    }
    int groupBegin(int index) const {
        const auto* category=lsampler::fx::parameters[size_t(index)].category;
        while(index>0 && std::strcmp(lsampler::fx::parameters[size_t(index-1)].category,category)==0)--index;
        return index;
    }
    int groupEnd(int index) const {
        const auto* category=lsampler::fx::parameters[size_t(index)].category;
        while(index+1<lsampler::fx::count && std::strcmp(lsampler::fx::parameters[size_t(index+1)].category,category)==0)++index;
        return index;
    }
    bool handle(const juce::KeyPress& k,bool valueFocused) {
        const auto mods=k.getModifiers();
        const auto code=k.getKeyCode();
        const auto shortcut=juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code));
        const bool alt=mods.isAltDown()&&!mods.isCtrlDown()&&!mods.isCommandDown();
        if(mods.isCtrlDown() && mods.isShiftDown() && !mods.isAltDown() && shortcut=='a') {
            auto* parameter=processor.getFXHostParameter(cursor);
            if(parameter != nullptr) {
                const int active=processor.getReaperEnvelopeActive(parameter);
                if(active>=0 && processor.toggleReaperEnvelopeActive(parameter)) {
                    lsampler::announceToActiveScreenReader(grid,
                        juce::String(lsampler::fx::parameters[size_t(cursor)].name)
                        +(active==0 ? ", automation On" : ", automation Off"));
                } else {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->getValue());
                    parameter->endChangeGesture();
                    lsampler::announceToActiveScreenReader(grid,
                        "Host automation. Last touched: "+juce::String(lsampler::fx::parameters[size_t(cursor)].name));
                }
            }
            return true;
        }
        if(alt && shortcut=='f') {if(onClose)onClose();return true;}
        if(alt && shortcut=='l') {grid.grabKeyboardFocus();return true;}
        if(alt && shortcut=='v') {value.grabKeyboardFocus();return true;}
        if(code==juce::KeyPress::escapeKey) {
            if(valueFocused)grid.grabKeyboardFocus();
            else if(onClose)onClose();
            return true;
        }
        if(code==juce::KeyPress::tabKey) {
            if(valueFocused)grid.grabKeyboardFocus();else value.grabKeyboardFocus();
            return true;
        }
        if(code==juce::KeyPress::returnKey) {
            if(valueFocused)value.showTextBox();
            else value.grabKeyboardFocus();
            return true;
        }
        if(code==juce::KeyPress::backspaceKey) {
            processor.setFXParameter(cursor,lsampler::fx::parameters[size_t(cursor)].initial);
            refreshRow(cursor);configureValue();
            lsampler::announceToActiveScreenReader(value,"Default. "+textFor(cursor));
            return true;
        }
        if(alt && (code==juce::KeyPress::upKey || code==juce::KeyPress::downKey
                   || code==juce::KeyPress::leftKey || code==juce::KeyPress::rightKey)) {
            changeValue(code==juce::KeyPress::downKey||code==juce::KeyPress::leftKey?-1:1,
                        code==juce::KeyPress::leftKey||code==juce::KeyPress::rightKey);
            return true;
        }
        if(valueFocused && !alt && !mods.isCtrlDown() && !mods.isShiftDown()
           && (code==juce::KeyPress::upKey || code==juce::KeyPress::downKey)) {
            changeValue(code==juce::KeyPress::downKey?-1:1,false);return true;
        }
        if(!valueFocused && !alt && !mods.isCtrlDown() && !mods.isShiftDown()) {
            if(code==juce::KeyPress::upKey) {choose(std::max(groupBegin(cursor),cursor-1));return true;}
            if(code==juce::KeyPress::downKey) {choose(std::min(groupEnd(cursor),cursor+1));return true;}
            if(code==juce::KeyPress::leftKey || code==juce::KeyPress::rightKey ||
               code==juce::KeyPress::pageUpKey || code==juce::KeyPress::pageDownKey) {
                const int begin=groupBegin(cursor),row=cursor-begin;
                const bool back=code==juce::KeyPress::leftKey||code==juce::KeyPress::pageUpKey;
                const int next=back ? (begin>0 ? groupBegin(begin-1) : begin)
                                    : (groupEnd(cursor)+1<lsampler::fx::count ? groupEnd(cursor)+1 : begin);
                choose(std::min(next+row,groupEnd(next)));return true;
            }
            if(code==juce::KeyPress::homeKey){choose(groupBegin(cursor));return true;}
            if(code==juce::KeyPress::endKey){choose(groupEnd(cursor));return true;}
        }
        if(valueFocused && code==juce::KeyPress::leftKey) {choose(std::max(0,cursor-1));return true;}
        if(valueFocused && code==juce::KeyPress::rightKey) {choose(std::min(lsampler::fx::count-1,cursor+1));return true;}
        return false;
    }
    void changeValue(int direction,bool coarse) {
        const auto& d=lsampler::fx::parameters[size_t(cursor)];
        const double step=d.kind==lsampler::Kind::continuous?d.step:1.0;
        const double next=juce::jlimit(d.minimum,d.maximum,processor.getFXParameter(cursor)+direction*step*(coarse?10:1));
        processor.setFXParameter(cursor,next);
        configureValue();refreshRow(cursor);
        lsampler::announceToActiveScreenReader(value,textFor(cursor));
    }
};
