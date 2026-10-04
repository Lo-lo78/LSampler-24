#include "../Source/SliceEngine.h"
#include "../Source/PluginProcessor.h"
#include <stdexcept>
#include <limits>
#include <iostream>

// Source-only regression cases supplied for the user's later test build.
// No test/build was run when this change was prepared.
extern thread_local bool realtimeGuard;
namespace {
using namespace lsampler;
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
void modelTests() {
    SliceState s;check(s.division()==8&&s[SliceG::mode]==0,"Legacy slice defaults are inactive");
    s.steps[0][SliceP::source]=8;s.steps[0][SliceP::repeat]=7;s.steps[127][SliceP::pitch]=13.25;
    s.boundaries[1]=.09;
    auto restored=SliceState::fromTree(s.toTree());
    check(restored.toTree().isEquivalentTo(s.toTree()),"Slice tree roundtrips all 128 records");
    s.setDivision(1);check(s.steps[0][SliceP::source]==8&&s.steps[127][SliceP::pitch]==13.25,"Division preserves latent step records");
    auto small=prepareSliceAudio(s,100);check(small.count==2&&small.boundaries[1]==50,"Division generates contiguous boundaries");
    s.setDivision(13);
    for(int length=1;length<512;++length) {
        const auto a=prepareSliceAudio(s,length);
        check(a.count==std::min(128,length)&&a.boundaries[0]==0&&a.boundaries[size_t(a.count)]==length,"Window endpoints exact");
        for(int i=1;i<=a.count;++i)check(a.boundaries[size_t(i)]>a.boundaries[size_t(i-1)],"No empty/overlapping slice at short lengths");
    }
    s.globals[0]=std::numeric_limits<double>::quiet_NaN();s.boundaries[1]=-2;s.boundaries[2]=std::numeric_limits<double>::infinity();
    s.steps[0][SliceP::repeat]=1e30;s.steps[0][SliceP::output]=-10;s.sanitise();
    check(s[SliceG::mode]==0&&s.steps[0][SliceP::repeat]==128&&s.steps[0][SliceP::output]==0,"Invalid persisted values are bounded");
    check(SliceState::fromTree({})[SliceG::mode]==0,"Missing Slice child backward compatibility");
}
int duration(SliceAudioState a,int request) {
    SlicePlayback q;q.start(a,request,1234);int frames=0;
    while(!q.finished&&frames<100000) {
        if(!q.configured)q.configure(a,800,48000,false,0);
        q.advance(a,1);++frames;
    }
    return frames;
}
void timingTests() {
    SliceState s;s[SliceG::mode]=7;s.steps[0][SliceP::repeat]=4;
    auto a=prepareSliceAudio(s,800);
    check(duration(a,0)==400,"Lengthen Repeat 4 occupies four units");
    s.steps[0][SliceP::fit]=1;a=prepareSliceAudio(s,800);
    check(duration(a,0)==100,"Fit Repeat 4 occupies one unit");
    s.steps[0][SliceP::repeat]=1;s.steps[0][SliceP::fit]=0;s.steps[0][SliceP::pitch]=12;
    a=prepareSliceAudio(s,800);SlicePlayback q;q.start(a,0,1);q.configure(a,800,48000,false,0);
    q.elapsed=25;bool silent=false;check(near(q.position(silent),50)&&!silent,"Fixed step pitch changes read speed");
    q.elapsed=60;q.position(silent);check(silent,"Fixed clock waits in silence after pitched sample end");
    check(duration(a,0)==100,"Original Pitch Step keeps duration");
    s[SliceG::pitchTime]=1;a=prepareSliceAudio(s,800);check(duration(a,0)==50,"Pitch Resizes Step changes duration");
    s.steps[0][SliceP::repeat]=3;s.steps[0][SliceP::repeatPitch]=12;s.steps[0][SliceP::repeatPan]=100;
    s.steps[0][SliceP::repeatLP]=7;s.steps[0][SliceP::repeatHP]=-4;s[SliceG::pitchTime]=0;
    a=prepareSliceAudio(s,800);q.start(a,0,2);q.configure(a,800,48000,false,0);
    check(q.repeatPan==-1&&q.lp==0&&q.hp==0,"First repeat starts left and unfiltered");
    q.repeat=2;q.configure(a,800,48000,false,0);
    check(q.repeatPan==1&&q.lp==-14&&q.hp==8&&near(q.pitch,8),"Repeat modulation accumulates in original units");
    s=SliceState{};s[SliceG::shuffle]=100;s[SliceG::mode]=7;
    a=prepareSliceAudio(s,800);q.start(a,-3,2);q.configure(a,800,48000,false,0);
    check(near(q.clockLength,150)&&near(q.readScale,1),"Shuffle without pitch moves event timing");
    for(int i=0;i<150;++i)q.advance(a,1);
    q.configure(a,800,48000,false,0);check(q.step==1&&near(q.clockLength,50),"Shuffle preserves pair duration");
    s[SliceG::random]=2;for(int i=0;i<8;++i)s.steps[size_t(i)][SliceP::source]=i%2+1;
    a=prepareSliceAudio(s,800);q.start(a,-3,9);int previous=-1;
    for(int j=0;j<32;++j) {
        q.configure(a,800,48000,false,0);check(q.source!=previous,"Random No Repeat compares source slices, not step numbers");
        previous=q.source;for(int i=0;i<int(q.clockLength);++i)q.advance(a,1);
    }
}
void renderTests() {
    auto states=std::make_unique<std::array<SlotAudioState,24>>();
    SharedSample sample;sample.sourceSampleRate=48000;sample.audio.setSize(2,800);sample.peak=.5;
    for(int i=0;i<800;++i)for(int c=0;c<2;++c)sample.audio.setSample(c,i,float((i/100+1)*.05));
    SlotParameters p;p[P::global_one_shot]=1;
    auto& state=(*states)[0];state=prepareSlotAudioState(p,&sample,48000,1);
    SliceState s;s[SliceG::mode]=7;s.steps[0][SliceP::source]=4;s.steps[0][SliceP::output]=2;
    state.slice=prepareSliceAudio(s,state.length);
    GlobalVoicePool pool;pool.prepare(48000);pool.setStates(states.get());
    std::array<int,25> routes;routes.fill(-1);routes[0]=0;routes[2]=4;pool.setOutputRoutes(routes);
    juce::AudioBuffer<float> audio(6,120);audio.clear();
    realtimeGuard=true;pool.noteOn(0,60,1,0,false,-1,0);pool.render(audio,0,120);realtimeGuard=false;
    check(near(audio.getSample(4,10),.2)&&audio.getMagnitude(0,0,120)==0,"Step mapping and output override reach the requested bus");
    check(pool.activeVoiceCount()==0,"Single slice ends without slot-loop wrap");
    s.steps[0][SliceP::mute]=1;state.slice=prepareSliceAudio(s,state.length);++state.revision;pool.setStates(states.get());audio.clear();
    realtimeGuard=true;pool.noteOn(0,60,1,0,false,-1,0);pool.render(audio,0,120);realtimeGuard=false;
    check(audio.getMagnitude(0,120)==0,"Mute is independent of persisted volume");
    s.steps[0][SliceP::mute]=0;s.steps[0][SliceP::repeat]=2;s.steps[0][SliceP::repeatLP]=12;
    state.slice=prepareSliceAudio(s,state.length);++state.revision;pool.setStates(states.get());audio.clear();
    realtimeGuard=true;pool.noteOn(0,60,1,0,false,-1,0);pool.render(audio,0,120);realtimeGuard=false;
    check(pool.diagnostics.lp==20,"Bypassed LP wakes only during programmed repeats");
    pool.allNotesOff();
}
void persistenceTests(const juce::File& folder) {
    auto owner=std::make_unique<LSampler24AudioProcessor>(folder.getChildFile("slice-library"));auto& p=*owner;
    juce::String error;check(p.loadSample(folder.getChildFile("test.wav"),error),"Load persistence slice sample");
    auto s=p.getSliceState(0);s[SliceG::mode]=7;s[SliceG::midiMap]=1;s.zeroCrossing=true;s.boundaries[1]=.08;
    s.steps[4][SliceP::midiNote]=36;s.steps[4][SliceP::source]=8;s.steps[4][SliceP::repeatLP]=-42;
    s.steps[127][SliceP::fadeOut]=875;p.setSliceState(0,s);
    auto same=[&](int slot){check(p.getSliceState(slot).toTree().isEquivalentTo(s.toTree()),"Processor preserves complete Slice state");};
    p.copyCurrentSlot();p.setCurrentSlot(3);check(p.pasteCurrentSlot(),"Slot paste");same(3);
    const auto slot=folder.getChildFile("slice.lsampler-24-s");check(p.saveSlotPreset(slot,error),"Slice slot save");
    p.clearCurrentSlot();check(p.loadSlotPreset(slot,error),"Slice slot load");same(3);
    const auto bank=folder.getChildFile("slice.lsampler-24-b");check(p.saveBankPreset(bank,error),"Slice bank save");
    p.clearBank();check(p.loadBankPreset(bank,error),"Slice bank load");same(0);same(3);
    juce::MemoryBlock data;p.getStateInformation(data);p.clearBank();p.setStateInformation(data.getData(),int(data.getSize()));same(0);same(3);
    // A physically edited sample must retain internal boundaries when slot-save
    // materialises its RAM buffer, without creating a separate WAV for each slice.
    p.setCurrentSlot(0);p.applyZeroCrossing(false);s=p.getSliceState(0);
    int startBefore=0,startAfter=0;double rate=0;
    const auto before=p.getSliceLayout(0,startBefore,rate);
    juce::MemoryBlock trimmedState;p.getStateInformation(trimmedState);p.clearBank();
    p.setStateInformation(trimmedState.getData(),int(trimmedState.getSize()));same(0);
    const auto after=p.getSliceLayout(0,startAfter,rate);
    check(before.boundaries[size_t(before.count)]==after.boundaries[size_t(after.count)],"Host state restores exact trimmed buffer length");
    check(p.saveSlotPreset(slot,error),"Trimmed sample plus Slice save");p.clearCurrentSlot();check(p.loadSlotPreset(slot,error),"Trimmed sample plus Slice load");same(0);
}
}
void runSliceTests(const juce::File& folder) {
    modelTests();timingTests();renderTests();persistenceTests(folder);
    std::cout<<"PASS Slice boundaries, state, repeat clocks, random, routing and persistence\n";
}
