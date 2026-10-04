#pragma once
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <bitset>

namespace rackgui {
inline const juce::Colour chassis { 0xff20252a }, panel { 0xff2b3239 }, screen { 0xff0c1713 },
    green { 0xffa3d6ae }, amber { 0xffefb46e }, text { 0xffe4e9e9 }, muted { 0xffa7b4b9 };
inline void caption(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) {
    g.setColour(amber); g.setFont(15.0f); g.drawText(title, r, juce::Justification::centredLeft, true);
}
inline void frame(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) {
    g.setColour(panel); g.fillRoundedRectangle(r.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff49535b)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 5.0f, 1.0f);
    caption(g, r.reduced(12).withHeight(22), title);
}
class Theme : public juce::LookAndFeel_V4 {
public:
    Theme() { apply(*this); }
    static void apply(juce::LookAndFeel& lf) {
        lf.setColour(juce::ResizableWindow::backgroundColourId, chassis);
        lf.setColour(juce::Label::textColourId, text);
        lf.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff38444e));
        lf.setColour(juce::TextButton::textColourOffId, text);
        lf.setColour(juce::ComboBox::backgroundColourId, screen);
        lf.setColour(juce::ComboBox::textColourId, green);
        lf.setColour(juce::ComboBox::outlineColourId, muted);
        lf.setColour(juce::PopupMenu::backgroundColourId, chassis);
        lf.setColour(juce::PopupMenu::textColourId, text);
        lf.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff475d52));
        lf.setColour(juce::Slider::trackColourId, green);
        lf.setColour(juce::Slider::thumbColourId, amber);
        lf.setColour(juce::Slider::textBoxTextColourId, text);
        lf.setColour(juce::Slider::textBoxBackgroundColourId, screen);
        lf.setColour(juce::Slider::textBoxOutlineColourId, muted);
        lf.setColour(juce::TextEditor::backgroundColourId, screen);
        lf.setColour(juce::TextEditor::textColourId, text);
        lf.setColour(juce::TextEditor::focusedOutlineColourId, amber);
    }
};
// All new surfaces are observational: they never intercept mouse/keyboard,
// enter the accessibility tree, or publish accessibility events.
class Display : public juce::Component {
public:
    Display() { setAccessible(false); setWantsKeyboardFocus(false); setInterceptsMouseClicks(false, false); }
};
class Waveform final : public Display {
public:
    void update(const LSampler24AudioProcessor::VisualSlotState& next, int currentSlice = -1, int side = -1, int loop = 0) {
        state = next; highlight = currentSlice; boundarySide = side; selectedLoop = loop;
        if (owner != next.sample) {
            owner = next.sample; peaks.clear();
            if (owner && owner->audio.getNumSamples() > 0) {
                const auto& audio = owner->audio;
                const int n = audio.getNumSamples(), bins = std::min(n, 1024);
                peaks.resize(size_t(bins));
                for (int b = 0; b < bins; ++b) {
                    float lo = 0, hi = 0;
                    const int first = int(int64_t(b) * n / bins), last = int(int64_t(b + 1) * n / bins);
                    for (int ch = 0; ch < audio.getNumChannels(); ++ch) {
                        const auto* data = audio.getReadPointer(ch);
                        for (int i = first; i < last; ++i) { lo = std::min(lo, data[i]); hi = std::max(hi, data[i]); }
                    }
                    peaks[size_t(b)] = {lo, hi};
                }
            }
        }
        repaint();
    }
    void paint(juce::Graphics& g) override {
        frame(g, getLocalBounds(), "SAMPLE / WAVEFORM");
        auto r = getLocalBounds().reduced(14); r.removeFromTop(30);
        auto footer = r.removeFromBottom(64); auto header = r.removeFromTop(24);
        g.setColour(green); g.setFont(15.0f);
        const auto name = state.name.isNotEmpty() ? state.name : owner ? owner->sourceFile.getFileNameWithoutExtension() : juce::String("Empty slot");
        g.drawText(name, header, juce::Justification::centredLeft, true);
        g.setColour(screen); g.fillRect(r);
        if (!owner || peaks.empty()) {
            g.setColour(muted); g.drawText("No sample loaded", r, juce::Justification::centred); return;
        }
        const auto& p = state.parameters;
        const int total = owner->audio.getNumSamples();
        auto x = [&](double percent) { return float(r.getX()) + float(juce::jlimit(0.0, 100.0, percent) * r.getWidth() / 100.0); };
        auto framePercent = [&](int f) { return 100.0 * f / std::max(1, total); };
        const float startX = x(p[lsampler::P::sample_start]), endX = x(p[lsampler::P::sample_end]);
        g.setColour(juce::Colour(0xff21392b));
        g.fillRect(juce::Rectangle<float>(startX, float(r.getY()), std::max(0.0f, endX-startX), float(r.getHeight())));
        g.setColour(green.withAlpha(0.12f));
        for (int i=1;i<4;++i) g.drawVerticalLine(r.getX()+r.getWidth()*i/4, float(r.getY()), float(r.getBottom()));
        g.drawHorizontalLine(r.getCentreY(), float(r.getX()), float(r.getRight()));
        g.setColour(green);
        const float mid = float(r.getCentreY()), scale = float(r.getHeight()) * 0.40f;
        for (int pixel = 0; pixel < r.getWidth(); ++pixel) {
            const int first = int(int64_t(pixel)*int(peaks.size())/r.getWidth());
            const int last = std::min(int(peaks.size()), std::max(first+1, int(int64_t(pixel+1)*int(peaks.size())/r.getWidth())));
            float lo=0,hi=0;
            for(int b=first;b<last;++b) {lo=std::min(lo,peaks[size_t(b)].first);hi=std::max(hi,peaks[size_t(b)].second);}
            g.drawVerticalLine(r.getX()+pixel, mid-juce::jlimit(-1.0f,1.0f,hi)*scale, mid-juce::jlimit(-1.0f,1.0f,lo)*scale);
        }
        struct MarkerLabel { juce::Rectangle<int> bounds; juce::Colour colour; juce::String text; };
        std::vector<MarkerLabel> markerLabels;
        auto marker = [&](double pos, juce::Colour colour, const juce::String& label, int lane=0) {
            const float px=x(pos);
            g.setColour(colour); g.drawLine(px,float(r.getY()),px,float(r.getBottom()),1.4f);
            if(label.isNotEmpty()) {
                const int width=label.length()>5?94:50;
                const int left=juce::jlimit(r.getX(),std::max(r.getX(),r.getRight()-width),int(px)+3);
                const int top=lane==0?r.getY()+2:lane==1?r.getBottom()-20:lane==2?r.getY()+22:r.getBottom()-40;
                markerLabels.push_back({juce::Rectangle<int>(left,top,width,18),colour,label});
            }
        };
        const auto layout = lsampler::prepareSliceAudio(state.slice, std::max(0, state.effectiveEnd-state.effectiveStart));
        if (state.slice[lsampler::SliceG::mode]!=0 || highlight>=0) {
            for(int i=0;i<=layout.count;++i) {
                const bool current = highlight>=0 && (i==highlight || i==highlight+1);
                marker(framePercent(state.effectiveStart+layout.boundaries[size_t(i)]),current?amber:juce::Colour(0xff6b9fba),{},true);
            }
            if(highlight>=0 && boundarySide>=0 && highlight+boundarySide<=layout.count)
                marker(framePercent(state.effectiveStart+layout.boundaries[size_t(highlight+boundarySide)]),amber,boundarySide==0?"Edit Start":"Edit End",3);
        }
        const auto& loop=state.parameters.loops[size_t(juce::jlimit(0,lsampler::loopCount-1,selectedLoop))];
        // Loop coordinates are percentages within the threshold-derived effective window.
        if(p[lsampler::P::global_one_shot]==0 && loop[1]>loop[0]) {
            const double span=framePercent(state.effectiveEnd-state.effectiveStart);
            marker(framePercent(state.effectiveStart)+span*loop[0]*.01,juce::Colour(0xff86adc7),"Loop S",3);
            marker(framePercent(state.effectiveStart)+span*loop[1]*.01,juce::Colour(0xff86adc7),"Loop E",3);
        }
        const double effectiveStart=framePercent(state.effectiveStart), effectiveEnd=framePercent(state.effectiveEnd);
        const bool shiftedStart=std::abs(effectiveStart-p[lsampler::P::sample_start])>100.0/std::max(1,total);
        const bool shiftedEnd=std::abs(effectiveEnd-p[lsampler::P::sample_end])>100.0/std::max(1,total);
        if(shiftedStart)marker(effectiveStart,green,"Eff. Start",2);
        if(shiftedEnd)marker(effectiveEnd,green,"Eff. End",2);
        marker(p[lsampler::P::sample_start],amber,"Start");marker(p[lsampler::P::sample_end],amber,"End");
        marker(p[lsampler::P::sample_play_start],text,"Play",true);
        // Paint labels last so waveform peaks and later marker lines cannot cross the text.
        for (const auto& label : markerLabels) {
            g.setColour(screen);
            g.fillRect(label.bounds.expanded(2,0).getIntersection(r));
            g.setColour(label.colour);g.setFont(14.0f);
            g.drawText(label.text,label.bounds,juce::Justification::centredLeft);
        }
        g.setColour(green);g.setFont(14.0f);
        g.drawText("Start "+juce::String(p[lsampler::P::sample_start],2)+"%    End "+juce::String(p[lsampler::P::sample_end],2)+"%    Play "+juce::String(p[lsampler::P::sample_play_start],2)+"%",footer.removeFromTop(22),juce::Justification::centredLeft,true);
        g.setColour(muted);
        g.drawText(juce::String(owner->sourceSampleRate,0)+" Hz  /  "+juce::String(owner->audio.getNumChannels())+" ch  /  "+juce::String(total / std::max(1.0,owner->sourceSampleRate),2)+" s",footer.removeFromTop(20),juce::Justification::centredLeft,true);
        g.drawText("Effective Start "+juce::String(state.effectiveStart)+" / End "+juce::String(state.effectiveEnd)+" frames",footer,juce::Justification::centredLeft,true);
    }
private:
    LSampler24AudioProcessor::VisualSlotState state;
    std::shared_ptr<const SharedSample> owner;
    std::vector<std::pair<float,float>> peaks;
    int highlight=-1,boundarySide=-1,selectedLoop=0;
};
class SlotOverview final : public Display {
public:
    std::array<juce::String,24> names;
    std::array<bool,24> loaded {};
    int current=0;
    void paint(juce::Graphics& g) override {
        frame(g,getLocalBounds(),"SLOTS / 01-24");
        const auto r=getLocalBounds().reduced(12).withTrimmedTop(30);
        const int w=r.getWidth()/3,h=r.getHeight()/8;
        for(int col=0;col<3;++col)for(int row=0;row<8;++row) {
            int i=col*8+row;auto cell=juce::Rectangle<int>(r.getX()+col*w,r.getY()+row*h,w-4,h-4);
            g.setColour(i==current?juce::Colour(0xff4b4034):screen);g.fillRoundedRectangle(cell.toFloat(),3);
            g.setColour(i==current?amber:muted);g.drawRoundedRectangle(cell.toFloat().reduced(.5f),3,1);
            g.setColour(loaded[size_t(i)]?green:muted.withAlpha(.4f));g.fillEllipse(float(cell.getRight()-10),float(cell.getY()+6),4,4);
            g.setColour(i==current?amber:text);g.setFont(13.0f);
            g.drawText(juce::String(i+1).paddedLeft('0',2),cell.reduced(6).withHeight(17),juce::Justification::centredLeft);
            g.setColour(muted);g.setFont(13.0f);g.drawText(names[size_t(i)],cell.reduced(6).withTrimmedTop(18),juce::Justification::centredLeft,true);
        }
    }
};
class CurrentParameter final : public Display {
public:
    juce::String category,name,value;
    bool compact=false;
    void paint(juce::Graphics& g) override {
        frame(g,getLocalBounds(),"CURRENT EDIT / "+category);
        auto r=getLocalBounds().reduced(14).withTrimmedTop(compact?26:34);
        if(compact) {
            g.setColour(text);g.setFont(18.0f);g.drawText(name,r.removeFromTop(22),juce::Justification::centredLeft,true);
            g.setColour(green);g.setFont(15.0f);g.drawText(value,r,juce::Justification::centredLeft,true);
            return;
        }
        g.setColour(text);g.setFont(17.0f);g.drawFittedText(name,r.removeFromTop(40),juce::Justification::centredLeft,2);
        g.setColour(green);g.setFont(24.0f);g.drawText(value,r.removeFromTop(30),juce::Justification::centredLeft,true);
    }
};
class SliceOverview final : public Display {
public:
    lsampler::SliceState state;
    int page=-1,current=-1,property=0,global=0;
    std::bitset<128> selected;
    void paint(juce::Graphics& g) override {
        frame(g,getLocalBounds(),"SLICE / Alt+E");
        auto r=getLocalBounds().reduced(14).withTrimmedTop(32);
        auto tabs=r.removeFromTop(26);const int w=tabs.getWidth()/3;
        const char* titles[]={"1 Global Slice Settings","2 Slice Sequencer","3 Boundaries"};
        for(int i=0;i<3;++i) {
            g.setColour(i==page?amber:muted);g.setFont(page<0?14.0f:15.0f);
            g.drawText(titles[i],tabs.removeFromLeft(w),juce::Justification::centredLeft,true);
        }
        r.removeFromTop(8);
        const int n=state.division();
        if(page<0) {
            g.setColour(green);g.setFont(15.0f);
            g.drawText("Mode: "+lsampler::sliceValueText(lsampler::sliceGlobals[0],state.globals[0])+"   /   "+juce::String(n)+" slices",r,juce::Justification::centredLeft,true);
            return;
        }
        if(page==0) {
            // Match the existing eight-row keyboard grid, including the short second column.
            constexpr int rows=8;
            const int h=std::min(24,r.getHeight()/rows),w2=r.getWidth()/2;
            for(int i=0;i<int(lsampler::sliceGlobals.size());++i) {
                auto cell=juce::Rectangle<int>(r.getX()+(i/rows)*w2,r.getY()+(i%rows)*h,w2-12,h-1);
                if(i==global){g.setColour(screen);g.fillRect(cell);g.setColour(amber);g.fillRect(cell.removeFromLeft(3));}
                g.setColour(i==global?amber:text);g.setFont(15.0f);
                g.drawText(juce::String(lsampler::sliceGlobals[size_t(i)].name)+": "+lsampler::sliceValueText(lsampler::sliceGlobals[size_t(i)],state.globals[size_t(i)]),cell.reduced(7,0),juce::Justification::centredLeft,true);
            }
            return;
        }
        if(page==2) {
            g.setColour(green);g.setFont(16.0f);
            g.drawText("Mode: "+lsampler::sliceValueText(lsampler::sliceGlobals[0],state.globals[0])+"   /   "+juce::String(n)+" slices",r.removeFromTop(30),juce::Justification::centredLeft,true);
            g.setColour(text);g.setFont(15.0f);
            g.drawText("Edit Start / Edit End marks the boundary being edited.",r.removeFromTop(28),juce::Justification::centredLeft,true);
            g.setColour(muted);
            g.drawText("Amber: current slice. Blue: other slice boundaries. Green: effective sample window.",r.removeFromTop(28),juce::Justification::centredLeft,true);
            return;
        }
        // Follow the existing edit cursor in groups of 16; no new navigation or playback cursor.
        const int first=std::max(0,current)/16*16,last=std::min(n,first+16),cols=8;
        const int rows=std::max(1,(last-first+cols-1)/cols);
        auto footer=r.removeFromBottom(30);
        const int w2=r.getWidth()/cols,h=std::min(76,r.getHeight()/rows);
        for(int i=first;i<last;++i) {
            auto cell=juce::Rectangle<int>(r.getX()+((i-first)%cols)*w2,r.getY()+((i-first)/cols)*h,w2-5,h-6);
            const auto& step=state.steps[size_t(i)];g.setColour(screen);g.fillRect(cell);
            g.setColour(i==current?amber:selected[size_t(i)]?green:muted);g.drawRect(cell,i==current?2:1);
            auto content=cell.reduced(6);
            g.setFont(15.0f);g.drawText(juce::String(i+1)+(step[lsampler::SliceP::mute]!=0?" M":""),content.removeFromTop(18),juce::Justification::centredLeft);
            g.setColour(text);g.setFont(14.0f);
            g.drawText("S"+juce::String(int(step[lsampler::SliceP::source]))+" x"+juce::String(int(step[lsampler::SliceP::repeat])),content.removeFromTop(20),juce::Justification::centredLeft,true);
            g.setColour(muted);g.setFont(13.0f);
            g.drawText(step[lsampler::SliceP::repeatType]==0?"Extend":"Consume",content,juce::Justification::centredLeft,true);
        }
        g.setColour(muted);g.setFont(14.0f);
        g.drawText("Steps "+juce::String(first+1)+"-"+juce::String(last)+" / "+juce::String(n)+"   |   S = Source Slice, x = Repeat   |   "+lsampler::sliceProperties[size_t(property)].name,footer,juce::Justification::centredLeft,true);
    }
};
class MasterOutput final : public Display {
public:
    std::array<double,5> values {};
    std::array<float,2> peaks {};
    void paint(juce::Graphics& g) override {
        frame(g,getLocalBounds(),"OUTPUT / MASTER");
        auto r=getLocalBounds().reduced(14).withTrimmedTop(32);
        auto meters=r.removeFromRight(150);r.removeFromRight(12);
        g.setFont(14.0f);
        for(int ch=0;ch<2;++ch) {
            auto row=meters.removeFromTop(23);g.setColour(muted);
            g.drawText(ch==0?"L":"R",row.removeFromLeft(16),juce::Justification::centredLeft);
            auto db=row.removeFromRight(48);const float level=juce::Decibels::gainToDecibels(peaks[size_t(ch)],-60.0f);
            g.drawText(juce::String(level,1),db,juce::Justification::centredRight);
            row.reduce(0,6);g.setColour(screen);g.fillRect(row);g.setColour(level>=0?amber:green);
            g.fillRect(row.withWidth(int(row.getWidth()*juce::jlimit(0.0f,1.0f,(level+60)/60))));
        }
        g.setColour(muted);g.setFont(13.0f);g.drawText("Main 1/2 - dBFS",meters,juce::Justification::centredLeft,true);
        g.setColour(green);g.setFont(15.0f);
        g.drawText("Master "+juce::String(values[0],1)+" dB  /  Stage "+(values[1]!=0?"LR-608":"Off"),r.removeFromTop(23),juce::Justification::centredLeft,true);
        g.setColour(text);g.setFont(14.0f);
        g.drawText("Glue "+juce::String(values[2],1)+"%  /  Soft Drive "+juce::String(values[3],1)+"%",r.removeFromTop(23),juce::Justification::centredLeft,true);
        g.setColour(muted);
        g.drawText(values[1]!=0?"Protection On / Ceiling "+juce::String(values[4],3):juce::String("Legacy protection / Ceiling 0.980"),r,juce::Justification::centredLeft,true);
    }
};
} // namespace rackgui
