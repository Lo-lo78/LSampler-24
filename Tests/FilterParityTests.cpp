#include "../Source/SamplerVoice.h"
#include "ReferenceTEST59/SamplerVoice.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Count heap operations only during the actual audio render, on this thread.
thread_local bool audioGuard=false;
thread_local uint64_t audioAllocations=0,audioDeletes=0;
void* operator new(std::size_t n) {if(audioGuard)++audioAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(audioGuard&&p)++audioDeletes;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

using namespace lsampler;
namespace {
struct Guard { Guard(){audioGuard=true;} ~Guard(){audioGuard=false;} };
void check(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}

struct Case {
    std::string name;
    double sampleRate=48000;
    int blockSize=128,voices=48,filters=1,wave=0,feature=0;
    bool varied=false,slice=false,mono=false,events=true,allLfoDestinations=false;
};
struct Scene {
    // Owners outlive both pools: pools are local to the comparison functions.
    std::array<SharedSample,2> samples;
    std::unique_ptr<std::array<SlotAudioState,24>> states=std::make_unique<std::array<SlotAudioState,24>>();
    std::array<SlotParameters,2> params;
    double rate;
    uint64_t revision=0;
    explicit Scene(const Case& c):rate(c.sampleRate) {
        for(int slot=0;slot<2;++slot) {
            auto& a=samples[size_t(slot)];a.sourceSampleRate=44100;a.peak=.4;
            a.audio.setSize(c.mono?1:2,4096);
            for(int ch=0;ch<a.audio.getNumChannels();++ch)for(int i=0;i<4096;++i) {
                const int period=slot==0?127:131;
                const double phase=double((i+ch*17)%period)/period;
                const float value=slot==0?(phase<.5?.4f:-.4f):float(.8*phase-.4);
                a.audio.setSample(ch,i,value);
            }
            auto& p=params[size_t(slot)];
            p[P::global_one_shot]=0;p[P::slot_polyphony]=0;p[P::input_gain]=-24;
            p[P::pan]=slot==0?-80:80;p[P::release]=4;p[P::sustain_pedal]=1;
            p[P::lp_on]=(c.filters&1)?1:0;p[P::hp_on]=(c.filters&2)?1:0;
            p[P::lp_cutoff]=.68;p[P::hp_cutoff]=.18;
            p[P::lp_resonance]=.43;p[P::hp_resonance]=.22;
            p[P::lfo1_rate]=1.31;p[P::lfo2_rate]=.73;
            p[P::lfo1_wave]=c.wave;p[P::lfo2_wave]=(c.wave+1)%6;
            p[P::lfo1_lp_depth]=11;p[P::lfo2_lp_depth]=-7;
            p[P::lfo1_hp_depth]=5;p[P::lfo2_hp_depth]=9;
            p[P::lfo1_mode]=1;p[P::lfo2_mode]=1;
            if(c.varied) {
                p[P::lp_vel_amount]=.55;p[P::hp_vel_amount]=-.3;
                p[P::lp_key_follow]=.4;p[P::lp_env_amount]=17;p[P::hp_env_amount]=-9;
                p[P::lp_env_attack]=.08;p[P::lp_env_decay]=.3;
                p[P::hp_env_attack]=.07;p[P::hp_env_decay]=.2;
                p[P::lfo1_mode]=0;p[P::lfo2_mode]=0;
                p[P::lfo1_smoothing]=.17;p[P::lfo2_smoothing]=.37;
                p.loops[0][size_t(L::end)]=60;p.loops[0][size_t(L::repeats)]=5;
                p.loops[0][size_t(L::lp_down)]=.4;p.loops[1][size_t(L::start)]=60;
            }
            if(c.allLfoDestinations) {
                p[P::lfo1_volume_depth]=.3;p[P::lfo2_pan_depth]=.6;
                p[P::lfo1_pitch_depth]=3;p[P::lfo2_mod_pitch_depth]=5;
                p[P::lfo1_sample_depth]=2;p[P::lfo2_sample_depth]=-1;
                p[P::lfo1_delay]=.03;p[P::lfo2_one_shot]=1;
                p[P::sample_start]=11;p[P::sample_end]=79;p[P::sample_play_start]=35;
                p[P::output_route]=slot+1;
            }
        }
        publish(c);
    }
    void publish(const Case& c) {
        for(int slot=0;slot<2;++slot) {
            auto& state=(*states)[size_t(slot)];
            state=prepareSlotAudioState(params[size_t(slot)],&samples[size_t(slot)],rate,++revision);
            SliceState slice;
            if(c.slice) {
                slice[SliceG::mode]=7;slice.setDivision(3);
                slice.steps[0][SliceP::repeatLP]=-5;slice.steps[1][SliceP::repeatHP]=3;
                slice.steps[0][SliceP::repeat]=4;slice.steps[2][SliceP::output]=2;
                slice.steps[3][SliceP::pitch]=7;
            }
            state.slice=prepareSliceAudio(slice,state.length);
        }
    }
};
template<class Pool> void setup(Pool& pool,Scene& scene,const Case& c) {
    pool.prepare(c.sampleRate);pool.setStates(scene.states.get());pool.setTempo(123);
    std::array<int,25> routes;for(int bus=0;bus<25;++bus)routes[size_t(bus)]=2*bus;
    pool.setOutputRoutes(routes);
    for(int voice=0;voice<c.voices;++voice) {
        const int note=36+(voice/2)%48;
        const float velocity=c.varied?(.25f+float(voice%7)*.1f):.8f;
        pool.noteOn(voice%2,note,velocity,voice%4);
    }
}
template<class Pool> void midiEvents(Pool& pool,int block,bool second) {
    if(!second) {
        if(block==1)for(int ch=0;ch<4;++ch)pool.controller(ch,64,127);
        if(block==2)for(int ch=0;ch<4;++ch)for(int note=36;note<84;++note)pool.noteOff(note,ch);
        if(block==3)for(int ch=0;ch<4;++ch)pool.controller(ch,64,0);
        if(block==4)for(int ch=0;ch<4;++ch)pool.controller(ch,1,96);
        if(block==7)for(int ch=0;ch<4;++ch)pool.controller(ch,1,0);
        if(block==9)pool.setTempo(97);
        if(block==20)pool.noteOn(1,71,.42f,0);
        if(block==27)pool.controller(0,120,0);
        if(block==31)pool.allNotesOff();
        if(block==32)pool.noteOn(0,61,.91f,0,true);
        if(block==35)pool.stopPreviewVoices();
    } else {
        if(block==5)pool.pitchBend(0,2307);
        if(block==8)pool.pitchBend(0,0);
        if(block==21)pool.noteOff(71,0);
    }
}
void setExtraFeature(SlotParameters& p,int feature,bool enabled) {
    const SlotParameters defaults;
    auto set=[&](P key,double value){p[key]=enabled?value:defaults[key];};
    switch(feature) {
        case 1:set(P::ram_reverse,1);break;
        case 2:set(P::ram_downsample,4);break;
        case 3:set(P::dc_remove,1);break;
        case 4:set(P::ram_fade_in,18);break;
        case 5:set(P::ram_fade_out,18);break;
        case 6:set(P::stereo_delay_left,2);break;
        case 7:set(P::stereo_delay_right,3);break;
        case 8:set(P::stretch_amount,.4);break;
        case 9:set(P::stretch_amount,-.4);break;
        case 10:set(P::loop_crossfade,2);break;
        case 11:set(P::start_end_fade,2);break;
        case 12:set(P::pan_env,35);break;
        case 13:set(P::retrigger_smooth,4);break;
        case 14:p.loops[0][size_t(L::fade_in)]=enabled?2:0;break;
        case 15:set(P::drive_type,1);set(P::drive_amount,30);break;
        case 16:set(P::fm_amount,2);break;
        case 17:set(P::machine_character,2);set(P::character_depth,40);break;
        case 18:set(P::ram_swap_lr,1);break;
        case 19:set(P::ram_stereo_width,70);break;
        case 20:set(P::normalize_on,1);break;
        case 21:set(P::comp_on,1);set(P::comp_mix,65);break;
        case 22:set(P::gate_on,1);set(P::gate_depth,30);set(P::gate_mix,75);break;
        case 23:set(P::transient_shape,25);set(P::transient_mix,70);break;
        case 24:set(P::degrade_amount,20);break;
        case 25:set(P::ring_mode,1);set(P::ring_amount,20);break;
        case 26:p.loops[0][size_t(L::fade_out)]=enabled?2:0;break;
        default:break;
    }
}
void stateEvents(Scene& scene,const Case& c,int block) {
    if(c.feature!=0 && (block==16||block==18)) {
        for(auto& p:scene.params)setExtraFeature(p,c.feature,block==16);
        scene.publish(c);
    }
    if(block==10) {
        for(auto& p:scene.params){p[P::lp_cutoff]=.31;p[P::hp_cutoff]=.26;p[P::lp_resonance]=.71;p[P::hp_resonance]=.37;}
        scene.publish(c);
    }
    if(block==12) {for(auto& p:scene.params){p[P::lp_on]=0;p[P::hp_on]=0;}scene.publish(c);}
    if(block==13) {for(auto& p:scene.params){p[P::lp_on]=(c.filters&1)?1:0;p[P::hp_on]=(c.filters&2)?1:0;}scene.publish(c);}
    if(block==15) {
        for(auto& p:scene.params){p[P::lfo1_bpm_sync]=1;p[P::lfo2_bpm_sync]=1;p[P::lfo1_rate]=4;p[P::lfo2_rate]=3;}
        scene.publish(c);
    }
}
struct Metrics {uint64_t samples=0,specialised=0,general=0,plain=0;};
Metrics compare(const Case& c,std::ofstream& report) {
    Scene scene(c);
    auto baseline=std::make_unique<Test59VoicePool>();auto candidate=std::make_unique<GlobalVoicePool>();
    setup(*baseline,scene,c);setup(*candidate,scene,c);
    check(baseline->activeVoiceCount()==c.voices&&candidate->activeVoiceCount()==c.voices,"Voice count at start: "+c.name);
    juce::AudioBuffer<float> a(50,c.blockSize+16),b(50,c.blockSize+16);
    Metrics result;
    for(int block=0;block<40;++block) {
        if(c.events) {
            stateEvents(scene,c,block);baseline->setStates(scene.states.get());candidate->setStates(scene.states.get());
            midiEvents(*baseline,block,false);midiEvents(*candidate,block,false);
        }
        if(c.events && block==36) {
            // Check re-preparation and path selection after a sample-rate change.
            Case changed=c;changed.sampleRate=c.sampleRate==96000?44100:96000;
            result.specialised+=candidate->diagnostics.filterSpans;result.general+=candidate->diagnostics.generalSpans;result.plain+=candidate->diagnostics.simpleSpans;
            scene.rate=changed.sampleRate;scene.publish(changed);
            setup(*baseline,scene,changed);setup(*candidate,scene,changed);
        }
        a.clear();b.clear();const int offset=7,first=c.blockSize/3;
        {Guard guard;baseline->render(a,offset,first);candidate->render(b,offset,first);}
        if(c.events){midiEvents(*baseline,block,true);midiEvents(*candidate,block,true);}
        {Guard guard;baseline->render(a,offset+first,c.blockSize-first);candidate->render(b,offset+first,c.blockSize-first);}
        for(int ch=0;ch<50;++ch)for(int i=0;i<a.getNumSamples();++i) {
            const float x=a.getSample(ch,i),y=b.getSample(ch,i);++result.samples;
            if(!std::isfinite(x)||!std::isfinite(y)||std::memcmp(&x,&y,sizeof(float))!=0) {
                report<<"FAIL "<<c.name<<" block="<<block<<" channel="<<ch<<" frame="<<i
                      <<" TEST59="<<std::setprecision(17)<<x<<" candidate="<<y<<"\n";report.flush();
                throw std::runtime_error("Audio difference (including sign of zero) or non-finite output: "+c.name);
            }
        }
        check(baseline->activeVoiceCount()==candidate->activeVoiceCount(),"Voice retirement: "+c.name);
        check(baseline->hasPreviewVoices()==candidate->hasPreviewVoices(),"Preview state: "+c.name);
        const auto& x=baseline->diagnostics;const auto& y=candidate->diagnostics;
        check(x.lp==y.lp&&x.hp==y.hp&&x.lfo==y.lfo&&x.ring==y.ring&&x.fm==y.fm
              &&x.drive==y.drive&&x.comp==y.comp&&x.gate==y.gate&&x.transient==y.transient
              &&x.degrade==y.degrade&&x.machine==y.machine,"DSP operation counts: "+c.name);
    }
    const auto& y=candidate->diagnostics;
    result.specialised+=y.filterSpans;result.general+=y.generalSpans;result.plain+=y.simpleSpans;
    if(c.feature!=0)check(result.specialised>0&&result.general>0,"Fast/general transition not exercised: "+c.name);
    if(c.allLfoDestinations)check(result.specialised>0,"LFO destinations must exercise specialised path: "+c.name);
    report<<"PASS "<<c.name<<" | exact float bits: "<<result.samples<<" samples | specialised spans="
          <<result.specialised<<", general="<<result.general<<", plain="<<result.plain<<"\n";report.flush();return result;
}
// Keep timing separate from parity: no comparing/reporting/allocating in timed spans.
volatile double benchmarkSink=0;
template<class Pool> double measure(const Case& c) {
    Scene scene(c);auto pool=std::make_unique<Pool>();setup(*pool,scene,c);
    juce::AudioBuffer<float> output(50,c.blockSize);
    for(int block=0;block<16;++block){output.clear();pool->render(output,0,c.blockSize);}
    for(int ch=0;ch<4;++ch)for(int note=36;note<84;++note)pool->noteOff(note,ch);
    const auto begin=std::chrono::steady_clock::now();
    for(int block=0;block<96;++block){output.clear();pool->render(output,0,c.blockSize);}
    const auto end=std::chrono::steady_clock::now();
    benchmarkSink=double(output.getMagnitude(0,c.blockSize))+pool->activeVoiceCount();
    return std::chrono::duration<double,std::milli>(end-begin).count();
}
double median(std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];}
void benchmark(const Case& c,std::ofstream& csv,std::ofstream& report) {
    std::vector<double> oldTimes,newTimes;
    for(int trial=0;trial<5;++trial) {
        double oldTime,newTime;
        if(trial%2==0){oldTime=measure<Test59VoicePool>(c);newTime=measure<GlobalVoicePool>(c);}
        else {newTime=measure<GlobalVoicePool>(c);oldTime=measure<Test59VoicePool>(c);}
        oldTimes.push_back(oldTime);newTimes.push_back(newTime);
        csv<<c.name<<','<<c.sampleRate<<','<<c.blockSize<<','<<c.voices<<','<<trial<<','
           <<std::setprecision(10)<<oldTime<<','<<newTime<<'\n';
    }
    const double oldMedian=median(oldTimes),newMedian=median(newTimes);
    report<<"BENCH "<<c.name<<" | TEST59 median="<<oldMedian<<" ms | candidate median="<<newMedian
          <<" ms | speedup="<<oldMedian/newMedian<<"x | time reduction="<<(1-newMedian/oldMedian)*100<<"%\n";
    if(newMedian>=oldMedian)report<<"NOTE: no speed gain measured for "<<c.name<<"; retain TEST59 for this workload.\n";
    report.flush();csv.flush();
}
}
int main(int argc,char** argv) {
    const auto directory=argc>1?juce::File(juce::String::fromUTF8(argv[1])):juce::File::getCurrentWorkingDirectory().getChildFile("filter-reports");
    directory.createDirectory();
    std::ofstream report(directory.getChildFile("FILTER_TEST61_REPORT.txt").getFullPathName().toStdString());
    std::ofstream csv(directory.getChildFile("FILTER_TEST61_BENCHMARK.csv").getFullPathName().toStdString());
    try {
        check(report.good()&&csv.good(),"Cannot create report files");
        report<<"LSampler TEST61 vs frozen TEST59: specialised filters/LFO renderer\n"
              <<"Exact sample comparison, all 50 channels; no error tolerance.\n"
              <<"Timings cover voice rendering, not REAPER GUI, driver or plugin output stage.\n"
              <<"Warm-up, five trials, alternating order; median; CI CPU is variable.\n\n";
        csv<<"scenario,sample_rate,block_size,voices,trial,test59_ms,candidate_ms\n";
        uint64_t comparisons=0,specialised=0,general=0,plain=0;int cases=0;
        for(double rate:{44100.,48000.,96000.})for(int filters:{1,2,3}) {
            Case c;c.sampleRate=rate;c.filters=filters;c.voices=96;
            c.blockSize=filters==1?64:filters==2?127:512;
            c.name="rate"+std::to_string(int(rate))+"_filters"+std::to_string(filters)+"_96voices";
            auto m=compare(c,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        }
        for(int wave=0;wave<6;++wave) {
            Case c;c.wave=wave;c.filters=3;c.varied=true;c.mono=wave%2==0;c.blockSize=191;c.voices=24;
            c.name="varied_velocity_envelope_wave"+std::to_string(wave);
            auto m=compare(c,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        }
        for(int voices:{2,16,48,96}) {
            Case c;c.voices=voices;c.filters=3;c.blockSize=256;c.slice=true;
            c.name="slice_filters_voices"+std::to_string(voices);
            auto m=compare(c,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        }
        for(int feature=1;feature<=26;++feature) {
            Case c;c.filters=3;c.voices=16;c.feature=feature;c.blockSize=128;
            c.name="live_fast_general_transition_"+std::to_string(feature);
            auto m=compare(c,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        }
        for(int filters:{0,1,2,3}) {
            Case c;c.filters=filters;c.voices=24;c.allLfoDestinations=true;c.mono=filters%2==0;
            c.name="all_LFO_destinations_filters_"+std::to_string(filters);
            auto m=compare(c,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        }
        Case clean;clean.filters=0;clean.voices=96;clean.name="clean_TEST59_fast_path";
        auto m=compare(clean,report);comparisons+=m.samples;specialised+=m.specialised;general+=m.general;plain+=m.plain;++cases;
        check(specialised>0&&general>0&&plain>0,"Specialised, general and TEST59 plain paths must all be exercised");
        check(audioAllocations==0&&audioDeletes==0,"Heap allocation/deletion in audio render");
        report<<"\nAUDIO PARITY PASS: "<<cases<<" cases, "<<comparisons<<" float samples bit-identical.\n"
              <<"Guarded render allocations="<<audioAllocations<<", deletes="<<audioDeletes<<".\n"
              <<"Specialised spans="<<specialised<<", general="<<general<<", plain="<<plain<<".\n\n";
        for(int voices:{16,48,96}) {
            Case c;c.voices=voices;c.blockSize=256;c.events=false;
            c.name="LP_two_LFO_"+std::to_string(voices)+"voices";benchmark(c,csv,report);
        }
        Case varied;varied.voices=96;varied.filters=3;varied.varied=true;varied.blockSize=256;varied.events=false;
        varied.name="LP_HP_varied_modulation";benchmark(varied,csv,report);
        Case noFilters;noFilters.filters=0;noFilters.voices=96;noFilters.blockSize=256;noFilters.events=false;
        noFilters.name="clean_TEST59_fast_path";benchmark(noFilters,csv,report);
        report<<"\nPASS: TEST61 audio gate passed. Performance is reported, not asserted.\n";
        std::cout<<"PASS: audio bit-identical in "<<cases<<" cases. Read FILTER_TEST61_REPORT.txt for timings.\n";
        return 0;
    } catch(const std::exception& e) {
        audioGuard=false;report<<"\nFAIL: "<<e.what()<<"\nNo TEST61 VST3 should be published.\n";
        std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;
    }
}
