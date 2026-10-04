#include "SamplerVoice.h"
#include <cmath>
#include <algorithm>
using namespace lsampler;
namespace {
constexpr double pi=3.14159265358979323846, offAmplitude=.00003162277660168379;
double clamp(double x,double lo,double hi) noexcept {return std::clamp(x,lo,hi);}
double wrap(double x,double length) noexcept {return x-std::floor(x/length)*length;}
double edgeGain(double pos,double length,double in,double out) noexcept {
    double g=1;
    if(in>1 && pos<in)g=std::min(g,clamp(pos/in,0,1));
    if(out>1 && length-1-pos<out)g=std::min(g,clamp((length-1-pos)/out,0,1));
    return g;
}
double velocityGain(double velocity,double depth) noexcept {
    // Keep the Lua -1..1 edit range but obey JSFX DSP precedence: negative depths
    // were clamped to zero by the legacy engine (they are not an inverse velocity mode).
    depth=clamp(depth,0,1);
    return 1-depth+depth*velocity;
}
}
double GlobalVoicePool::Envelope::tick(const EnvelopeSettings& e) noexcept {
    if(phase==1) {value+=e.attackStep;if(value>=1){value=1;phase=2;}}
    if(phase==2) {
        value=e.filter?value-e.decay:e.sustain+(value-e.sustain)*e.decay;
        if(value<=e.sustain+.000001){value=e.sustain;phase=3;}
    }
    if(phase==3)value=e.sustain;
    if(phase==4){value*=e.release;if(value<=offAmplitude){value=0;phase=0;}}
    return value;
}
void GlobalVoicePool::prepare(double rate) {
    allNotesOff();hostSampleRate=rate>0?rate:44100;ageCounter=noteCounter=0;
    freePhase={};bend={};wheel={};pedal={};diagnostics={};
    outputRoutes.fill(-1);outputRoutes[0]=0;
}
void GlobalVoicePool::setStates(const std::array<SlotAudioState,slotCount>* next) noexcept {
    states=next;
    if(!states)return;
    std::array<bool,slotCount> changed {};
    for(auto& v:voices)if(v.active) {
        const auto& s=(*states)[size_t(v.slotIndex)];
        if(v.sample!=s.sample || s.length<1)stop(v);
        else if(v.revision!=s.revision) {changed[size_t(v.slotIndex)]=true;updateVoice(v,s);}
    }
    // A live mode/cap edit must also constrain already sounding voices.
    // Preserve the newest voices, using the same oldest-first stealing policy.
    for(int slot=0;slot<slotCount;++slot)if(changed[size_t(slot)]) {
        const auto& p=(*states)[size_t(slot)].params;
        const int cap=(p[P::polyphony]==0||p[P::retrigger_smooth]>0)?1:int(p[P::slot_polyphony]);
        while(cap>0 && activePerSlot[size_t(slot)]>cap)stop(chooseVoice(slot,cap));
    }
}
void GlobalVoicePool::stop(Voice& v) noexcept {
    if(v.active) {
        --activePerSlot[size_t(v.slotIndex)];
        v.active=false;
        v.sample->voiceReferences.fetch_sub(1,std::memory_order_release);
        v.sample=nullptr;
    }
}
void GlobalVoicePool::release(Voice& v) noexcept {v.amp.release();v.lpEnvelope.release();v.hpEnvelope.release();v.keyDown=false;v.sustained=false;v.loopsReleased=true;}
void GlobalVoicePool::allNotesOff() {for(auto& v:voices)stop(v);held={};pedal={};}
void GlobalVoicePool::stopSlotVoices(int slot) {for(auto& v:voices)if(v.active&&v.slotIndex==slot)stop(v);}
void GlobalVoicePool::stopPreviewVoices(int slot) {for(auto& v:voices)if(v.active&&v.preview&&(slot<0||v.slotIndex==slot))stop(v);}
bool GlobalVoicePool::hasPreviewVoices(int slot) const noexcept {for(const auto& v:voices)if(v.active&&v.preview&&(slot<0||v.slotIndex==slot))return true;return false;}
int GlobalVoicePool::activeVoiceCount() const noexcept {int n=0;for(auto i:activePerSlot)n+=i;return n;}
int GlobalVoicePool::activeVoiceCount(int slot) const noexcept {return activePerSlot[size_t(slot)];}
GlobalVoicePool::Voice& GlobalVoicePool::chooseVoice(int slot,int cap) {
    Voice* oldest=nullptr;
    if(cap>0 && activePerSlot[size_t(slot)]>=cap) {
        for(auto& v:voices)if(v.active&&v.slotIndex==slot&&(!oldest||v.age<oldest->age))oldest=&v;
        if(oldest)return *oldest;
    }
    for(auto& v:voices)if(!v.active)return v;
    oldest=&voices.front();for(auto& v:voices)if(v.age<oldest->age)oldest=&v;
    return *oldest;
}
void GlobalVoicePool::selectStage(Voice& v,const SlotAudioState& s) noexcept {
    const double pos=v.position/s.stretchFactor;
    int selected=-1;
    for(int i=0;i<s.stageCount;++i)if(s.stages[size_t(i)].start<=pos)selected=i;
    if(selected!=v.stage){v.stage=selected;v.repeat=0;v.skipStage=-1;}
}
void GlobalVoicePool::updateVoice(Voice& v,const SlotAudioState& s) noexcept {
    const auto& p=s.params;
    v.revision=s.revision;v.pitchCached=v.followCached=v.lpModCached=v.hpModCached=1e30;
    v.velocityGain=velocityGain(v.velocity,p[P::vel_volume_depth]);
    {
        const double vel=p[P::lp_vel_amount];
        double base=p[P::lp_cutoff];
        base*=vel>=0?1-(1-v.velocity)*vel:1-v.velocity*(-vel);
        v.lpBase=20*std::pow(500,base);
        v.lpCoefficients=s.lpStatic;
    }
    {
        const double vel=p[P::hp_vel_amount];
        double base=p[P::hp_cutoff];
        base+=(1-base)*(vel>=0?v.velocity*vel:(1-v.velocity)*(-vel));
        v.hpBase=20*std::pow(500,base);v.hpCoefficients=s.hpStatic;
    }
    if(v.sustained && !p[P::sustain_pedal])release(v);
    selectStage(v,s);
    if(v.slice.request==-1) {
        const int mode=int(s.slice.state[SliceG::mode]);
        if(v.slice.mode!=mode || (v.slice.active!=(mode!=0&&s.slice.count>0)))
            v.slice.start(s.slice,-1,v.randomState);
    }
    v.slice.configured=false;
}
void GlobalVoicePool::retarget(Voice& v,int note,double velocity,const SlotAudioState& s,bool retrigger) {
    v.note=note;v.velocity=velocity;v.keyDown=true;v.sustained=false;
    if(retrigger) {
        v.amp={};v.lpEnvelope={};v.hpEnvelope={};v.loopsReleased=false;v.repeat=0;v.skipStage=-1;
        // With No Retrigger Smooth, preserve the read head on a retrigger.
        if(s.params[P::retrigger_smooth]==0) {
            const double skip=std::round(std::min(s.length*.20,s.sample->sourceSampleRate*.250)*s.params[P::vel_attack_depth]*.01*std::pow(1-velocity,1.5));
            const int rawSamples = s.sample ? s.sample->audio.getNumSamples() : 0;
            const double absoluteFrame = rawSamples > 1
                ? clamp(s.params[P::sample_play_start], 0.0, 100.0) * .01 * rawSamples
                : double(s.start);
            const double localFrame = clamp(absoluteFrame - s.start, 0.0, double(s.length - 1));
            v.position=clamp(std::round(localFrame)+skip,0,double(s.length-1));
        }
        for(int i=0;i<2;++i)if(s.lfo[size_t(i)].trigger||s.lfo[size_t(i)].oneShot)v.lfo[size_t(i)]={};
    }
    if(retrigger)v.slice.start(s.slice,-1,v.randomState);
    if(s.params[P::portamento]==0)v.effectiveNote=note;
    updateVoice(v,s);
}
void GlobalVoicePool::noteOn(int slot,int note,float velocity,int channel,bool preview,double previewStartPercent,int sliceRequest,bool sliceMidiPad) {
    if(!states||slot<0||slot>=slotCount||note<0||note>127||channel<0||channel>15)return;
    const auto& s=(*states)[size_t(slot)];const auto& p=s.params;
    if(!s.sample||s.length<1||s.sample->peak<1e-12)return;
    if(!preview)held[size_t(channel)][size_t(note)]={++noteCounter,velocity};
    const bool mono=p[P::polyphony]==0;
    if(!mono&&!preview&&!sliceMidiPad&&p[P::retrigger_smooth]>0)
        for(auto& existing:voices)if(existing.active&&!existing.preview&&existing.slotIndex==slot) {
            existing.channel=channel;retarget(existing,note,velocity,s,true);return;
        }
    if(mono&&!preview&&!sliceMidiPad)for(auto& v:voices)if(v.active&&!v.preview&&v.slotIndex==slot) {
        const bool connected=v.keyDown||v.sustained;
        v.channel=channel;retarget(v,note,velocity,s,p[P::retrigger_smooth]>0||!(connected&&p[P::legato]!=0));return;
    }
    auto& v=chooseVoice(slot,mono?1:int(p[P::slot_polyphony]));stop(v);v=Voice{};
    v.active=true;v.preview=preview;v.slotIndex=slot;v.note=note;v.channel=channel;v.velocity=velocity;
    v.sample=s.sample;v.sample->voiceReferences.fetch_add(1,std::memory_order_relaxed);++activePerSlot[size_t(slot)];
    v.age=++ageCounter;v.randomState=uint32_t(v.age*747796405u+uint64_t(note)*2891336453u+1);
    v.effectiveNote=note;v.velocityGain=velocityGain(velocity,p[P::vel_volume_depth]);v.velocitySmooth=v.velocityGain;
    if(!mono&&p[P::poly_drift]>0) {
        const double h=std::sin(double(&v-voices.data()+1)*12.9898+(note+1)*78.233+(slot+1)*37.719)*43758.5453;
        v.drift=((h-std::floor(h))*2-1)*p[P::poly_drift];
    }
    const double skip=std::round(std::min(s.length*.20,s.sample->sourceSampleRate*.250)*p[P::vel_attack_depth]*.01*std::pow(1-velocity,1.5));
    // Sample Play Start and preview audition positions are absolute percentages
    // of the original in-RAM sample. Convert through raw frames so a hidden
    // Start/End Threshold window can clamp playback without changing the visible
    // Sample Start/Sample End values.
    const double absoluteStart = (preview && previewStartPercent >= 0.0)
        ? previewStartPercent
        : p[P::sample_play_start];
    const int rawSamples = s.sample ? s.sample->audio.getNumSamples() : 0;
    const double absoluteFrame = rawSamples > 1
        ? clamp(absoluteStart, 0.0, 100.0) * .01 * rawSamples
        : double(s.start);
    const double localFrame = clamp(absoluteFrame - s.start, 0.0, double(s.length - 1));
    v.position=clamp(std::round(localFrame)+skip,0,double(s.length-1));
    for(int i=0;i<2;++i) {
        const auto& l=s.lfo[size_t(i)];
        if(!l.trigger&&!l.oneShot)v.lfo[size_t(i)].phase=freePhase[size_t(slot)][size_t(i)];
        v.lfo[size_t(i)].hold=random(v);
    }
    updateVoice(v,s);
    v.slice.start(s.slice,sliceRequest,v.randomState,sliceMidiPad);
    if(sliceMidiPad)v.effectiveNote=p[P::root];
}
void GlobalVoicePool::noteOff(int note,int channel) {
    if(!states||note<0||note>127||channel<0||channel>15)return;
    held[size_t(channel)][size_t(note)]={};
    for(auto& v:voices)if(v.active&&!v.preview&&v.note==note&&v.channel==channel) {
        const auto& s=(*states)[size_t(v.slotIndex)];const auto& p=s.params;
        if(p[P::polyphony]==0&&!v.slice.pad) {
            int last=-1,lastChannel=0;uint64_t order=0;
            for(int ch=0;ch<16;++ch)for(int n=0;n<128;++n) {
                const auto& h=held[size_t(ch)][size_t(n)];const auto vel=std::round(h.velocity*127);
                if(h.order>order&&n>=p[P::low]&&n<=p[P::high]&&vel>=p[P::velocity_low]&&vel<=p[P::velocity_high]){order=h.order;last=n;lastChannel=ch;}
            }
            if(last>=0){v.channel=lastChannel;retarget(v,last,held[size_t(lastChannel)][size_t(last)].velocity,s,p[P::legato]==0);continue;}
        }
        v.keyDown=false;
        if(p[P::sustain_pedal]!=0&&pedal[size_t(channel)])v.sustained=true;
        else release(v);
    }
}
void GlobalVoicePool::controller(int ch,int number,int value) {
    if(ch<0||ch>=16)return;
    if(number==1)wheel[size_t(ch)]=value/127.0;
    if(number==64) {
        pedal[size_t(ch)]=value>=64;
        if(value<64)for(auto& v:voices)if(v.active&&v.channel==ch&&v.sustained)release(v);
    }
    if(number==120||number==123) {
        held[size_t(ch)]={};pedal[size_t(ch)]=false;
        for(auto& v:voices)if(v.active&&!v.preview&&v.channel==ch){if(number==120)stop(v);else release(v);}
    }
    if(number==121) {
        bend[size_t(ch)]=wheel[size_t(ch)]=0;pedal[size_t(ch)]=false;
        for(auto& v:voices)if(v.active&&v.channel==ch&&v.sustained)release(v);
    }
}
void GlobalVoicePool::pitchBend(int ch,int value) noexcept {if(ch>=0&&ch<16)bend[size_t(ch)]=clamp(value/8192.0,-1,1);}
void GlobalVoicePool::choke(int note) {
    if(!states)return;
    for(const auto& s:*states)if(s.sample&&s.params[P::choke_trigger]==note&&s.params[P::choke_target]>=0)
        for(auto& v:voices)if(v.active&&!v.preview&&v.note==int(s.params[P::choke_target])) {
            if(s.params[P::choke_mode]==0)stop(v);else release(v);
        }
}
double GlobalVoicePool::random(Voice& v) noexcept {
    uint32_t x=v.randomState;x^=x<<13;x^=x>>17;x^=x<<5;v.randomState=x;
    return double(x)*(2.0/4294967296.0)-1;
}
double GlobalVoicePool::oscillator(Voice& v,double phase,int wave,bool isLfo) noexcept {
    phase=wrap(phase,1);
    switch(wave) {
        case 0:return std::sin(2*pi*phase);
        case 1:return isLfo?4*std::abs(phase-.5)-1:1-4*std::abs(phase-.5);
        case 2:return 2*phase-1;
        case 3:return 1-2*phase;
        case 4:return phase<.5?1:-1;
        default:return random(v);
    }
}
double GlobalVoicePool::lfoTick(Voice& v,int index,const LfoSettings& s,double wheelValue) noexcept {
    if(!s.active && (s.wheel==0||wheelValue==0))return 0;
    ++diagnostics.lfo;
    auto& l=v.lfo[size_t(index)];++l.age;
    l.phase+=s.rate*(s.sync?bpm:1);
    if(s.oneShot)l.phase=std::min(1.0,l.phase);
    else if(l.phase>=1){l.phase=wrap(l.phase,1);if(s.wave==5)l.hold=random(v);}
    double raw=s.wave==5?l.hold:oscillator(v,l.phase,s.wave,true);
    // Legacy Delay is a fade-in, not a postponed oscillator start.
    if(s.delay>0 && double(l.age)<s.delay)raw*=double(l.age)/s.delay;
    l.output+=(raw-l.output)*s.smoothing;
    return l.output;
}
double GlobalVoicePool::read(const Voice& v,const SlotAudioState& s,double pos,int channel) const noexcept {
    if(pos<0)return 0; // native equivalent of the legacy per-channel delay's silent pre-roll
    pos=clamp(pos,0,double(s.length-1));
    if(!v.slice.active&&s.params[P::ram_reverse]!=0)pos=s.length-1-pos;
    const int low=v.slice.active?int(v.slice.begin):0;
    const int high=v.slice.active?int(v.slice.begin+v.slice.length)-1:s.length-1;
    if(v.slice.active&&(pos<low||pos>high))return 0;
    const auto& audio=v.sample->audio;
    channel=(s.params[P::mode]==0)?0:std::min(channel,audio.getNumChannels()-1);
    const auto* data=audio.getReadPointer(channel);
    auto at=[&](int relative) {
        const int index=s.start+std::clamp(relative,low,high);
        double x=data[index];
        if(s.params[P::dc_remove]!=0)x-=v.sample->dc[size_t(channel)];
        // Legacy RAM fades are sequential multiplications, including overlapping fades.
        if(s.fadeIn>1&&index<s.fadeIn)x*=index/s.fadeIn;
        const double tail=audio.getNumSamples()-1-index;
        if(s.fadeOut>1&&tail<s.fadeOut)x*=tail/s.fadeOut;
        return x;
    };
    const int idx=int(pos);
    double value;
    const int ds=v.slice.active&&v.slice.downsample>=0?v.slice.downsample:int(s.params[P::ram_downsample]);
    if(ds!=0) {
        const int hold=v.slice.active?v.slice.downsampleHold:s.downsampleHold;
        const double scale=v.slice.active?v.slice.downsampleScale:s.downsampleScale;
        value=at(low+((idx-low)/hold)*hold);
        value=clamp(std::copysign(std::floor(std::abs(value)*scale+.5)/scale,value),-1,1);
    } else {const double a=at(idx);value=a+(at(std::min(idx+1,s.length-1))-a)*(pos-idx);}
    return value;
}
void GlobalVoicePool::processEffects(Voice& v,const SlotAudioState& s,double& left,double& right) {
    const auto& p=s.params;
    if(p[P::ram_swap_lr]!=0)std::swap(left,right);
    if(p[P::ram_stereo_width]!=100) {
        const double mid=(left+right)*.5,side=(left-right)*.5*p[P::ram_stereo_width]*.01;
        left=mid+side;right=mid-side;
    }
    if(p[P::normalize_on]!=0){left*=s.normalize;right*=s.normalize;}
    if(s.transient) {
        ++diagnostics.transient;const double peak=std::max(std::abs(left),std::abs(right));
        v.transFast=s.transFast*v.transFast+(1-s.transFast)*peak;
        v.transSlow=s.transSlow*v.transSlow+(1-s.transSlow)*peak;
        const double gain=1+s.transMix*(clamp(1+s.transAmount*(v.transFast-v.transSlow)*6,0,4)-1);
        left*=gain;right*=gain;
    }
    auto drive=[&] {
        if(!s.drive)return;
        ++diagnostics.drive;const double amount=p[P::drive_amount]*.01;
        auto colour=[&](double x){double y=x*(1+amount*8);
            switch(int(p[P::drive_type])) {case 1:y=(2/pi)*std::atan(y);break;case 2:y/=1+std::abs(y);break;case 3:y=clamp(y,-1,1);break;default:break;}
            return x*(1-amount)+y*amount;};
        left=colour(left);right=colour(right);
    };
    auto comp=[&] {
        if(!s.comp)return;
        ++diagnostics.comp;const double peak=std::max(std::abs(left),std::abs(right));
        const double c=peak>v.compEnv?s.compAttack:s.compRelease;
        v.compEnv=c*v.compEnv+(1-c)*peak;
        // Algebraically equivalent to the legacy dB/log/pow chain, with one pow only above threshold.
        const double gain=(v.compEnv>s.compThreshold?std::pow(v.compEnv/s.compThreshold,s.compExponent):1)*s.compMakeup;
        const double mix=1+s.compMix*(gain-1)-(p[P::comp_delta]!=0?1:0);
        left*=mix;right*=mix;
    };
    auto gate=[&] {
        if(!s.gate)return;
        ++diagnostics.gate;const double peak=std::max(std::abs(left),std::abs(right));
        double c=peak>v.gateEnv?s.gateAttack:s.gateRelease;
        v.gateEnv=c*v.gateEnv+(1-c)*peak;
        double target=s.gateClosed;
        if(v.gateEnv>=s.gateThreshold){target=1;v.gateHold=std::max(v.gateHold,s.gateHold);}
        else if(v.gateHold>0){--v.gateHold;target=1;}
        c=target>v.gateGain?s.gateAttack:s.gateRelease;
        v.gateGain=c*v.gateGain+(1-c)*target;
        const double mix=1+s.gateMix*(v.gateGain-1)-(p[P::gate_delta]!=0?1:0);
        left*=mix;right*=mix;
    };
    if(p[P::drive_position]==0)drive();
    if(p[P::dynamics_order]==0){comp();gate();}else{gate();comp();}
    if(p[P::drive_position]!=0)drive();
    if(s.degrade) {
        ++diagnostics.degrade;
        if(v.degradeCount<=0) {
            v.heldL=left;v.heldR=right;
            const int jitter=s.degradeJitter?int(std::floor((random(v)+1)*.5*(2*s.degradeJitter+1)))-s.degradeJitter:0;
            v.degradeCount=std::max(1,s.degradeHold+jitter);
        }
        --v.degradeCount;
        auto quantise=[&](double x){return std::floor(x*s.degradeScale+(x>=0?.5:-.5))/s.degradeScale;};
        left+=(quantise(v.heldL)-left)*s.degradeAmount;right+=(quantise(v.heldR)-right)*s.degradeAmount;
    }
    if(s.ring) {
        ++diagnostics.ring;v.ringPhase=wrap(v.ringPhase+v.ringIncrement,1);
        const double gain=1+oscillator(v,v.ringPhase,int(p[P::ring_wave]))*p[P::ring_amount];left*=gain;right*=gain;
    }
}
void GlobalVoicePool::applyMachine(Voice& v,const SlotAudioState& s,double& left,double& right) noexcept {
    ++diagnostics.machine;
    const auto& a=s.character;
    if(a[2]+a[3]+a[4]>0) {
        v.characterPhase=wrap(v.characterPhase+.23/hostSampleRate,1);v.characterPhase2=wrap(v.characterPhase2+.137/hostSampleRate,1);
        const double amp=1+std::sin(2*pi*v.characterPhase)*(a[2]*.003+a[3]*.002);
        const double pan=std::sin(2*pi*v.characterPhase2)*(a[2]*.0025+a[3]*.0035);
        left*=amp-pan;right*=amp+pan;
    }
    double pair[]{left,right};
    for(int ch=0;ch<2;++ch) {
        auto& x=pair[ch];
        if(a[0]>0)x=(x*(1+a[0]*7))/(1+std::abs(x)*a[0]*7)/(1+a[0]*1.85);
        if(a[1]>0) {
            x+=(std::floor(x*s.characterScale+.5)/s.characterScale-x)*a[1];
            v.characterConverter[ch]+=(x-v.characterConverter[ch])*s.characterSlew;
            x+=(v.characterConverter[ch]-x)*a[1]*.18;
        }
        if(a[4]>0) {
            v.characterAir[ch]+=(x-v.characterAir[ch])*s.characterAir;
            const double next=x+(v.characterAir[ch]-x)*a[4]*.18+v.characterDelay[ch]*a[4]*.035;
            v.characterDelay[ch]=x;x=next;
        }
    }
    left=pair[0];right=pair[1];
    if(a[5]>0) {
        v.characterGlue=std::max(std::max(std::abs(left),std::abs(right)),v.characterGlue*s.characterRelease);
        const double gain=(1+a[5]*.08)/(1+v.characterGlue*a[5]*.55);left*=gain;right*=gain;
    }
}
void GlobalVoicePool::advanceLoops(Voice& v,const SlotAudioState& s,double increment) noexcept {
    v.position+=increment;
    const double cycle=s.length*s.stretchFactor;
    const int oneShot=int(s.params[P::global_one_shot]);
    if(v.position>=cycle && oneShot!=0 && !v.loopsReleased) {
        if(oneShot==1){stop(v);return;}
        release(v);v.position=wrap(v.position,cycle);v.stage=-1;v.repeat=0;v.skipStage=-1;
    }
    if(s.stageCount>0) {
        // Catch entry into the next absolute stage without a per-sample ten-stage scan.
        while(v.stage+1<s.stageCount && v.position>=s.stages[size_t(v.stage+1)].start*s.stretchFactor
              && (v.stage<0 || v.position<s.stages[size_t(v.stage)].end*s.stretchFactor || v.skipStage==v.stage)) {
            ++v.stage;v.repeat=0;v.skipStage=-1;
        }
        if(v.stage>=0 && v.skipStage!=v.stage && (!v.loopsReleased||s.params[P::global_release_loops]!=0)) {
            const auto& loop=s.stages[size_t(v.stage)];
            const double end=loop.end*s.stretchFactor;
            if(v.position>=end) {
                const double overshoot=v.position-end;
                const bool next=v.stage+1<s.stageCount;
                const bool baseOneShot=loop.memory==0&&!next&&oneShot!=0;
                if(baseOneShot&&!v.loopsReleased) {
                    if(oneShot==1){stop(v);return;}
                    release(v);v.skipStage=v.stage;
                } else {
                    const int repeats=v.loopsReleased&&loop.repeats==0?1:loop.repeats;
                    if(repeats==0||++v.repeat<=repeats) {
                        const double length=std::max(1.0,(loop.end-loop.start)*s.stretchFactor);
                        v.position=loop.start*s.stretchFactor+wrap(overshoot,length);
                    } else if(next&&!loop.oneShot&&!v.loopsReleased) {
                        ++v.stage;v.repeat=0;v.skipStage=-1;
                        v.position=s.stages[size_t(v.stage)].start*s.stretchFactor+overshoot;
                    } else {
                        if(loop.oneShot)release(v);
                        v.skipStage=v.stage;v.repeat=0;
                        // A final finite stage passes through to the sample end.
                        if(!next)v.loopsReleased=true;
                    }
                }
            }
        }
    }
    if(v.position>=cycle) {
        v.position=wrap(v.position,cycle);v.repeat=0;v.skipStage=-1;selectStage(v,s);
    }
}
void GlobalVoicePool::advanceSlice(Voice& v,const SlotAudioState& s,double increment) noexcept {
    if(v.slice.advance(s.slice,increment)) {
        if(v.slice.single||s.params[P::global_one_shot]==1){stop(v);return;}
        if(s.params[P::global_one_shot]==2&&!v.loopsReleased)release(v);
    }
}
void GlobalVoicePool::render(juce::AudioBuffer<float>& output,int start,int count) {
    if(!states||count<=0||activeVoiceCount()==0)return;
    for(auto& v:voices) {
        if(!v.active)continue;
        const auto& s=(*states)[size_t(v.slotIndex)];const auto& p=s.params;
        const int channel=outputRoutes[size_t(int(p[P::output_route]))];
        float* outL=channel>=0 && channel<output.getNumChannels()?output.getWritePointer(channel):nullptr;
        float* outR=channel>=0 && channel+1<output.getNumChannels()?output.getWritePointer(channel+1):nullptr;
        const double mirror=p[P::polyphony]!=0&&((&v-voices.data())%2)!=0?-1:1;
        for(int frame=start;frame<start+count && v.active;++frame) {
            if(v.amp.phase==0){stop(v);break;}
            if(v.slice.active) {
                if(s.slice.count<=0){stop(v);break;}
                if(!v.slice.configured)v.slice.configure(s.slice,s.length,s.sample->sourceSampleRate,p[P::ram_reverse]!=0,int(p[P::ram_downsample]));
            }
            const double targetNote=v.slice.pad?p[P::root]:v.note;
            if(std::abs(v.effectiveNote-targetNote)>.0000001)v.effectiveNote+=(targetNote-v.effectiveNote)*s.portamento;
            else v.effectiveNote=targetNote;
            const double effective=v.effectiveNote+bend[size_t(v.channel)]*p[P::pitch_bend_range];
            // Zero level / settled silent sustain keeps its musical read clock but sleeps every optional processor.
            if(s.level==0 || (v.amp.phase==3 && s.amp.sustain==0)) {
                v.amp.tick(s.amp);
                const double pitch=effective-p[P::root]+p[P::octave]*12+p[P::voice_pitch]+v.drift;
                if(pitch!=v.pitchCached){v.increment=std::pow(2.0,pitch/12)*s.sourceRatio;v.pitchCached=pitch;}
                if(v.slice.active)advanceSlice(v,s,v.increment);else advanceLoops(v,s,v.increment);continue;
            }
            const double wheelValue=wheel[size_t(v.channel)];
            const double l1=lfoTick(v,0,s.lfo[0],wheelValue),l2=lfoTick(v,1,s.lfo[1],wheelValue);
            const LoopAudioState* loop=!v.slice.active&&v.stage>=0?&s.stages[size_t(v.stage)]:nullptr;
            const double repeats=loop&&loop->repeats>2?std::min(v.repeat,loop->repeats):0;
            const double repeatPitch=loop?-repeats*loop->pitch:0;
            const double pitch=effective-p[P::root]+p[P::octave]*12+p[P::voice_pitch]+v.drift
                +v.amp.value*p[P::pitch_env]+l1*s.lfo[0].pitch+l2*(s.lfo[1].pitch+wheelValue*s.lfo[1].wheel)+repeatPitch;
            if(pitch!=v.pitchCached) {v.increment=std::pow(2.0,pitch/12)*s.sourceRatio;v.pitchCached=pitch;}
            if((s.ring||s.fm)&&effective!=v.followCached) {
                const double follow=std::pow(2.0,(effective-69)/12);
                if(s.ring)v.ringIncrement=clamp(p[P::ring_freq]*(p[P::ring_mode]==2?follow:1),.1,20000)/hostSampleRate;
                if(s.fm)v.fmIncrement=clamp(440*follow*p[P::fm_ratio],.1,20000)/hostSampleRate;
                v.followCached=effective;
            }
            bool sliceSilent=false;
            double pos=v.slice.active?v.slice.position(sliceSilent):v.position;
            if(!v.slice.active&&p[P::stretch_amount]>0)pos=std::floor(pos/(s.grain*s.stretchFactor))*s.grain+wrap(pos,s.grain);
            else if(!v.slice.active&&p[P::stretch_amount]<0)pos=std::floor(pos/s.grain)*(s.grain/s.stretchFactor)+wrap(pos,s.grain);
            pos=clamp(pos,0,double(s.length-1));
            if(s.lfo[0].move!=0||s.lfo[1].move!=0)pos=clamp(pos+(l1*s.lfo[0].move+l2*s.lfo[1].move)*.01*s.length,0,double(s.length-1));
            if(s.fm) {
                ++diagnostics.fm;v.fmPhase=wrap(v.fmPhase+v.fmIncrement,1);
                v.fmFeedback=oscillator(v,v.fmPhase+v.fmFeedback*p[P::fm_feedback]*.0095,int(p[P::fm_wave]));
                pos=clamp(pos+v.fmFeedback*p[P::fm_amount]*.01*std::min(2048.0,std::max(1.0,s.length*.025)),0,double(s.length-1));
            }
            double left=read(v,s,pos-s.delayL,0),right=read(v,s,pos-s.delayR,1);
            if(v.slice.active) {
                const auto& q=v.slice;
                const double fade=q.fade(pos-q.begin)*(sliceSilent?0:q.gain);
                left*=fade*(1-std::max(0.0,q.pan))*(1-std::max(0.0,q.repeatPan));
                right*=fade*(1+std::min(0.0,q.pan))*(1+std::min(0.0,q.repeatPan));
            }
            if(!v.slice.active&&s.crossfade>1) {
                const double ls=loop?loop->start:0,le=loop?loop->end:s.length;
                const bool one=loop?loop->oneShot:p[P::global_one_shot]!=0;
                const double fade=std::min(s.crossfade,(le-ls)*.5);
                if(!one&&fade>1) {
                    auto cross=[&](double x,int ch,double current) {
                        if(x<le-fade||x>=le)return current;
                        const double t=(x-(le-fade))/fade;
                        return current*(1-t)+read(v,s,ls+x-(le-fade),ch)*t;
                    };
                    left=cross(pos-s.delayL,0,left);right=cross(pos-s.delayR,1,right);
                }
            }
            if(s.edgeFade>0) {left*=edgeGain(pos-s.delayL,s.length,s.edgeFade,s.edgeFade);right*=edgeGain(pos-s.delayR,s.length,s.edgeFade,s.edgeFade);}
            if(loop&&(loop->fadeIn>0||loop->fadeOut>0)) {
                auto loopFade=[&](double x) {
                    if(x<loop->start||x>=loop->end)return 1.0;
                    const double gain=edgeGain(x-loop->start,loop->end-loop->start,loop->fadeIn,loop->fadeOut);
                    return gain*gain*gain; // compute_fade_gain_in_out in the supplied JSFX
                };
                left*=loopFade(pos-s.delayL);right*=loopFade(pos-s.delayR);
            }
            processEffects(v,s,left,right);
            if(v.slice.active&&v.slice.globalPanDepth>0) {
                const auto& q=v.slice;const double mid=(left+right)*.70710678,phase=(q.globalPan+1)*pi*.25;
                left=left*(1-q.globalPanDepth)+mid*std::cos(phase)*q.globalPanDepth;
                right=right*(1-q.globalPanDepth)+mid*std::sin(phase)*q.globalPanDepth;
            }
            const double lfoPan=clamp((l1*s.lfo[0].pan+l2*s.lfo[1].pan)*mirror,-1,1);
            if(lfoPan!=0) {
                const double depth=std::abs(lfoPan),mono=(left+right)*.70710678,phase=(lfoPan+1)*pi*.25;
                left=left*(1-depth)+mono*std::cos(phase)*depth;right=right*(1-depth)+mono*std::sin(phase)*depth;
            }
            const double amp=v.amp.tick(s.amp);
            if(s.lp||(v.slice.active&&v.slice.lp!=0)) {
                ++diagnostics.lp;
                const double env=p[P::lp_env_amount]!=0?v.lpEnvelope.tick(s.lpEnv):0;
                const double mod=env*p[P::lp_env_amount]+(v.note-p[P::root])*p[P::lp_key_follow]
                    +l1*s.lfo[0].lp+l2*s.lfo[1].lp-(loop?repeats*loop->lp:0)+(v.slice.active?v.slice.lp:0);
                if(mod!=v.lpModCached) {
                    v.lpCoefficients=mod==0&&p[P::lp_vel_amount]==0?s.lpStatic:filterCoefficients(false,v.lpBase*std::pow(2.0,mod/12),p[P::lp_resonance],hostSampleRate);
                    v.lpModCached=mod;
                }
                auto filter=[&](double x,int ch) {
                    const auto& c=v.lpCoefficients;const double v3=x-v.lp.low[ch];
                    const double v1=c.a1*v.lp.band[ch]+c.a2*v3,v2=v.lp.low[ch]+c.a2*v.lp.band[ch]+c.a3*v3;
                    v.lp.band[ch]=clamp(2*v1-v.lp.band[ch],-16,16);v.lp.low[ch]=clamp(2*v2-v.lp.low[ch],-16,16);
                    return v.lp.low[ch]; // preserve the legacy integrator output
                };left=filter(left,0);right=filter(right,1);
            }
            if(s.hp||(v.slice.active&&v.slice.hp!=0)) {
                ++diagnostics.hp;
                const double env=p[P::hp_env_amount]!=0?v.hpEnvelope.tick(s.hpEnv):0;
                const double mod=env*p[P::hp_env_amount]+l1*s.lfo[0].hp+l2*s.lfo[1].hp-(loop?repeats*loop->hp:0)+(v.slice.active?v.slice.hp:0);
                if(mod!=v.hpModCached) {
                    v.hpCoefficients=mod==0&&p[P::hp_vel_amount]==0?s.hpStatic:filterCoefficients(true,v.hpBase*std::pow(2.0,mod/12),p[P::hp_resonance],hostSampleRate);
                    v.hpModCached=mod;
                }
                auto filter=[&](double x,int ch) {const auto& c=v.hpCoefficients;const double y=x-v.hp.low[ch]-c.q*v.hp.band[ch];
                    v.hp.band[ch]=clamp(v.hp.band[ch]+c.f*y,-16,16);v.hp.low[ch]=clamp(v.hp.low[ch]+c.f*v.hp.band[ch],-16,16);return y;};
                left=filter(left,0);right=filter(right,1);
            }
            v.velocitySmooth+=(v.velocityGain-v.velocitySmooth)*std::min(1.0,1/(.002*hostSampleRate));
            const double rawAmp=amp*v.velocitySmooth;
            if(p[P::retrigger_smooth]>0)v.ampSmooth+=(rawAmp-v.ampSmooth)*s.smooth;else v.ampSmooth=rawAmp;
            const double gain=v.ampSmooth*s.level*clamp(1+l1*s.lfo[0].volume+l2*s.lfo[1].volume,0,2);
            double panL=s.panL,panR=s.panR;
            if(p[P::pan_env]!=0){const double pan=clamp((p[P::pan]+amp*p[P::pan_env]*mirror)*.01,-1,1);panL=1-std::max(0.0,pan);panR=1+std::min(0.0,pan);}
            left*=gain*panL;right*=gain*panR;
            if(s.machine)applyMachine(v,s,left,right);
            if(v.slice.active&&v.slice.output>0) {
                const int route=outputRoutes[size_t(v.slice.output)];
                if(route>=0&&route+1<output.getNumChannels()) {
                    output.addSample(route,frame,float(left));output.addSample(route+1,frame,float(right));
                }
            } else {
                if(outL)* (outL+frame)+=float(left);
                if(outR)* (outR+frame)+=float(right);
            }
            if(v.slice.active)advanceSlice(v,s,v.increment);else advanceLoops(v,s,v.increment);
        }
    }
    // Free-running phase is slot-shared and advanced analytically once per render span.
    // Idle slots and ineffective LFOs do not do waveform or phase work.
    for(int slot=0;slot<slotCount;++slot)if(activePerSlot[size_t(slot)]>0) {
        const auto& s=(*states)[size_t(slot)];
        for(int i=0;i<2;++i) {
            const auto& l=s.lfo[size_t(i)];
            bool wheelActive=false;
            if(l.wheel!=0)for(const auto& v:voices)if(v.active&&v.slotIndex==slot&&wheel[size_t(v.channel)]!=0){wheelActive=true;break;}
            if(!l.trigger&&!l.oneShot&&(l.active||wheelActive))
                freePhase[size_t(slot)][size_t(i)]=wrap(freePhase[size_t(slot)][size_t(i)]+count*l.rate*(l.sync?bpm:1),1);
        }
    }
}
