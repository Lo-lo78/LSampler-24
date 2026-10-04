#include "SliceState.h"
namespace lsampler {
SliceState::SliceState() {
    for(size_t i=0;i<globals.size();++i)globals[i]=sliceGlobals[i].initial;
    for(int i=0;i<maxSlices;++i)steps[size_t(i)][SliceP::source]=i+1;
    setDivision(5);
}
void SliceState::setDivision(int index) noexcept {
    (*this)[SliceG::division]=std::clamp(index,0,13);
    const int n=division();
    for(int i=0;i<=n;++i)boundaries[size_t(i)]=double(i)/n;
    // Keep all 128 step records, including temporarily out-of-range references.
}
void SliceState::sanitise() noexcept {
    for(size_t i=0;i<globals.size();++i)globals[i]=sliceSanitise(sliceGlobals[i],globals[i]);
    const int n=division();boundaries[0]=0;boundaries[size_t(n)]=1;
    constexpr double gap=1.0e-9;
    for(int i=1;i<n;++i) {
        double& b=boundaries[size_t(i)];if(!std::isfinite(b))b=double(i)/n;
        b=std::clamp(b,boundaries[size_t(i-1)]+gap,1-(n-i)*gap);
    }
    for(auto& step:steps)for(size_t i=0;i<step.values.size();++i)
        step.values[i]=sliceSanitise(sliceProperties[i],step.values[i]);
}
juce::ValueTree SliceState::toTree() const {
    juce::ValueTree tree("Slice");tree.setProperty("version",1,nullptr);
    tree.setProperty("zeroCrossing",zeroCrossing,nullptr);
    for(size_t i=0;i<globals.size();++i)tree.setProperty(sliceGlobals[i].key,globals[i],nullptr);
    juce::ValueTree points("Boundaries");
    for(int i=0;i<=division();++i) {
        juce::ValueTree point("Boundary");point.setProperty("index",i,nullptr);
        point.setProperty("position",boundaries[size_t(i)],nullptr);points.addChild(point,-1,nullptr);
    }
    tree.addChild(points,-1,nullptr);
    juce::ValueTree sequence("Steps");
    for(int i=0;i<maxSlices;++i) {
        juce::ValueTree step("Step");step.setProperty("index",i,nullptr);
        for(size_t k=0;k<sliceProperties.size();++k)step.setProperty(sliceProperties[k].key,steps[size_t(i)].values[k],nullptr);
        sequence.addChild(step,-1,nullptr);
    }
    tree.addChild(sequence,-1,nullptr);return tree;
}
SliceState SliceState::fromTree(const juce::ValueTree& tree) {
    SliceState s;if(!tree.isValid())return s;
    for(size_t i=0;i<s.globals.size();++i)s.globals[i]=sliceSanitise(sliceGlobals[i],double(tree.getProperty(sliceGlobals[i].key,s.globals[i])));
    s.setDivision(int(s[SliceG::division]));s.zeroCrossing=bool(tree.getProperty("zeroCrossing",false));
    const auto points=tree.getChildWithName("Boundaries");
    for(int i=0;i<points.getNumChildren();++i) {
        auto p=points.getChild(i);int k=int(p.getProperty("index",-1));
        if(k>=0&&k<=s.division())s.boundaries[size_t(k)]=double(p.getProperty("position",s.boundaries[size_t(k)]));
    }
    const auto steps=tree.getChildWithName("Steps");
    for(int i=0;i<steps.getNumChildren();++i) {
        auto p=steps.getChild(i);int k=int(p.getProperty("index",-1));if(k<0||k>=maxSlices)continue;
        for(size_t j=0;j<sliceProperties.size();++j)s.steps[size_t(k)].values[j]=double(p.getProperty(sliceProperties[j].key,s.steps[size_t(k)].values[j]));
    }
    s.sanitise();return s;
}
SliceAudioState prepareSliceAudio(const SliceState& state,int length) {
    SliceAudioState a;a.state=state;a.state.sanitise();
    a.count=std::min(std::max(0,length),a.state.division());
    if(a.count==0)return a;
    a.boundaries[0]=0;a.boundaries[size_t(a.count)]=length;
    for(int i=1;i<a.count;++i) {
        // A buffer shorter than the requested division cannot have empty slices.
        const double p=a.count==a.state.division()?a.state.boundaries[size_t(i)]:double(i)/a.count;
        a.boundaries[size_t(i)]=std::clamp(int(std::floor(p*length)),a.boundaries[size_t(i-1)]+1,length-(a.count-i));
    }
    for(int i=0;i<a.count;++i) {
        const auto& p=a.state.steps[size_t(i)];
        a.gains[size_t(i)]=p[SliceP::mute]!=0?0:std::pow(10.0,p[SliceP::volume]/20);
        a.cycleUnits+=p[SliceP::fit]!=0?1:p[SliceP::repeat];
    }
    return a;
}
bool sampleCrossesZero(const juce::AudioBuffer<float>& a,int at,int first,int last) noexcept {
    if(at<=first||at>=last)return false;
    for(int ch=0;ch<a.getNumChannels();++ch) {
        const float x=a.getSample(ch,at-1),y=a.getSample(ch,at);
        if((x<=0&&y>=0)||(x>=0&&y<=0))return true;
    }
    return false;
}
juce::String sliceValueText(const SliceDescriptor& d,double x) {
    if(*d.labels) {
        const auto labels=juce::StringArray::fromTokens(d.labels,"|","");
        return labels[juce::jlimit(0,labels.size()-1,int(std::round(x-d.min)))];
    }
    const juce::String key(d.key);
    if(key=="midiNote")return x<0?"Off":juce::String(int(x))+" "+juce::MidiMessage::getMidiNoteName(int(x),true,true,4);
    if(key=="output")return x==0?"Slot output":juce::String(int(x)*2+1)+"/"+juce::String(int(x)*2+2);
    if(key=="fadeIn"||key=="fadeOut")return x<0?"Inherit":juce::String(x,2)+" ms";
    return juce::String(x,d.step==1?0:2);
}
}
