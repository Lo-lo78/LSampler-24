#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "SlotAudioState.h"
#include "SliceEngine.h"
#include <array>

class GlobalVoicePool
{
public:
    static constexpr int voiceCount = 96, slotCount = 24;
    ~GlobalVoicePool() { allNotesOff(); }
    void prepare(double sampleRate);
    void setStates(const std::array<lsampler::SlotAudioState, slotCount>* states) noexcept;
    void setTempo(double tempo) noexcept { bpm = tempo; }
    void setOutputRoutes(const std::array<int,25>& routes) noexcept { outputRoutes=routes; }
    void noteOn(int slotIndex,int note,float velocity,int channel=0,bool preview=false,double previewStartPercent=-1.0,int sliceRequest=-1,bool sliceMidiPad=false);
    void noteOff(int note,int channel=0);
    void controller(int channel,int number,int value);
    void pitchBend(int channel,int value) noexcept;
    void choke(int note);
    void allNotesOff();
    void stopSlotVoices(int slotIndex);
    void stopPreviewVoices(int slotIndex=-1);
    bool hasPreviewVoices(int slotIndex=-1) const noexcept;
    int activeVoiceCount() const noexcept;
    int activeVoiceCount(int slot) const noexcept;
    void render(juce::AudioBuffer<float>& output,int start,int count);
    // Counters count actual optional module calls; useful for bypass regression tests.
    struct Diagnostics { uint64_t lp=0,hp=0,lfo=0,ring=0,fm=0,drive=0,comp=0,gate=0,transient=0,degrade=0,machine=0; } diagnostics;
private:
    struct Envelope {
        double value=0; int phase=1;
        double tick(const lsampler::EnvelopeSettings&) noexcept;
        void release() noexcept { if(phase!=0)phase=4; }
    };
    struct Lfo { double phase=0,output=0,hold=0; uint64_t age=0; };
    struct Filter { double band[2]{},low[2]{}; };
    struct Voice {
        lsampler::SlicePlayback slice;
        bool active=false,preview=false,keyDown=true,sustained=false,loopsReleased=false;
        int slotIndex=-1,note=-1,channel=0,stage=-1,repeat=0,skipStage=-1;
        uint64_t age=0,revision=0;
        SharedSample* sample=nullptr;
        double position=0,effectiveNote=60,velocity=1,velocityGain=1,velocitySmooth=1,drift=0;
        double pitchCached=1e30,increment=1,ampSmooth=0,followCached=1e30,ringIncrement=0,fmIncrement=0;
        double lpBase=10000,hpBase=20,lpModCached=1e30,hpModCached=1e30;
        lsampler::FilterCoefficients lpCoefficients,hpCoefficients;
        Envelope amp,lpEnvelope,hpEnvelope;
        std::array<Lfo,2> lfo;
        Filter lp,hp;
        double ringPhase=0,fmPhase=0,fmFeedback=0;
        int degradeCount=0;
        double heldL=0,heldR=0,compEnv=0,gateEnv=0,gateGain=1,gateHold=0,transFast=0,transSlow=0;
        double characterPhase=0,characterPhase2=0,characterConverter[2]{},characterAir[2]{},characterDelay[2]{},characterGlue=0;
        uint32_t randomState=1;
    };
    struct HeldNote { uint64_t order=0; float velocity=0; };
    void stop(Voice&) noexcept;
    void release(Voice&) noexcept;
    Voice& chooseVoice(int slot,int cap);
    void updateVoice(Voice&,const lsampler::SlotAudioState&) noexcept;
    void retarget(Voice&,int note,double velocity,const lsampler::SlotAudioState&,bool retrigger);
    double random(Voice&) noexcept;
    double oscillator(Voice&,double phase,int wave,bool lfo=false) noexcept;
    double lfoTick(Voice&,int index,const lsampler::LfoSettings&,double wheel) noexcept;
    double read(const Voice&,const lsampler::SlotAudioState&,double position,int channel) const noexcept;
    void processEffects(Voice&,const lsampler::SlotAudioState&,double& left,double& right);
    void applyMachine(Voice&,const lsampler::SlotAudioState&,double& left,double& right) noexcept;
    void advanceSlice(Voice&,const lsampler::SlotAudioState&,double) noexcept;
    void advanceLoops(Voice&,const lsampler::SlotAudioState&,double increment) noexcept;
    void selectStage(Voice&,const lsampler::SlotAudioState&) noexcept;
    std::array<Voice,voiceCount> voices {};
    std::array<std::array<HeldNote,128>,16> held {};
    std::array<bool,16> pedal {};
    std::array<double,16> bend {},wheel {};
    std::array<std::array<double,2>,slotCount> freePhase {};
    std::array<int,slotCount> activePerSlot {};
    std::array<int,25> outputRoutes {};
    const std::array<lsampler::SlotAudioState,slotCount>* states=nullptr;
    double hostSampleRate=44100,bpm=120;
    uint64_t ageCounter=0,noteCounter=0;
};
