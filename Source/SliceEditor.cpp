#include "SliceEditor.h"
#include "ScreenReaderAnnouncer.h"
using namespace lsampler;
namespace {
constexpr std::array<int,5> sliceStepWidths {1,5,10,15,20};
constexpr int slicePageStep = 40;
}
SliceEditor::SliceEditor(LSampler24AudioProcessor& p,int s,bool sequencer):processor(p),slot(s) {
    page=sequencer?Page::steps:Page::globals;
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
    // One speech path only.  Mixing titleChanged with the explicit NVDA
    // announcer caused consecutive overlapping messages on some Slice pages.
    announceToActiveScreenReader(*this,prefix+line);setDescription({});
}
void SliceEditor::announceEntry() {
    grabKeyboardFocus();if(auto* h=getAccessibilityHandler())h->grabFocus();
    speak("Slice Edit. Slot "+juce::String(slot+1)+". "+juce::String(count())+" slices. "
        +(page==Page::globals?"Global Slice Settings. ":page==Page::steps?"Slice Sequencer. ":"Boundaries. "));
}
void SliceEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff18202a));g.setColour(juce::Colours::white);g.setFont(22.0f);
    g.drawText("LSampler-24 - Slice Edit",20,14,getWidth()-40,34,juce::Justification::centredLeft);
    g.setFont(17.0f);
    const juce::String pageName=page==Page::boundaries?"Boundaries":page==Page::globals?"Global Slice Settings":"Slice Sequencer";
    g.drawText(pageName+" / Slot "+juce::String(slot+1),20,62,getWidth()-40,30,juce::Justification::centredLeft);
    g.drawFittedText(line,getLocalBounds().reduced(20).withTrimmedTop(100).withHeight(120),juce::Justification::centredLeft,4);
    g.setFont(14.0f);
    g.drawFittedText("1/2/3 or Tab: page | Arrows/Home/End: navigate (page 1: 8-row grid) | Alt+Up/Down: value | Alt+PgUp/PgDn: coarse value\nAlt+Left/Right: value step | Alt+Home/End: max/min | Enter: type value\nSpace: normal preview On/Off; when Off, slice/step navigation and edits auto-audition | F1: help | Escape: close",20,getHeight()-110,getWidth()-40,90,juce::Justification::centredLeft,4);
}
void SliceEditor::resized(){number.setBounds(20,getHeight()/2,getWidth()-40,40);}
void SliceEditor::pushUndo() {
    undo.push_back(processor.getSliceState(slot));if(undo.size()>64)undo.pop_front();redo.clear();
}
void SliceEditor::commit(SliceState& state) {processor.setSliceState(slot,state);auditionEdit();speak();}
void SliceEditor::auditionEdit() {
    const int kind=processor.getSlicePreviewKind();
    // Match the final Lua/JSFX F6 behaviour: a running Space preview has
    // priority.  When no full preview is running, navigation and edits give
    // immediate one-shot feedback from the current physical slice/step.
    if(kind==0||kind==2||kind==4)preview(false,true,false);
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
    const int width=sliceStepWidths[size_t(stepWidthIndex)];
    if(page==Page::boundaries) {
        const auto old=processor.getSliceState(slot);
        const int amount=width*(coarse?slicePageStep:1);
        if(processor.moveSliceBoundary(slot,item+boundarySide,direction,amount)) {
            undo.push_back(old);if(undo.size()>64)undo.pop_front();redo.clear();auditionEdit();speak();
        }
        return;
    }
    const auto s=processor.getSliceState(slot);
    const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
    const double value=page==Page::globals?s.globals[size_t(global)]:s.steps[size_t(item)].values[size_t(property)];
    // Fade In/Out have one sentinel position only: -1 = Inherit.  Do not make
    // the user traverse -0.99, -0.98 ... before reaching 0 ms.
    if(page==Page::steps&&(property==int(SliceP::fadeIn)||property==int(SliceP::fadeOut))) {
        if(direction>0&&value<0){setValue(0.0);return;}
        if(direction<0&&value<=0){setValue(-1.0);return;}
    }
    const double multiplier=double(width)*(coarse?double(slicePageStep):1.0);
    setValue(value+direction*d.step*multiplier);
}
void SliceEditor::changeStepWidth(int direction) {
    const int next=juce::jlimit(0,int(sliceStepWidths.size())-1,stepWidthIndex+direction);
    if(next==stepWidthIndex)return;
    stepWidthIndex=next;
    announceToActiveScreenReader(*this,"Step "+juce::String(sliceStepWidths[size_t(stepWidthIndex)]));
    setDescription({});
}
void SliceEditor::setValueBoundary(bool maximum) {
    if(page==Page::boundaries) {
        int start=0;double rate=0;const auto layout=processor.getSliceLayout(slot,start,rate);
        const int b=juce::jlimit(0,layout.count,item+boundarySide);
        if(b<=0||b>=layout.count)return; // fixed Sample Start / End edges
        const int current=layout.boundaries[size_t(b)];
        const int target=maximum?layout.boundaries[size_t(b+1)]-1:layout.boundaries[size_t(b-1)]+1;
        const int delta=target-current;
        if(delta==0)return;
        const auto old=processor.getSliceState(slot);
        if(processor.moveSliceBoundary(slot,b,delta>0?1:-1,std::abs(delta))) {
            undo.push_back(old);if(undo.size()>64)undo.pop_front();redo.clear();auditionEdit();speak();
        }
        return;
    }
    const auto& d=page==Page::globals?sliceGlobals[size_t(global)]:sliceProperties[size_t(property)];
    setValue(maximum?d.max:d.min);
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
    item=target;
    if(page==Page::steps) {
        // Navigation only moves the preview head.  Never retrigger a running
        // Space preview: the newly selected step becomes the start point only
        // the next time preview is started after being stopped.
        lastPreviewItem=item;
        const int kind=processor.getSlicePreviewKind();
        if(kind==0||kind==2||kind==4) auditionEdit();
    } else auditionEdit();
    speak();
}
void SliceEditor::preview(bool whole,bool slice,bool toggle) {
    // Page 3 keeps the normal Slice Mode semantics, but remembers the current
    // step as the timeline start for the next Space preview.  Kind 5 is the
    // normal MIDI-note Slice path with an explicit starting timeline position.
    const int kind=whole?(page==Page::steps?5:1):slice?(page==Page::boundaries?2:4):page==Page::boundaries?1:3;
    const bool stopping=toggle&&processor.getSlicePreviewKind()==kind&&(kind==1||kind==3||kind==5||lastPreviewItem==item);
    processor.requestSlicePreview(slot,kind,item,toggle);lastPreviewItem=item;
    if(toggle) {
        const juce::String name=(kind==1||kind==5)?"Sample preview":kind==3?"Sequence preview":kind==2?"Slice preview":"Step preview";
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
        announceToActiveScreenReader(*this,"Slice Edit. 1, 2 and 3 jump directly to Global Slice Settings, Slice Sequencer and Boundaries. Tab also cycles the pages. Plain arrows, Home and End navigate. Global Slice Settings uses an 8-row grid: Up/Down moves within a column and Left/Right changes column. On parameter pages a letter, number or punctuation character searches forward by parameter initial; Shift plus the same character searches backward. Alt Up and Alt Down change the current value. Alt Page Up and Alt Page Down change it coarsely. Alt Left and Alt Right select the value step. Alt Home sets maximum and Alt End sets minimum. Enter types a value. Z toggles boundary Zero Crossing. Space toggles the normal slot preview. While Space preview is off, moving between slices or steps and editing them automatically auditions the current slice or programmed step with its current parameters. Shift Up Down selects a step range. Shift Space toggles selection. Ctrl Delete clears selection. Ctrl C V copies and pastes steps. Ctrl Z Y undo and redo. Backspace resets current parameter. Escape closes and stops preview.");return true;
    }
    if(!mods.isCtrlDown()&&!mods.isAltDown()&&!mods.isShiftDown()&&!mods.isCommandDown()) {
        if(c=='1'){setPage(Page::globals);return true;}
        if(c=='2'){setPage(Page::steps);return true;}
        if(c=='3'){setPage(Page::boundaries);return true;}
    }
    // Parameter type-ahead on the parameter pages. Plain character cycles
    // forward; Shift+character cycles backward. Global names use "Slice" only
    // as context, so search the meaningful parameter name after that prefix.
    if(!mods.isCtrlDown()&&!mods.isAltDown()&&!mods.isCommandDown()
       && (page==Page::globals||page==Page::steps)) {
        // Use the actual typed character, not the virtual key code.  Arrow,
        // Home/End and Page keys have numeric key codes that can fall inside
        // the printable ASCII range, which previously caused type-ahead to
        // swallow normal exploratory navigation on pages 2 and 3.
        const auto typed=k.getTextCharacter();
        const auto initial=juce::CharacterFunctions::toLowerCase(typed);
        if(typed>=33&&typed!=127) {
            const bool backwards=mods.isShiftDown();
            const int direction=backwards?-1:1;
            const int total=page==Page::globals?int(sliceGlobals.size()):int(sliceProperties.size());
            int& cursor=page==Page::globals?global:property;
            for(int distance=1;distance<=total;++distance) {
                int next=(cursor+direction*distance)%total;
                if(next<0)next+=total;
                juce::String name=page==Page::globals?juce::String(sliceGlobals[size_t(next)].name)
                                                     :juce::String(sliceProperties[size_t(next)].name);
                if(page==Page::globals&&name.startsWithIgnoreCase("Slice "))name=name.substring(6);
                if(name.isNotEmpty()&&juce::CharacterFunctions::toLowerCase(name[0])==initial) {
                    cursor=next;speak();break;
                }
            }
            return true;
        }
    }
    if(mods.isCtrlDown()&&!mods.isAltDown()) {
        if(c=='z'){restore(false);return true;}if(c=='y'){restore(true);return true;}
        if(c=='c'){copy();return true;}if(c=='v'){paste();return true;}
        if(code==juce::KeyPress::deleteKey){selected.reset();anchor=-1;speak("Selection cleared. ");return true;}
        if(code==juce::KeyPress::spaceKey){preview(false,true);return true;}
        return true;
    }
    if(mods.isAltDown()&&!mods.isCtrlDown()&&!mods.isCommandDown()) {
        if(code==juce::KeyPress::spaceKey){preview(true,false);return true;}
        if(code==juce::KeyPress::upKey){changeValue(1,false);return true;}
        if(code==juce::KeyPress::downKey){changeValue(-1,false);return true;}
        if(code==juce::KeyPress::pageUpKey){changeValue(1,true);return true;}
        if(code==juce::KeyPress::pageDownKey){changeValue(-1,true);return true;}
        if(code==juce::KeyPress::leftKey){changeStepWidth(-1);return true;}
        if(code==juce::KeyPress::rightKey){changeStepWidth(1);return true;}
        if(code==juce::KeyPress::homeKey){setValueBoundary(true);return true;}
        if(code==juce::KeyPress::endKey){setValueBoundary(false);return true;}
        return true;
    }
    if(code==juce::KeyPress::tabKey) {setPage(Page((int(page)+(mods.isShiftDown()?2:1))%3));return true;}
    if(code==juce::KeyPress::spaceKey) {
        if(mods.isShiftDown()&&page==Page::steps){selected.flip(size_t(item));anchor=item;speak();}
        else if(!mods.isShiftDown())preview(true,false); // exactly the normal MIDI-note Slice path
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
        // Global Slice Settings (page 1) is a compact 8-row grid.  Page Up/Down changes column while
        // preserving the row when that cell exists.
        const int direction=code==juce::KeyPress::pageDownKey?1:-1;
        if(page==Page::globals) {
            constexpr int rows=8;
            const int total=int(sliceGlobals.size());
            const int next=global+direction*rows;
            if(next>=0&&next<total){global=next;speak();}
        } else moveItem(item+direction*8,mods.isShiftDown());
        return true;
    }
    if(code==juce::KeyPress::leftKey||code==juce::KeyPress::rightKey) {
        const int direction=code==juce::KeyPress::rightKey?1:-1;
        if(page==Page::globals) {
            // 8 rows per column, same exploratory grid logic as the main grid.
            constexpr int rows=8;
            const int total=int(sliceGlobals.size());
            const int next=global+direction*rows;
            if(next>=0&&next<total){global=next;speak();}
        } else {
            int& target=page==Page::steps?property:boundarySide;
            const int next=juce::jlimit(0,page==Page::steps?int(sliceProperties.size())-1:1,target+direction);
            if(next!=target){target=next;speak();}
        }
        return true;
    }
    if(code==juce::KeyPress::upKey||code==juce::KeyPress::downKey||code==juce::KeyPress::homeKey||code==juce::KeyPress::endKey) {
        if(page==Page::globals) {
            constexpr int rows=8;
            const int total=int(sliceGlobals.size());
            const int column=global/rows;
            const int columnStart=column*rows;
            const int columnEnd=std::min(total-1,columnStart+rows-1);
            int next=global;
            if(code==juce::KeyPress::homeKey) next=columnStart;
            else if(code==juce::KeyPress::endKey) next=columnEnd;
            else next=global+(code==juce::KeyPress::downKey?1:-1);
            if(next>=columnStart&&next<=columnEnd&&next!=global){global=next;speak();}
        } else {
            const int target=code==juce::KeyPress::homeKey?0:
                             code==juce::KeyPress::endKey?count()-1:
                             item+(code==juce::KeyPress::downKey?1:-1);
            moveItem(target,mods.isShiftDown());
        }
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
