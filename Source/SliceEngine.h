#pragma once
#include "SliceState.h"
namespace lsampler {
// Per-voice sample clock. Never allocates, locks, or accesses the UI.
// Requests: -1 normal mode, -2 whole-sample bypass, -3 force sequencer,
// -3000-index preserves Slice Mode and begins its timeline at that step;
// -2000-index force sequencer beginning at that step; >=0 audition/MIDI step;
// -1000-index auditions a physical slice directly.
struct SlicePlayback {
    bool active=false, single=false, physical=false, pad=false, configured=false, finished=false;
    int request=-1, timeline=0, step=0, source=0, repeat=0, lastSource=-1, mode=0;
    int repeats=1, timelineAdvance=1, output=0, downsample=-1,downsampleHold=1;
    double downsampleScale=1;
    double elapsed=0, units=0, eventUnits=1, consumedUnits=0, pitch=1, gain=1;
    double pan=0, repeatPan=0, globalPan=0, globalPanDepth=0, lp=0, hp=0;
    double begin=0,length=1,unit=1,clockLength=1,readScale=1,fadeIn=0,fadeOut=0,fadeShapeIn=3,fadeShapeOut=3;
    bool reverse=false,fixed=true,fit=false,consumeSteps=false,shufflePitch=false;
    uint32_t randomState=1;
    void start(const SliceAudioState&,int requestToUse,uint32_t seed,bool midiPad=false) noexcept;
    void configure(const SliceAudioState&,int totalLength,double sourceRate,bool slotReverse,int slotDownsample) noexcept;
    double position(bool& silent) const noexcept;
    double fade(double localPosition) const noexcept;
    // True at cycle end. The caller applies slot One Shot/release policy.
    bool advance(const SliceAudioState&,double baseIncrement) noexcept;
private:
    int randomIndex(int count) noexcept;
    void chooseEvent(const SliceAudioState&) noexcept;
};
}
