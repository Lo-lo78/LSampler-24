#include "SlotAudioState.h"
#include <cmath>
namespace lsampler {
namespace {
constexpr double pi = 3.14159265358979323846;
double db(double x) { return std::pow(10.0,x/20.0); }
EnvelopeSettings envelope(double a,double d,double s,double r,double sr,bool filter) {
    return {1/std::max(1.0,a*sr),filter?(1-s)/std::max(1.0,d*sr):std::pow(.001,1/std::max(1.0,d*sr)),s,
        filter?std::exp(-1/std::max(1.0,r*sr)):std::pow(.001,1/std::max(1.0,r*sr)),filter};
}
}
FilterCoefficients filterCoefficients(bool hp,double hz,double resonance,double sr) noexcept {
    FilterCoefficients c;
    hz=std::clamp(hz,20.0,std::min(10000.0,sr*.45));
    if (hp) { c.f=std::clamp(2*std::sin(pi*hz/sr),.000001,1.99); c.q=2-std::min(.99,resonance)*1.9; }
    else { const double g=std::clamp(std::tan(pi*hz/sr),.000001,20.0), k=1/(.707+std::min(.99,resonance)*9.293);
        c.a1=1/(1+g*(g+k)); c.a2=g*c.a1;c.a3=g*c.a2; }
    return c;
}
ThresholdWindow calculateThresholdWindow(const SlotParameters& p, SharedSample* sample) noexcept {
    ThresholdWindow w;
    if (!sample || sample->audio.getNumSamples() < 2) return w;

    const auto& audio = sample->audio;
    const int n = audio.getNumSamples();
    const int visibleStart = std::clamp(int(std::floor(n * p[P::sample_start] * .01)), 0, n - 2);
    const int visibleEnd = std::clamp(int(std::floor(n * p[P::sample_end] * .01)), visibleStart + 1, n);
    int effectiveStart = visibleStart;
    int effectiveEnd = visibleEnd;

    const double startDb = p[P::start_threshold];
    if (startDb > -119.9) {
        const double threshold = std::pow(10.0, startDb / 20.0);
        bool found = false;
        for (int i = visibleStart; i < visibleEnd && !found; ++i) {
            double level = 0.0;
            for (int ch = 0; ch < std::min(2, audio.getNumChannels()); ++ch)
                level = std::max(level, std::abs(double(audio.getSample(ch, i))));
            if (level >= threshold) { effectiveStart = i; found = true; }
        }
    }

    const double endDb = p[P::end_threshold];
    if (endDb > -119.9 && visibleEnd > effectiveStart + 1) {
        const double threshold = std::pow(10.0, endDb / 20.0);
        const double sourceRate = sample->sourceSampleRate > 1.0 ? sample->sourceSampleRate : 44100.0;
        int holdFrames = std::max(16, int(std::floor(0.065 * sourceRate)));
        holdFrames = std::min(holdFrames, visibleEnd - effectiveStart);
        bool seenHot = false;
        int silentCount = 0;
        int firstSilent = -1;
        for (int i = effectiveStart; i < visibleEnd && effectiveEnd == visibleEnd; ++i) {
            double level = 0.0;
            for (int ch = 0; ch < std::min(2, audio.getNumChannels()); ++ch)
                level = std::max(level, std::abs(double(audio.getSample(ch, i))));
            if (level >= threshold) {
                seenHot = true; silentCount = 0; firstSilent = -1;
            } else if (seenHot) {
                if (firstSilent < 0) firstSilent = i;
                if (++silentCount >= holdFrames) effectiveEnd = firstSilent;
            }
        }
    }

    effectiveStart = std::clamp(effectiveStart, visibleStart, visibleEnd - 1);
    effectiveEnd = std::clamp(effectiveEnd, effectiveStart + 1, visibleEnd);
    w.start = effectiveStart; w.end = effectiveEnd;
    return w;
}

SlotAudioState prepareSlotAudioState(const SlotParameters& p,SharedSample* sample,double sr,uint64_t revision,
                                    int effectiveStart,int effectiveEnd) {
    SlotAudioState s; s.params=p;s.sample=sample;s.sampleRate=sr;s.revision=revision;
    auto v=[&](P key){return p[key];};
    auto ms=[&](P key){return v(key)*.001*sr;};
    auto coeff=[&](P key){return std::exp(-1/std::max(1.0,ms(key)));};
    const double sourceRate=sample?sample->sourceSampleRate:sr;
    const double sourceMs=sourceRate*.001;
    s.sourceRatio=sourceRate/sr;
    if (sample && sample->audio.getNumSamples()>=2) {
        const int n=sample->audio.getNumSamples();
        if (effectiveStart < 0 || effectiveEnd <= effectiveStart) {
            const auto threshold = calculateThresholdWindow(p, sample);
            effectiveStart = threshold.start;
            effectiveEnd = threshold.end;
        }
        s.start=std::clamp(effectiveStart,0,n-2);
        s.length=std::clamp(effectiveEnd,s.start+1,n)-s.start;
    }
    s.level=v(P::input_gain)<=-119.9?0:db(v(P::input_gain));
    s.panL=1-std::max(0.0,v(P::pan)*.01);s.panR=1+std::min(0.0,v(P::pan)*.01);
    if (v(P::normalize_on) && sample) s.normalize=std::min(24.0,db(v(P::normalize_target))/std::max(1e-9,sample->peak));
    const double frames=sample?sample->audio.getNumSamples():0;
    s.fadeIn=std::min(frames,std::floor(v(P::ram_fade_in)*sourceMs));s.fadeOut=std::min(frames,std::floor(v(P::ram_fade_out)*sourceMs));
    s.edgeFade=std::min(s.length*.5,v(P::start_end_fade)*sourceMs);
    s.delayL=v(P::stereo_delay_left)*sourceMs;s.delayR=v(P::stereo_delay_right)*sourceMs;
    s.stretchFactor=v(P::stretch_amount)>=0?1+v(P::stretch_amount):1/(1-v(P::stretch_amount));
    s.grain=std::clamp(std::floor(sourceRate/v(P::stretch_frequency)+.5),16.0,8192.0);
    const int ds=int(v(P::ram_downsample));
    if (ds) {
        constexpr double rates[]{0,32000,22050,22050,12000,12000,11025,11025,8000,8000,8000};
        constexpr int bits[]{0,16,12,8,8,4,12,8,12,8,4};
        s.downsampleHold=std::max(1,int(std::floor(sourceRate/rates[ds]+.5)));
        s.downsampleScale=std::pow(2.0,bits[ds]-1)-1;
    }
    s.portamento=1/std::max(1.0,ms(P::portamento));s.smooth=1/std::max(1.0,ms(P::retrigger_smooth));
    s.amp=envelope(v(P::attack),v(P::decay),v(P::sustain),v(P::release),sr,false);
    s.lp=v(P::lp_on)!=0;s.hp=v(P::hp_on)!=0;
    {
        s.lpEnv=envelope(v(P::lp_env_attack),v(P::lp_env_decay),v(P::lp_env_sustain),v(P::lp_env_release),sr,true);
        s.lpHz=20*std::pow(500,v(P::lp_cutoff));s.lpStatic=filterCoefficients(false,s.lpHz,v(P::lp_resonance),sr);
    }
    {
        s.hpEnv=envelope(v(P::hp_env_attack),v(P::hp_env_decay),v(P::hp_env_sustain),v(P::hp_env_release),sr,true);
        s.hpHz=20*std::pow(500,v(P::hp_cutoff));s.hpStatic=filterCoefficients(true,s.hpHz,v(P::hp_resonance),sr);
    }
    for(int i=0;i<2;++i) {
        auto& l=s.lfo[size_t(i)];
        const int base=int(i?P::lfo2_rate:P::lfo1_rate);
        auto f=[&](int k){return p.values[size_t(base+k)];};
        l.sync=f(2)!=0;l.trigger=f(1)!=0;l.oneShot=f(3)!=0;l.wave=int(f(4));
        l.rate=l.sync?std::max(.125,f(0))/(240*sr):f(0)/sr;
        l.delay=f(5)*sr;l.smoothing=1-std::min(.999,f(6));
        l.volume=f(7);l.pan=f(8);l.lp=s.lp?f(9):0;l.hp=s.hp?f(10):0;l.pitch=f(11);
        l.wheel=i?v(P::lfo2_mod_pitch_depth):0;l.move=i?v(P::lfo2_sample_depth):v(P::lfo1_sample_depth);
        l.active=l.volume!=0||l.pan!=0||l.lp!=0||l.hp!=0||l.pitch!=0||l.move!=0;
    }
    s.drive=v(P::drive_type)>0&&v(P::drive_amount)>0;
    s.comp=v(P::comp_on)!=0&&(v(P::comp_mix)>0||v(P::comp_delta)!=0);
    if(s.comp) { s.compAttack=coeff(P::comp_attack);s.compRelease=coeff(P::comp_release);s.compThreshold=db(v(P::comp_threshold));
        s.compExponent=1/v(P::comp_ratio)-1;s.compMakeup=db(v(P::comp_makeup));s.compMix=v(P::comp_mix)*.01; }
    s.gate=v(P::gate_on)!=0&&v(P::gate_depth)>0&&(v(P::gate_mix)>0||v(P::gate_delta)!=0);
    if(s.gate) {s.gateAttack=coeff(P::gate_attack);s.gateRelease=coeff(P::gate_release);s.gateThreshold=db(v(P::gate_threshold));
        s.gateClosed=db(-v(P::gate_depth));s.gateHold=ms(P::gate_hold);s.gateMix=v(P::gate_mix)*.01;}
    s.transient=v(P::transient_shape)!=0&&v(P::transient_mix)>0;
    if(s.transient) {s.transFast=std::exp(-1/(std::max(.25,v(P::transient_speed)*.25)*.001*sr));
        s.transSlow=std::exp(-1/(std::max(1.0,v(P::transient_speed)*4)*.001*sr));s.transAmount=v(P::transient_shape)*.01;s.transMix=v(P::transient_mix)*.01;}
    s.degrade=v(P::degrade_amount)>0;
    if(s.degrade) {s.degradeAmount=v(P::degrade_amount)*.01;
        s.degradeScale=std::pow(2.0,std::floor(16-(16-v(P::degrade_bits))*s.degradeAmount+.5)-1);
        s.degradeHold=int(std::floor(1+(v(P::degrade_hold)-1)*s.degradeAmount+.5));
        s.degradeJitter=int(std::floor(s.degradeHold*v(P::degrade_jitter)*.01));}
    s.ring=v(P::ring_mode)>0&&v(P::ring_amount)>0;s.fm=v(P::fm_amount)>0;
    s.machine=v(P::machine_character)>0&&v(P::character_depth)>0;
    if(s.machine) {
        constexpr double models[8][6]{{},{.02,0,.02,0,0,.04},{.45,.42,.14,.18,.05,.30},{.35,.50,.20,.24,.20,.34},
            {.30,.25,.10,.14,.08,.24},{.22,.22,.18,.22,.24,.26},{.60,.72,.42,.50,.20,.22},{.10,.04,.34,.44,.60,.16}};
        for(int i=0;i<6;++i) s.character[size_t(i)]=std::min(i==1?1.0:4.0,v(P::character_depth)*.002*(models[int(v(P::machine_character))][i]+p.values[size_t(int(P::character_input_drive)+i)]*.0002));
        s.characterScale=std::pow(2.0,std::floor(16-s.character[1]*13+.5)-1);s.characterSlew=1-std::min(.995,s.character[1]*.72);
        s.characterAir=.0015+s.character[4]*.025;s.characterRelease=std::exp(-1/(.180*sr));
    }
    // Sort stage boundaries once per publication, never scan ten memories per audio sample.
    bool any=false;
    for(int i=0;i<loopCount;++i) for(int k=0;k<loopParameterCount;++k)
        any|=p.loops[size_t(i)][size_t(k)]!=loopParameters[size_t(k)].initial;
    if(any && s.length>0) {
        for(int i=0;i<loopCount;++i) {
            bool active=i==0;
            for(int k=0;k<loopParameterCount;++k) active|=p.loops[size_t(i)][size_t(k)]!=loopParameters[size_t(k)].initial;
            if(!active)continue;
            auto& l=s.stages[size_t(s.stageCount++)];l.memory=i;
            l.start=std::min(double(s.length-1),std::floor(s.length*p.loop(i,L::start)*.01));
            l.end=std::clamp(std::floor(s.length*p.loop(i,L::end)*.01),l.start+1,double(s.length));
            l.repeats=int(p.loop(i,L::repeats));l.fadeIn=p.loop(i,L::fade_in)*sourceMs;l.fadeOut=p.loop(i,L::fade_out)*sourceMs;
            l.pitch=p.loop(i,L::pitch_down);l.lp=p.loop(i,L::lp_down);l.hp=p.loop(i,L::hp_down);l.oneShot=p.loop(i,L::one_shot)!=0;
        }
        std::sort(s.stages.begin(),s.stages.begin()+s.stageCount,[](const auto& a,const auto& b){return a.start<b.start||(a.start==b.start&&a.memory<b.memory);});
        for(int i=0;i+1<s.stageCount;++i) if(s.stages[size_t(i+1)].start>s.stages[size_t(i)].start)
            s.stages[size_t(i)].end=std::min(s.stages[size_t(i)].end,s.stages[size_t(i+1)].start);
        for(int i=0;i<s.stageCount;++i) {
            auto& loop=s.stages[size_t(i)];
            loop.fadeIn=std::min(loop.fadeIn,(loop.end-loop.start)*.5);
            loop.fadeOut=std::min(loop.fadeOut,(loop.end-loop.start)*.5);
        }
    }
    s.crossfade=v(P::loop_crossfade)*sourceMs;
    s.effectsNeeded = v(P::ram_swap_lr)!=0 || v(P::ram_stereo_width)!=100 || v(P::normalize_on)!=0
        || s.transient || s.drive || s.comp || s.gate || s.degrade || s.ring;
    // Selection is prepared off the audio thread. Dynamic Slice/LFO/wheel state
    // is checked again at each render span (MIDI events split render spans).
    s.simpleVoicePath = !s.effectsNeeded && !s.lp && !s.hp && !s.fm && !s.machine
        && v(P::ram_reverse)==0 && v(P::ram_downsample)==0 && v(P::dc_remove)==0
        && s.fadeIn<=1 && s.fadeOut<=1 && s.delayL==0 && s.delayR==0
        && !std::signbit(s.delayL) && !std::signbit(s.delayR)
        && v(P::stretch_amount)==0 && s.crossfade<=1 && s.edgeFade<=0
        && v(P::pan_env)==0 && v(P::retrigger_smooth)==0;
    for(int i=0;i<s.stageCount && s.simpleVoicePath;++i)
        if(s.stages[size_t(i)].fadeIn>0 || s.stages[size_t(i)].fadeOut>0) s.simpleVoicePath=false;
    return s;
}
} // namespace lsampler
