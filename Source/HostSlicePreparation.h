#pragma once
#include "SliceState.h"
namespace lsampler {
// Automation receives the already-sanitised snapshot state, then overlays
// range-checked host globals. Reuse the existing bounded layout/gain equations
// without invoking sliceSanitise (which constructs JUCE Strings). Steps and
// custom boundaries are unchanged; division edits use SliceState::setDivision.
// Keep the frozen TEST59 SliceState sources byte-identical.
inline SliceAudioState prepareAutomatedSliceAudio(const SliceState& state,int length) {
    SliceAudioState a;a.state=state;
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
    }
    // Published sequence duration. Extend adds virtual time exactly as before.
    // Consume Steps spends existing grid cells and skips over the cells it covers,
    // so a 16-step pattern remains bounded by its 16-cell grid.
    for(int i=0;i<a.count;) {
        const auto& p=a.state.steps[size_t(i)];
        const int repeatCount=std::max(1,int(p[SliceP::repeat]));
        const bool fit=p[SliceP::fit]!=0;
        const bool consume=p[SliceP::repeatType]!=0&&!fit;
        if(consume) {
            const int used=std::min(repeatCount,a.count-i);
            a.cycleUnits+=used;i+=used;
        } else {
            a.cycleUnits+=fit?1:repeatCount;++i;
        }
    }
    return a;
}
} // namespace lsampler
