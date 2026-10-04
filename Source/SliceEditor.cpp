#include "SliceEditor.h"
#include "ScreenReaderAnnouncer.h"
using namespace lsampler;
SliceEditor::SliceEditor(LSampler24AudioProcessor& p,int s,bool sequencer):processor(p),slot(s) {
    page=sequencer?Page::steps:Page::boundaries;
    setWantsKeyboardFocus(true);setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    setOpaque(true);addChildComponent(number);number.setInputRestrictions(32,"-0123456789.");
    number.setTitle("Value");number.setMultiLine(false);
    number.finish=[this](bool accept) {
        const auto text=number.getText().trim();
        number.setVisible(false);numeric=false;grabKeyboardFocus();
        if(accept&&text.isNotEmpty()&&text!="-"&&text!="."&&text!="-.")setValue(text.getDoubleValue());
        else speak();
    };
}
SliceEditor::~SliceEditor(){processor.stopSlicePreview();}
std::unique_ptr<juce::AccessibilityHandler> SliceEditor::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::list);
}
int SliceEditor::count() const {
    int start=0;double rate=0;return processor.getSliceLayout(slot,start,rate).count;
}
juce::String SliceEditor::currentLine() const {
    const auto state=processor.getSliceState(slot);
    if(page==Page::globals) {
        const auto& d=sliceGlobals[size_t(global)];return juce::String(d.name)+", "+sliceValueText(d,state.globals[size_t(global)]);
    }
    if(page==Page::steps) {
        const auto& d=sliceProperties[size_t(property)];
        return "Step "+juce::String(item+1)+(selected[size_t(item)]?", Selected. ":". ")
            +d.name+", "+sliceValueText(d,state.steps[size_t(item)].values[size_t(property)]);
    }
    int start=0;double rate=0;const auto layout=processor.getSliceLayout(slot,start,rate);
    const int b=juce::jlimit(0,layout.count,item+boundarySide);
    const int frame=start+layout.boundaries[size_t(b)];
    return "Slice "+juce::String(item+1)+". "+(boundarySide==0?"Start":"End")+", "
        +juce::String(frame)+" samples, "+juce::String(1000.0*frame/std::max(1.0,rate),3)+" ms"
        +(b==0||b==layout.count?". Sample window edge":"")+". Zero Crossing "+(state.zeroCrossing?"On":"Off");
}
void SliceEditor::speak(const juce::String& prefix) {
    line=currentLine();setTitle(line);setName({});setDescription({});repaint();
    if(auto* h=getAccessibilityHandler())h->notifyAccessibilityEvent(juce::AccessibilityEvent::titleChanged);
    announceToActiveScreenReader(*this,prefix+line);setDescription({});
}
void SliceEditor::announceEntry() {
    grabKeyboardFocus();if(auto* h=getAccessibilityHandler())h->grabFocus();
    speak("Slice Edit. Slot "+juce::String(slot+1)+". "+juce::String(count())+" slices. "
        +(page==Page::steps?"Slice Sequencer. ":"Boundaries. "));
}
void SliceEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff18202a));g.setColour(juce::Colours::white);g.setFont(22.0f);
    g.drawText("LSampler-24 - Slice Edit",20,14,getWidth()-40,34,juce::Justification::centredLeft);
    g.setFont(17.0f);
    const juce::String pageName=page==Page::boundaries?"Boundaries":page==Page::globals?"Global Slice Settings":"Slice Sequencer";
    g.drawText(pageName+" / Slot "+juce::String(slot+1),20,62,getWidth()-40,30,juce::Justification::centredLeft);
    g.drawFittedText(line,getLocalBounds().reduced(20).withTrimmedTop(100).withHeight(120),juce::Justification::centredLeft,4);
    g.setFont(14.0f);
    g.drawFittedText("Tab: page | Up/Down: item | Left/Right: property (globals: value)\nPage Up/Down: value | Enter: type value | Ctrl+Space: slice | Space: sample/sequence\nF1: help | Escape: close",20,getHeight()-110,getWidth()-40,90,juce::Justification::centredLeft,4);
}
void SliceEditor::resized(){number.setBounds(20,getHeight()/2,getWidth()-40,40);}
void SliceEditor::pushUndo() {
    undo.push_back(processor.getSliceState(slot));if(undo.size()>64)undo.pop_front();redo.clear();
}
void SliceEditor::commit(SliceState& state) {processor.setSliceState(slot,state);auditionEdit();speak();}
void SliceEditor::auditionEdit() {
    const int kind=processor.getSlicePreviewKind();
    if(kind==2||kind==4)preview(false,true,false);
}
void SliceEditor::setValue(double value) {
    if(page==Page::boundaries)return;
    auto s=processor.getSliceState(slot);
    const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
    value=sliceSanitise(d,value);
    if(page==Page::globals) {
        if(s.globals[size_t(global)]==value)return;
        pushUndo();
        if(global==int(SliceG::division)) {
            s.setDivision(int(value));item=std::min(item,s.division()-1);selected.reset();anchor=-1;
        } else s.globals[size_t(global)]=value;
    } else {
        if(property==int(SliceP::source))value=std::min(value,double(count()));
        bool changed=false;
        for(int i=0;i<count();++i)if((selected.any()?selected[size_t(i)]:i==item)&&s.steps[size_t(i)].values[size_t(property)]!=value)changed=true;
        if(!changed)return;
        pushUndo();
        for(int i=0;i<count();++i)if(selected.any()?selected[size_t(i)]:i==item)s.steps[size_t(i)].values[size_t(property)]=value;
    }
    commit(s);
}
void SliceEditor::changeValue(int direction,bool coarse) {
    if(page==Page::boundaries) {
        const auto old=processor.getSliceState(slot);
        if(processor.moveSliceBoundary(slot,item+boundarySide,direction,coarse?100:1)) {
            undo.push_back(old);if(undo.size()>64)undo.pop_front();redo.clear();auditionEdit();speak();
        }
        return;
    }
    const auto s=processor.getSliceState(slot);
    const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
    const double value=page==Page::globals?s.globals[size_t(global)]:s.steps[size_t(item)].values[size_t(property)];
    setValue(value+direction*(coarse?std::max(1.0,d.step*10):d.step));
}
void SliceEditor::editNumber() {
    if(page==Page::boundaries){speak("Use Page Up and Page Down to move this boundary. ");return;}
    const auto s=processor.getSliceState(slot);
    const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
    const double value=page==Page::globals?s.globals[size_t(global)]:s.steps[size_t(item)].values[size_t(property)];
    number.setTitle(d.name);number.setText(juce::String(value,d.step==1?0:2),false);
    numeric=true;number.setVisible(true);number.grabKeyboardFocus();number.selectAll();
}
void SliceEditor::moveItem(int target,bool range) {
    target=juce::jlimit(0,std::max(0,count()-1),target);if(target==item)return;
    if(range&&page==Page::steps) {
        if(anchor<0)anchor=item;
        selected.reset();for(int i=std::min(anchor,target);i<=std::max(anchor,target);++i)selected.set(size_t(i));
    } else anchor=-1;
    item=target;auditionEdit();speak();
}
void SliceEditor::preview(bool whole,bool slice,bool toggle) {
    const int kind=whole?1:slice?(page==Page::boundaries?2:4):page==Page::boundaries?1:3;
    const bool stopping=toggle&&processor.getSlicePreviewKind()==kind&&(kind==1||kind==3||lastPreviewItem==item);
    processor.requestSlicePreview(slot,kind,item,toggle);lastPreviewItem=item;
    if(toggle) {
        const juce::String name=kind==1?"Sample preview":kind==3?"Sequence preview":kind==2?"Slice preview":"Step preview";
        announceToActiveScreenReader(*this,name+(stopping?" stopped":" started"));setDescription({});
    }
}
void SliceEditor::setPage(Page next) {
    page=next;item=std::min(item,std::max(0,count()-1));
    speak(page==Page::boundaries?"Boundaries. ":page==Page::globals?"Global Slice Settings. ":"Slice Sequencer. ");
}
void SliceEditor::copy() {
    if(page!=Page::steps)return;
    clipboard.clear();const auto s=processor.getSliceState(slot);
    for(int i=0;i<count();++i)if(selected.any()?selected[size_t(i)]:i==item)clipboard.push_back(s.steps[size_t(i)]);
    speak("Copied "+juce::String(int(clipboard.size()))+" steps. ");
}
void SliceEditor::paste() {
    if(page!=Page::steps||clipboard.empty())return;
    pushUndo();auto s=processor.getSliceState(slot);size_t k=0;
    for(int i=selected.any()?0:item;i<count();++i) {
        if(selected.any()&&!selected[size_t(i)])continue;
        if(!selected.any()&&k>=clipboard.size())break;
        s.steps[size_t(i)]=clipboard[k%clipboard.size()];++k;
    }
    commit(s);
}
void SliceEditor::restore(bool forward) {
    auto& from=forward?redo:undo;auto& to=forward?undo:redo;if(from.empty())return;
    to.push_back(processor.getSliceState(slot));if(to.size()>64)to.pop_front();
    processor.setSliceState(slot,from.back());from.pop_back();
    item=std::min(item,std::max(0,count()-1));selected.reset();anchor=-1;auditionEdit();speak(forward?"Redo. ":"Undo. ");
}
bool SliceEditor::keyPressed(const juce::KeyPress& k) {
    if(numeric)return true;
    const auto mods=k.getModifiers();const int code=k.getKeyCode();
    // getKeyCode, unlike getTextCharacter, remains a letter for Ctrl+C etc.
    const auto c=juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code));
    if(code==juce::KeyPress::escapeKey) {processor.stopSlicePreview();if(onClose)onClose();return true;}
    if(code==juce::KeyPress::F1Key) {
        announceToActiveScreenReader(*this,"Slice Edit. Tab cycles Boundaries, Globals and Sequencer. Up Down selects items. Left Right selects Start End or a step property; in Globals changes value. Page Up Down changes value; Shift uses larger increments. Enter types a value. Z toggles boundary Zero Crossing. Space toggles sample in Boundaries, sequence on other pages. Alt Space always plays whole sample. Ctrl Space auditions current slice or step. Shift Up Down selects a step range. Shift Space toggles selection. Ctrl Delete clears selection. Ctrl C V copies and pastes steps. Ctrl Z Y undo and redo. Backspace resets current parameter. Escape closes and stops preview.");return true;
    }
    if(mods.isCtrlDown()&&!mods.isAltDown()) {
        if(c=='z'){restore(false);return true;}if(c=='y'){restore(true);return true;}
        if(c=='c'){copy();return true;}if(c=='v'){paste();return true;}
        if(code==juce::KeyPress::deleteKey){selected.reset();anchor=-1;speak("Selection cleared. ");return true;}
        if(code==juce::KeyPress::spaceKey){preview(false,true);return true;}
        return true;
    }
    if(code==juce::KeyPress::spaceKey&&mods.isAltDown()){preview(true,false);return true;}
    if(mods.isAltDown())return true;
    if(code==juce::KeyPress::tabKey) {setPage(Page((int(page)+(mods.isShiftDown()?2:1))%3));return true;}
    if(code==juce::KeyPress::spaceKey) {
        if(mods.isShiftDown()&&page==Page::steps){selected.flip(size_t(item));anchor=item;speak();}
        else if(!mods.isShiftDown())preview(false,false);
        return true;
    }
    if(c=='z'&&page==Page::boundaries) {
        pushUndo();auto s=processor.getSliceState(slot);s.zeroCrossing=!s.zeroCrossing;commit(s);return true;
    }
    if(code==juce::KeyPress::returnKey){editNumber();return true;}
    if(code==juce::KeyPress::backspaceKey&&page!=Page::boundaries) {
        const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
        if(page==Page::steps&&property==int(SliceP::source)) {
            pushUndo();auto s=processor.getSliceState(slot);
            for(int i=0;i<count();++i)if(selected.any()?selected[size_t(i)]:i==item)s.steps[size_t(i)][SliceP::source]=i+1;
            commit(s);
        } else setValue(d.initial);
        return true;
    }
    if(code==juce::KeyPress::pageUpKey||code==juce::KeyPress::pageDownKey) {
        changeValue(code==juce::KeyPress::pageUpKey?1:-1,mods.isShiftDown());return true;
    }
    if(code==juce::KeyPress::leftKey||code==juce::KeyPress::rightKey) {
        const int direction=code==juce::KeyPress::rightKey?1:-1;
        if(page==Page::globals)changeValue(direction,mods.isShiftDown());
        else {
            int& target=page==Page::steps?property:boundarySide;
            const int next=juce::jlimit(0,page==Page::steps?16:1,target+direction);
            if(next!=target){target=next;speak();}
        }
        return true;
    }
    if(code==juce::KeyPress::upKey||code==juce::KeyPress::downKey||code==juce::KeyPress::homeKey||code==juce::KeyPress::endKey) {
        const int old=page==Page::globals?global:item;
        const int max=page==Page::globals?12:count()-1;
        const int target=code==juce::KeyPress::homeKey?0:code==juce::KeyPress::endKey?max:old+(code==juce::KeyPress::downKey?1:-1);
        if(page==Page::globals) {const int next=juce::jlimit(0,max,target);if(next!=global){global=next;speak();}}
        else moveItem(target,mods.isShiftDown());
        return true;
    }
    return true;
}

void SliceEditor::focusLost(FocusChangeType) {
    juce::MessageManager::callAsync([safe=juce::Component::SafePointer<SliceEditor>(this)] {
        if(safe!=nullptr&&!safe->hasKeyboardFocus(true))safe->processor.stopSlicePreview();
    });
}
void SliceEditor::focusOfChildComponentChanged(FocusChangeType cause) {
    if(!hasKeyboardFocus(true))focusLost(cause);
}
