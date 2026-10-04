#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace lsampler {
constexpr int maxSlices = 128;
inline constexpr std::array<int,14> sliceDivisions {1,2,3,4,6,8,12,16,24,32,48,64,96,128};
enum class SliceG { mode, division, pitchTime, random, shuffle, shufflePitch, fadeIn, fadeOut,
                    panMode, panDepth, pitchMode, pitchDepth, midiMap, count };
enum class SliceP { source, repeat, fit, pitch, repeatPitch, repeatPan, repeatLP, repeatHP,
                    volume, mute, midiNote, pan, output, reverse, downsample, fadeIn, fadeOut, count };
struct SliceDescriptor { const char* key; const char* name; double min,max,step,initial; const char* labels; };
inline constexpr std::array<SliceDescriptor,13> sliceGlobals {{
    {"mode","Slice Mode",0,7,1,0,"Off|Reverse|Pendulum|Random|Random No Repeat|Center Out|Edges In|Sequencer"},
    {"division","Slice Division",0,13,1,5,"1|2|3|4|6|8|12|16|24|32|48|64|96|128"},
    {"pitchTime","Slice Pitch Step Time",0,1,1,0,"Original Pitch Step|Pitch Resizes Step"},
    {"random","Slice Seq Random",0,2,1,0,"Off|Random|Random No Repeat"},
    {"shuffle","Slice Shuffle",0,100,.1,0,""}, {"shufflePitch","Slice Shuffle Pitch",0,1,1,0,"Off|On"},
    {"fadeIn","Slice Fade In",0,1000,.01,0,""}, {"fadeOut","Slice Fade Out",0,1000,.01,0,""},
    {"panMode","Slice Pan Mode",0,2,1,0,"Off|Follow Slice Mode|Follow Slice Mode Reverse"},
    {"panDepth","Slice Pan Depth",0,1,.01,1,""},
    {"pitchMode","Slice Pitch Mode",0,2,1,0,"Off|Follow Slice Mode|Follow Slice Mode Reverse"},
    {"pitchDepth","Slice Pitch Depth",0,48,.1,12,""}, {"midiMap","Slice MIDI Map",0,1,1,0,"Off|On"}
}};
inline constexpr std::array<SliceDescriptor,17> sliceProperties {{
    {"source","Source Slice",1,128,1,1,""}, {"repeat","Repeat",1,128,1,1,""},
    {"fit","Fit",0,1,1,0,"Lengthen|Fit"}, {"pitch","Pitch",-96,96,.01,0,""},
    {"repeatPitch","Repeat Pitch Depth",-100,100,.01,0,""}, {"repeatPan","Repeat Pan",-100,100,.01,0,""},
    {"repeatLP","Repeat LP",-96,96,.01,0,""}, {"repeatHP","Repeat HP",-96,96,.01,0,""},
    {"volume","Volume",-60,24,.1,0,""}, {"mute","Mute",0,1,1,0,"Off|On"},
    {"midiNote","MIDI Note",-1,127,1,-1,""}, {"pan","Pan",-100,100,.1,0,""},
    {"output","Output",0,24,1,0,""}, {"reverse","Reverse",-1,1,1,-1,"Inherit|Off|On"},
    {"downsample","Downsample",-1,10,1,-1,"Inherit|Off|32 kHz 16 bit|22.05 kHz 12 bit|22.05 kHz 8 bit|12 kHz 8 bit|12 kHz 4 bit|11.025 kHz 12 bit|11.025 kHz 8 bit|8 kHz 12 bit|8 kHz 8 bit|8 kHz 4 bit"},
    {"fadeIn","Fade In",-1,1000,.01,-1,""}, {"fadeOut","Fade Out",-1,1000,.01,-1,""}
}};
inline double sliceSanitise(const SliceDescriptor& d, double x) noexcept {
    if(!std::isfinite(x))x=d.initial;
    x=std::clamp(x,d.min,d.max);
    return d.step==1 ? std::round(x) : x;
}
struct SliceStep {
    std::array<double,17> values {};
    SliceStep() {for(size_t i=0;i<values.size();++i)values[i]=sliceProperties[i].initial;}
    double operator[](SliceP p) const noexcept {return values[size_t(p)];}
    double& operator[](SliceP p) noexcept {return values[size_t(p)];}
};
struct SliceState {
    std::array<double,13> globals {};
    // Shared, normalised boundary points in the active Start/End window, NOT
    // per-step regions. Resolve to absolute frames only at snapshot publication.
    std::array<double,129> boundaries {};
    std::array<SliceStep,128> steps {};
    bool zeroCrossing=false;
    SliceState();
    double operator[](SliceG p) const noexcept {return globals[size_t(p)];}
    double& operator[](SliceG p) noexcept {return globals[size_t(p)];}
    int division() const noexcept {return sliceDivisions[size_t(int((*this)[SliceG::division]))];}
    void setDivision(int index) noexcept;
    void sanitise() noexcept;
    juce::ValueTree toTree() const;
    static SliceState fromTree(const juce::ValueTree&);
};
// Prepared data contains no heap allocation or ownership. Audio thread reads a
// published snapshot; UI and persistence never mutate an audio reader's state.
struct SliceAudioState {
    SliceState state;
    int count=0;
    std::array<int,129> boundaries {}; // relative to SlotAudioState.start
    std::array<double,128> gains {};
    double cycleUnits=0;
};
SliceAudioState prepareSliceAudio(const SliceState&,int length);
// Shared crossing criterion with the existing Sample/Loop Zero Crossing action.
bool sampleCrossesZero(const juce::AudioBuffer<float>&,int frame,int first,int last) noexcept;
juce::String sliceValueText(const SliceDescriptor&,double);
} // namespace lsampler
