#include "SliceEngine.h"
namespace lsampler {
namespace {
double fadeFrames(double ms,double len,double rate) noexcept {
    if(ms<=500)return std::min(ms*.001*rate,len*.5);
    const double t=std::clamp((ms-500)/500,0.0,1.0);return len*(.5+.48*t*t);
}
double fadeShape(double frames,double rate) noexcept {
    const double t=std::clamp((frames*1000/rate-100)/900,0.0,1.0);return 3+9*t*t;
}
}
int SlicePlayback::randomIndex(int n) noexcept {
    auto x=randomState;x^=x<<13;x^=x>>17;x^=x<<5;randomState=x;
    return std::min(n-1,int(double(x)*n/4294967296.0));
}
void SlicePlayback::start(const SliceAudioState& a,int req,uint32_t seed,bool midiPad) noexcept {
    *this=SlicePlayback{};request=req;randomState=seed?seed:1;pad=midiPad;
    // -2000-index means: force sequencer playback, beginning from that step.
    // -3000-index means: preserve the configured Slice Mode, but begin its
    // timeline from that step.  This is the non-retriggering editor preview head.
    // Keep physical-slice audition in its existing -1000-index range.
    const bool sequencerFromStep=req<=-2000&&req>-3000;
    const bool normalFromStep=req<=-3000&&req>-4000;
    mode=(req==-3||sequencerFromStep)?7:int(a.state[SliceG::mode]);
    physical=req<=-1000&&req>-2000;
    single=req>=0||physical;
    active=a.count>0&&req!=-2&&(single||mode!=0);
    if(!active)return;
    timeline=sequencerFromStep?std::clamp(-2000-req,0,a.count-1)
        :normalFromStep?std::clamp(-3000-req,0,a.count-1)
        :single?std::clamp(physical?-1000-req:req,0,a.count-1):0;
    chooseEvent(a);
}
void SlicePlayback::chooseEvent(const SliceAudioState& a) noexcept {
    const int n=a.count;if(n<=0){active=false;finished=true;return;}
    timeline=std::clamp(timeline,0,n-1);step=timeline;source=timeline;
    const int rnd=mode==7?int(a.state[SliceG::random]):mode==3?1:mode==4?2:0;
    if(!single&&rnd!=0) {
        step=randomIndex(n);
        if(rnd==2)for(int i=0;i<n-1;++i) {
            const int candidate=mode==7?std::clamp(int(a.state.steps[size_t(step)][SliceP::source])-1,0,n-1):step;
            if(candidate!=lastSource)break;
            step=(step+1)%n;
        }
        source=step;
    }
    if((mode==7||single)&&!physical)source=std::clamp(int(a.state.steps[size_t(step)][SliceP::source])-1,0,n-1);
    else if(!single) {
        switch(mode) {
            case 1:source=n-1-timeline;break;
            case 2:source=std::min(n-1,timeline%2?timeline-1:timeline+1);break;
            case 5:source=timeline%2?n/2+timeline/2:(n-1)/2-timeline/2;break;
            case 6:source=timeline%2?n-1-timeline/2:timeline/2;break;
            default:break;
        }
    }
    source=std::clamp(source,0,n-1);lastSource=source;repeat=0;configured=false;
}
void SlicePlayback::configure(const SliceAudioState& a,int /*totalLength*/,double rate,bool slotReverse,int slotDownsample) noexcept {
    if(!active||a.count<=0)return;
    const auto& g=a.state;
    source=std::clamp(source,0,a.count-1);step=std::clamp(step,0,a.count-1);
    const bool programmed=(mode==7||single)&&!physical;
    const SliceStep defaults;const auto& p=programmed?g.steps[size_t(step)]:defaults;
    if(programmed)source=std::clamp(int(p[SliceP::source])-1,0,a.count-1);
    begin=a.boundaries[size_t(source)];length=a.boundaries[size_t(source+1)]-begin;
    // The JSFX has equal source windows. With manually edited shared boundaries,
    // an event follows the real source-window duration instead of retuning it.
    unit=length;
    repeats=programmed?int(p[SliceP::repeat]):1;repeat=std::min(repeat,repeats-1);fit=p[SliceP::fit]!=0;
    eventUnits=fit?1:repeats;
    const double sourceNorm=a.count>1?2.0*source/(a.count-1)-1:0;
    double semi=p[SliceP::pitch];
    if(g[SliceG::pitchMode]!=0&&!physical)semi+=sourceNorm*g[SliceG::pitchDepth]*(g[SliceG::pitchMode]==2?-1:1);
    // Random events inherit otherwise empty repeat modulation from the timeline
    // event, matching the final JSFX random-repeat fixes.
    auto depth=[&](SliceP key) {
        double value=p[key];
        if(programmed&&!single&&g[SliceG::random]!=0&&std::abs(value)<.000001)
            value=g.steps[size_t(std::clamp(timeline,0,a.count-1))][key];
        return value;
    };
    semi+=repeat*depth(SliceP::repeatPitch);
    pitch=std::pow(2.0,std::clamp(semi,-1200.0,1200.0)/12);
    fixed=programmed&&g[SliceG::pitchTime]==0;
    if(physical){pitch=1;fixed=true;}
    gain=programmed?a.gains[size_t(step)]:1;pan=p[SliceP::pan]*.01;
    repeatPan=repeats>1?(2.0*repeat/(repeats-1)-1)*depth(SliceP::repeatPan)*.01:0;
    lp=-repeat*depth(SliceP::repeatLP);hp=-repeat*depth(SliceP::repeatHP);
    globalPanDepth=(!physical&&g[SliceG::panMode]!=0)?g[SliceG::panDepth]:0;
    globalPan=sourceNorm*(g[SliceG::panMode]==2?-1:1);
    output=int(p[SliceP::output]);downsample=int(p[SliceP::downsample]);
    if(downsample<0)downsample=slotDownsample;
    if(downsample>0) {
        constexpr double rates[]{0,32000,22050,22050,12000,12000,11025,11025,8000,8000,8000};
        constexpr int bits[]{0,16,12,8,8,4,12,8,12,8,4};
        downsampleHold=std::max(1,int(std::floor(rate/rates[downsample]+.5)));
        downsampleScale=std::pow(2.0,bits[downsample]-1)-1;
    }
    // Per-step On reverses the local slice; Inherit retains slot direction.
    reverse=p[SliceP::reverse]<0?slotReverse:p[SliceP::reverse]!=0;
    const double swing=single?0:g[SliceG::shuffle]*.005;
    const auto virtualUnit=static_cast<uint64_t>(std::floor(units+(fit?0:repeat)));
    const double swingScale=virtualUnit%2?1-swing:1+swing;
    clockLength=std::max(1.0,unit/(fit?repeats:1))*swingScale;
    shufflePitch=g[SliceG::shufflePitch]!=0;
    // Shuffle without pitch moves/clips events and holds the final source frame;
    // Shuffle Pitch resamples their full length into the swung event duration.
    readScale=length/(unit/(fit?repeats:1))/(shufflePitch?swingScale:1);
    double fi=p[SliceP::fadeIn]<0?g[SliceG::fadeIn]:p[SliceP::fadeIn];
    double fo=p[SliceP::fadeOut]<0?g[SliceG::fadeOut]:p[SliceP::fadeOut];
    // Lua and direct-preview sliders expose 0..1000. The JSFX sequence helper
    // still clamps overrides to 100; preserve the documented UI range here.
    fadeIn=fadeFrames(fi,length,rate);fadeOut=fadeFrames(fo,length,rate);
    fadeShapeIn=fadeShape(fadeIn,rate);fadeShapeOut=fadeShape(fadeOut,rate);
    configured=true;
}
double SlicePlayback::position(bool& silent) const noexcept {
    double local=elapsed*readScale*(fixed?pitch:1);
    silent=fixed&&pitch>1.000001&&local>=length;
    local=std::clamp(local,0.0,std::max(0.0,length-1));
    return begin+(reverse?length-1-local:local);
}
double SlicePlayback::fade(double local) const noexcept {
    double g=1;
    if(fadeIn>1&&local<fadeIn)g=std::pow(std::clamp(local/fadeIn,0.0,1.0),fadeShapeIn);
    const double tail=length-1-local;
    if(fadeOut>1&&tail<fadeOut)g=std::min(g,std::pow(std::clamp(tail/fadeOut,0.0,1.0),fadeShapeOut));
    return g;
}
bool SlicePlayback::advance(const SliceAudioState& a,double increment) noexcept {
    if(!active||finished)return false;
    elapsed+=increment*(fixed?1:pitch);
    if(elapsed<clockLength)return false;
    // Extremely short/pitched slices still have at least one output frame.
    // Carry the fractional overshoot, bounded to the next event, without loops
    // proportional to an arbitrarily large pitch increment on the audio thread.
    elapsed=std::min(elapsed-clockLength,clockLength*.999999);
    ++repeat;configured=false;
    if(repeat<repeats)return false;
    if(single){finished=true;return true;}
    units+=eventUnits;consumedUnits+=eventUnits;
    const double cycle=mode==7?a.cycleUnits:a.count;
    ++timeline;
    const bool ended=(mode==7&&a.state[SliceG::random]!=0)?consumedUnits>=cycle:timeline>=a.count;
    if(ended){timeline=0;units=0;consumedUnits=0;}
    else timeline%=std::max(1,a.count);
    chooseEvent(a);return ended;
}
}
