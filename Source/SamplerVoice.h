#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "SlotAudioState.h"
#include "SliceEngine.h"
#include "FreeLfoTransport.h"
#include <array>
#include <cstdint>

class GlobalVoicePool
{
public:
    static constexpr int voiceCount = 96, slotCount = 24;
    ~GlobalVoicePool() { allNotesOff(); }
    void prepare(double sampleRate);
    void setStates(const std::array<lsampler::SlotAudioState, slotCount>* states) noexcept;
    void setTempo(double tempo) noexcept { bpm = tempo; }
    // TEST170: reset Free LFOs only when the host starts a new Play.
    void restartFreeLfosOnTransportPlay() noexcept;
    void setOutputRoutes(const std::array<int,25>& routes) noexcept { outputRoutes=routes; }
    void noteOn(int slotIndex,int note,float velocity,int channel=0,bool preview=false,double previewStartPercent=-1.0,int sliceRequest=-1,bool sliceMidiPad=false,int previewSampleIndex=-1,int forcedSampleIndex=-1);
    void noteOff(int note,int channel=0);
    void controller(int channel,int number,int value);
    void pitchBend(int channel,int value) noexcept;
    void choke(int note, int channel=0);
    void allNotesOff();
    void stopSlotVoices(int slotIndex);
    void stopPreviewVoices(int slotIndex=-1);
    bool hasPreviewVoices(int slotIndex=-1) const noexcept;
    int activeVoiceCount() const noexcept;
    int activeVoiceCount(int slot) const noexcept;
    void render(juce::AudioBuffer<float>& output,int start,int count);
    // Optional three stereo buses, fed independently by slot (no allocations).
    void setFXSendContext(std::array<juce::AudioBuffer<float>*,3> buses,
        const std::array<std::array<float,3>,slotCount>* levels) noexcept
    { fxSendBuses = buses; fxSendLevels = levels; }
    // Offline comparison hook: the full scalar path, with read reuse disabled.
    // Not a plugin parameter, not a user-visible quality setting.
    void renderReferenceForTesting(juce::AudioBuffer<float>& output,int start,int count);
    // Counters count actual optional module calls; useful for bypass regression tests.
    struct Diagnostics { uint64_t lp=0,hp=0,lfo=0,ring=0,fm=0,drive=0,comp=0,gate=0,transient=0,degrade=0,machine=0,simpleSpans=0,sharedReadSpans=0,filterSpans=0,generalSpans=0; } diagnostics;
private:
    struct Envelope {
        double value=0; int phase=1;
        double tick(const lsampler::EnvelopeSettings&) noexcept;
        void release() noexcept { if(phase!=0)phase=4; }
    };
    struct Lfo { double phase=0,output=0,hold=0,previousAudible=0; uint64_t age=0,cycles=0; uint32_t freeRandomState=1; };
    struct Filter { double band[2]{},low[2]{}; };
    struct Voice {
        lsampler::SlicePlayback slice;
        bool active=false,preview=false,keyDown=true,sustained=false,loopsReleased=false,retiring=false;
        int fadeInLeft=0,fadeOutLeft=0,legatoPitchSlewLeft=0,legatoPitchSlewTotal=0,delayFramesRemaining=0;
        double mirrorSign=1.0,legatoFromIncrement=1.0;
        bool autoSliceHasLoops=false; // cached once per automatic source-slice event
        int slotIndex=-1,note=-1,channel=0,stage=-1,repeat=0,skipStage=-1,sampleIndex=0,loopPitchTimeRepeat=-1;
        int repeatLevelIndexCached=-1, repeatLevelStageCached=-1;
        uint64_t age=0,revision=0;
        SharedSample* sample=nullptr;
        double position=0,loopClock=0,loopPitchTimeFactor=1,loopPitchTimeAmount=0,loopLevelSmooth=1,repeatLevelGain=1,repeatLevelDbCached=1e30,effectiveNote=60,velocity=1,velocityGain=1,velocitySmooth=1,drift=0;
        double pitchCached=1e30,increment=1,ampSmooth=0,followCached=1e30,ringIncrement=0,fmIncrement=0;
        double lpBase=10000,hpBase=20,lpModCached=1e30,hpModCached=1e30;
        double hpCutoffParamCached=1e30,hpResonanceParamCached=1e30;
        bool hpConfigValid=false,hpProcessingLast=false;
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
    template<bool Optimised> void renderInternal(juce::AudioBuffer<float>&,int start,int count);
    template<bool Simple, bool Optimised, int FilterMask=-1> void renderVoice(Voice&,const lsampler::SlotAudioState&,
        juce::AudioBuffer<float>&,int start,int count,float* outL,float* outR,double mirror);
    struct HeldNote { uint64_t order=0; float velocity=0; };
    void stop(Voice&) noexcept;
    bool retireVoice(const Voice&) noexcept; // TEST166: de-click tail without taking a musical voice
    void release(Voice&) noexcept;
    Voice& chooseVoice(int slot,int cap);
    int chooseSampleIndex(int slot,const lsampler::SlotAudioState&,int velocity,bool preview) noexcept;
    void updateVoice(Voice&,const lsampler::SlotAudioState&) noexcept;
    void retarget(Voice&,int note,double velocity,const lsampler::SlotAudioState&,bool retrigger);
    double random(Voice&) noexcept;
    double oscillator(Voice&,double phase,int wave,bool lfo=false) noexcept;
    double lfoTick(Voice&,int index,const lsampler::LfoSettings&,double wheel,
                   double envelopeSource,double crossSource,double laceSource) noexcept;
    double lfoTickState(Lfo&,uint32_t& randomState,const lsampler::LfoSettings&,double phaseIncrement,
                        double envelopeSource,double crossSource,double laceSource) noexcept;
    double lfoTickPrepared(Voice&,int index,const lsampler::LfoSettings&,double phaseIncrement,
                           double envelopeSource,double crossSource,double laceSource) noexcept;
    double read(const Voice&,const lsampler::SlotAudioState&,double position,int channel) const noexcept;
    void processEffects(Voice&,const lsampler::SlotAudioState&,double& left,double& right);
    void applyMachine(Voice&,const lsampler::SlotAudioState&,double& left,double& right) noexcept;
    // Automatic Slice orders source regions; nested subloops retain their
    // original sample coordinates. Sequencer and single-slice auditions are
    // deliberately excluded.
    static bool usesAutoSliceLoops(const Voice& v) noexcept {
        return v.slice.active && !v.slice.single && v.slice.mode>=1 && v.slice.mode<=6;
    }
    void syncAutoSliceStage(Voice&,const lsampler::SlotAudioState&) noexcept;
    double autoSliceLoopPitchFactor(Voice&,const lsampler::LoopAudioState&) noexcept;
    void advanceSlice(Voice&,const lsampler::SlotAudioState&,double) noexcept;
    void advanceLoops(Voice&,const lsampler::SlotAudioState&,double increment) noexcept;
    void selectStage(Voice&,const lsampler::SlotAudioState&) noexcept;
    std::array<Voice,voiceCount> voices {};
    std::array<Voice,voiceCount> retiringVoices {}; // fixed-capacity, no audio-thread allocations
    int activeRetiringVoices=0, transitionSamples=144;
    std::array<std::array<HeldNote,128>,16> held {};
    std::array<bool,16> pedal {};
    std::array<double,16> bend {},wheel {};
    std::array<std::array<double,2>,slotCount> freePhase {};
    std::array<std::array<uint64_t,2>,slotCount> freeCycles {};
    // TEST161: independent common LFOs for each of the 24 slots. Scratch is
    // fixed-capacity; rendering splits unusually large blocks, never allocates.
    static constexpr int monoLfoChunk = 512;
    std::array<std::array<Lfo,2>,slotCount> monoLfo {};
    std::array<uint32_t,slotCount> monoLfoRandom {};
    // Free-mode S&H does not share its PRNG with Triggered LFOs, slices or noise.
    std::array<std::array<uint32_t,2>,slotCount> monoFreeLfoRandom {};
    // Before the first host Play, preserve the frozen TEST59 PRNG behaviour.
    // After Play, Free LFOs use their own resettable streams.
    bool hostFreeLfoTransportStarted = false;
    std::array<std::array<std::array<double,monoLfoChunk>,2>,slotCount> monoLfoSamples {};
    std::array<std::array<std::array<double,monoLfoChunk>,2>,slotCount> monoLfoCoreSamples {};
    std::array<std::array<std::array<double,monoLfoChunk>,2>,slotCount> monoLfoPreviousAudibleSamples {};
    void retriggerMonoLfos(int slot,const lsampler::SlotAudioState&) noexcept;
    void initialiseFreeVoiceLfo(Voice&,int index) noexcept;
    std::array<int,slotCount> activePerSlot {};
    std::array<std::array<uint32_t, lsampler::SlotAudioState::sampleSetSize>, slotCount> roundRobinCounters {};
    std::array<std::array<int, lsampler::SlotAudioState::sampleSetSize>, slotCount> lastRandomChoice {};
    std::array<uint32_t,slotCount> variationRandomState {};
    // Fixed-capacity Shuffle No Repeat cache. At most 2*N+1 distinct velocity
    // candidate sets can occur with N samples and one contiguous range each.
    // Unlike the legacy RR/Random cache (indexed by first candidate), separate
    // eligible masks never overwrite each other's shuffle cycle. No heap/DSP cost.
    static constexpr int shuffleGroupCapacity = 2 * lsampler::SlotAudioState::sampleSetSize + 1;
    struct ShuffleBag {
        std::array<uint8_t, lsampler::SlotAudioState::sampleSetSize> order {};
        std::array<SharedSample*, lsampler::SlotAudioState::sampleSetSize> sources {};
        uint16_t eligibleMask = 0;
        uint8_t count = 0, cursor = 0;
        int lastPlayed = -1;
        uint32_t lastUsed = 0;
    };
    std::array<std::array<ShuffleBag, shuffleGroupCapacity>, slotCount> shuffleBags {};
    std::array<uint32_t, slotCount> shuffleTurns {};
    int activeTotal = 0, previewTotal = 0;
    std::array<int,25> outputRoutes {};
    std::array<juce::AudioBuffer<float>*,3> fxSendBuses {};
    const std::array<std::array<float,3>,slotCount>* fxSendLevels = nullptr;
    const std::array<lsampler::SlotAudioState,slotCount>* states=nullptr;
    double hostSampleRate=44100,bpm=120;
    uint64_t ageCounter=0,noteCounter=0;
};
