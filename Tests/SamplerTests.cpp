#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <new>
#include <cmath>
#include <cstring>

// Intercepts both allocation and destruction on the audio test thread, including voice stealing.
thread_local bool realtimeGuard=false;
thread_local unsigned realtimeAllocations=0,realtimeDeletes=0;
void* operator new(std::size_t n) {if(realtimeGuard)++realtimeAllocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(realtimeGuard&&p)++realtimeDeletes;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
void runSliceTests(const juce::File&);
using namespace lsampler;
namespace {
int checks=0;
void require(bool condition,const char* message) {++checks;if(!condition)throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<=tolerance;}
int index(P parameter){for(size_t i=0;i<grid.size();++i)if(grid[i].parameter==int(parameter))return int(i);throw std::runtime_error("Missing grid entry");}
int loopIndex(L parameter){for(size_t i=0;i<grid.size();++i)if(grid[i].loop==int(parameter))return int(i);throw std::runtime_error("Missing loop entry");}
struct Rig {
    SharedSample sample;
    std::unique_ptr<std::array<SlotAudioState,24>> stateStorage=std::make_unique<std::array<SlotAudioState,24>>();
    std::array<SlotAudioState,24>& states=*stateStorage;
    GlobalVoicePool pool;
    juce::AudioBuffer<float> output {50,65536};
    uint64_t revision=0;
    Rig() {
        sample.sourceSampleRate=48000;sample.audio.setSize(2,65536);sample.peak=.5;
        for(int i=0;i<65536;++i){sample.audio.setSample(0,i,float(.35*std::sin(i*.043)+.1));sample.audio.setSample(1,i,float(.3*std::cos(i*.071)));}
        pool.prepare(48000);set(0,SlotParameters{});pool.setStates(&states);
    }
    void set(int slot,SlotParameters p){states[size_t(slot)]=prepareSlotAudioState(p,&sample,48000,++revision);pool.setStates(&states);}
    void render(int count) {output.clear();realtimeGuard=true;pool.render(output,0,count);realtimeGuard=false;}
    void on(int slot=0,int note=60,float velocity=1,int channel=0){realtimeGuard=true;pool.noteOn(slot,note,velocity,channel);realtimeGuard=false;}
};
void catalogTests() {
    SlotParameters p;require(p[P::root]==60&&p[P::low]==0&&p[P::high]==127,"Approved mapping defaults");
    require(parameterCount==142&&loopCount==10,"Catalog size and loop count");
    for(int i=0;i<parameterCount;++i) {
        const auto& d=parameters[size_t(i)];require(d.initial>=d.minimum&&d.initial<=d.maximum,"Catalog default in range");
        for(int j=0;j<i;++j)require(std::strcmp(d.key,parameters[size_t(j)].key)!=0,"Unique persistent keys");
        require(near(sanitise(d,NAN),d.initial),"Invalid values default safely");
    }
    require(parameters[size_t(P::comp_release)].maximum==10000,"Lua compressor release range");
    require(parameters[size_t(P::gate_hold)].maximum==10000,"Lua gate hold range");
    std::cout<<"PASS catalog and precedence\n";
}
void cleanAndBypassTests() {
    Rig r;r.on();r.render(512);
    for(int i=0;i<512;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,i)),"Clean native path matches source at Original Pitch");
    const auto& d=r.pool.diagnostics;
    require(d.lp+d.hp+d.lfo+d.comp+d.gate+d.drive+d.ring+d.fm+d.degrade+d.transient+d.machine==0,"All optional blocks hard bypass by default");
    auto p=SlotParameters{};p[P::lfo1_lp_depth]=96;p[P::lfo2_hp_depth]=96;p[P::character_depth]=10000;p[P::character_input_drive]=10000;
    r.pool.allNotesOff();r.set(0,p);r.on();r.render(512);
    require(r.pool.diagnostics.lfo==0&&r.pool.diagnostics.machine==0,"Disabled filters and machine do not wake DSP");
    for(int i=0;i<512;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,i)),"Bypass remains unchanged with dormant depths");
    p[P::lp_on]=1;p[P::input_gain]=-120;r.pool.allNotesOff();r.set(0,p);r.on();r.render(512);
    require(r.pool.diagnostics.lp==0&&r.pool.diagnostics.lfo==0,"Silent level skips optional DSP");
    require(r.output.getMagnitude(0,512)==0,"Muted slot remains silent");
    std::cout<<"PASS clean audio, disabled modules and silent paths\n";
}
void voiceTests() {
    Rig r;auto p=SlotParameters{};p[P::global_one_shot]=0;
    for(int slot=0;slot<24;++slot)r.set(slot,p);
    for(int note=40;note<50;++note)for(int slot=0;slot<24;++slot)r.on(slot,note);
    require(r.pool.activeVoiceCount()==96,"One global 96-voice limit");
    r.pool.allNotesOff();p[P::slot_polyphony]=2;r.set(0,p);
    for(int note=50;note<60;++note)r.on(0,note);
    require(r.pool.activeVoiceCount(0)==2,"Per-slot cap inside global pool");
    r.pool.allNotesOff();p[P::polyphony]=0;p[P::portamento]=30;p[P::legato]=1;r.set(0,p);
    r.on(0,60);r.render(128);r.on(0,67);r.render(128);
    require(r.pool.activeVoiceCount()==1,"Mono reuses a single musical voice");
    r.pool.noteOff(67);r.render(128);require(r.pool.activeVoiceCount()==1,"Mono last-note fallback");
    r.pool.noteOff(60);r.render(20000);require(r.pool.activeVoiceCount()==0,"Release eventually retires mono voice");
    p[P::polyphony]=1;p[P::sustain_pedal]=1;r.set(0,p);r.on();r.pool.controller(0,64,127);r.pool.noteOff(60);r.render(1000);
    require(r.pool.activeVoiceCount()==1,"Enabled sustain holds released key");r.pool.controller(0,64,0);r.render(20000);require(r.pool.activeVoiceCount()==0,"Pedal release releases voice");
    r.on();r.pool.controller(1,120,0);require(r.pool.activeVoiceCount()==1,"CC120 isolation by MIDI channel");r.pool.controller(0,120,0);require(r.pool.activeVoiceCount()==0,"CC120 immediate stop");
    p[P::polyphony]=1;p[P::retrigger_smooth]=4;r.set(0,p);r.on();r.render(10);r.on(0,67);require(r.pool.activeVoiceCount()==1,"No Retrigger Smooth retains read head on a new note");
    r.pool.allNotesOff();p[P::choke_trigger]=62;p[P::choke_target]=60;r.set(1,p);r.on();r.pool.choke(62);require(r.pool.activeVoiceCount()==0,"Per-slot choke kills matching voices");
    require(r.sample.voiceReferences.load()==0,"All stopped voices relinquish their sample pins");
    std::cout<<"PASS voices, mono, cap, sustain, channels, choke and sample lifetime\n";
}
void pitchAndStereoTests() {
    Rig r;auto p=SlotParameters{};p[P::octave]=1;r.set(0,p);r.on();r.render(64);
    for(int i=0;i<64;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,2*i)),"Octave mapping");
    r.pool.allNotesOff();p[P::octave]=0;p[P::pitch_bend_range]=12;r.set(0,p);r.pool.pitchBend(0,-8192);r.on();r.render(64);
    for(int i=0;i<64;++i){const double pos=i*.5;const int j=int(pos);const auto a=r.sample.audio.getSample(0,j),b=r.sample.audio.getSample(0,j+1);require(near(r.output.getSample(0,i),a+(b-a)*(pos-j)),"Per-channel pitch bend range");}
    r.pool.allNotesOff();r.pool.pitchBend(0,0);p[P::ram_swap_lr]=1;p[P::pan]=-100;r.set(0,p);r.on();r.render(64);
    require(near(r.output.getSample(0,9),r.sample.audio.getSample(1,9)),"Stereo swap");require(r.output.getMagnitude(1,0,64)==0,"Static pan");
    r.pool.allNotesOff();p=SlotParameters{};p[P::sample_start]=25;p[P::sample_end]=75;p[P::sample_play_start]=50;r.set(0,p);r.on();r.render(2);
    const int offset=16384+int(std::round((32768-1)*.5));require(near(r.output.getSample(0,0),r.sample.audio.getSample(0,offset)),"Sample window and play start");
    r.pool.allNotesOff();p=SlotParameters{};p[P::ram_reverse]=1;r.set(0,p);r.on();r.render(32);
    for(int i=0;i<32;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,65535-i)),"Non-destructive reverse");
    r.pool.allNotesOff();p=SlotParameters{};p[P::vel_volume_depth]=-1;r.set(0,p);r.on(0,60,.25f);r.render(32);
    require(near(r.output.getSample(0,10),r.sample.audio.getSample(0,10)),"Negative velocity depth follows legacy clamp");
    std::cout<<"PASS pitch, velocity, stereo tools, sample window and reverse\n";
}
void dspReferenceTests() {
    Rig r;auto p=SlotParameters{};p[P::ring_mode]=1;p[P::ring_amount]=.6;p[P::ring_freq]=440;r.set(0,p);r.on();r.render(128);
    for(int i=0;i<128;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,i)*(1+.6*std::sin(2*juce::MathConstants<double>::pi*440*(i+1)/48000))),"Legacy additive ring equation");
    r.pool.allNotesOff();p=SlotParameters{};p[P::lp_on]=1;p[P::lp_cutoff]=.4;p[P::lp_resonance]=.3;r.set(0,p);r.on();r.render(128);
    const double hz=20*std::pow(500,.4),g=std::tan(juce::MathConstants<double>::pi*hz/48000),k=1/(.707+.3*9.293),a1=1/(1+g*(g+k)),a2=g*a1,a3=g*a2;
    double band=0,low=0;
    for(int i=0;i<128;++i){const double x=r.sample.audio.getSample(0,i),v3=x-low,v1=a1*band+a2*v3,v2=low+a2*band+a3*v3;band=2*v1-band;low=2*v2-low;require(near(r.output.getSample(0,i),low),"Legacy LP integrator output");}
    r.pool.allNotesOff();p=SlotParameters{};p[P::drive_type]=1;p[P::drive_amount]=50;r.set(0,p);r.on();r.render(128);
    for(int i=0;i<128;++i){const double x=r.sample.audio.getSample(0,i);require(near(r.output.getSample(0,i),x*.5+(2/juce::MathConstants<double>::pi)*std::atan(x*5)*.5),"Legacy tape drive equation");}
    r.pool.allNotesOff();p=SlotParameters{};p[P::comp_on]=1;p[P::comp_ratio]=1;p[P::comp_delta]=1;r.set(0,p);r.on();r.render(256);require(r.output.getMagnitude(0,256)==0,"Compressor delta zero at unity ratio/makeup");
    r.pool.allNotesOff();p=SlotParameters{};p[P::lfo1_bpm_sync]=1;p[P::lfo1_rate]=4;p[P::lfo1_mode]=1;p[P::lfo1_volume_depth]=1;r.set(0,p);r.pool.setTempo(120);r.on();r.render(128);
    for(int i=0;i<128;++i)require(near(r.output.getSample(0,i),r.sample.audio.getSample(0,i)*(1+std::sin(2*juce::MathConstants<double>::pi*2*(i+1)/48000))),"Host tempo LFO conversion");
    r.pool.allNotesOff();r.pool.setTempo(60);r.on();r.render(128);require(near(r.output.getSample(0,127),r.sample.audio.getSample(0,127)*(1+std::sin(2*juce::MathConstants<double>::pi*128/48000))),"Tempo changes update synced LFO");
    std::cout<<"PASS legacy ring, LP, tape, compressor delta and BPM LFO equations\n";
}
void loopAndEffectTests() {
    Rig r;r.sample.audio.setSize(2,100);r.sample.peak=1;
    for(int i=0;i<100;++i)for(int ch=0;ch<2;++ch)r.sample.audio.setSample(ch,i,float(i)/100);
    auto p=SlotParameters{};p[P::global_one_shot]=0;
    p.loops[0][size_t(L::end)]=10;p.loops[0][size_t(L::repeats)]=3;
    p.loops[1][size_t(L::start)]=50;p.loops[1][size_t(L::end)]=60;p.loops[1][size_t(L::repeats)]=2;
    r.set(0,p);r.on();r.render(50);
    for(int i=0;i<40;++i)require(near(r.output.getSample(0,i),double(i%10)/100),"Loop counted repeats");
    for(int i=40;i<50;++i)require(near(r.output.getSample(0,i),double(50+i-40)/100),"Next loop jumps to absolute start");
    r.pool.allNotesOff();p=SlotParameters{};p[P::global_one_shot]=1;r.set(0,p);r.on();r.render(200);require(r.pool.activeVoiceCount()==0,"Global one shot stops at sample end");
    r.pool.allNotesOff();p=SlotParameters{};p[P::global_one_shot]=0;p[P::machine_character]=7;p[P::character_depth]=10000;
    p[P::comp_on]=p[P::gate_on]=p[P::lp_on]=p[P::hp_on]=1;p[P::fm_amount]=80;p[P::ring_mode]=2;p[P::ring_amount]=1;
    p[P::degrade_amount]=80;p[P::transient_shape]=-80;p[P::drive_type]=2;p[P::drive_amount]=80;p[P::lfo2_pitch_depth]=2;
    r.set(0,p);r.on();r.render(8192);
    for(int i=0;i<8192;++i)require(std::isfinite(r.output.getSample(0,i)),"Combined DSP output remains finite");
    const auto& d=r.pool.diagnostics;require(d.machine&&d.comp&&d.gate&&d.hp&&d.fm&&d.ring&&d.degrade&&d.transient&&d.drive&&d.lfo,"Every enabled module executes");
    std::array<int,25> routes;routes.fill(-1);routes[0]=0;routes[3]=6;r.pool.setOutputRoutes(routes);
    r.pool.allNotesOff();p=SlotParameters{};p[P::output_route]=3;r.set(0,p);r.on();r.render(64);
    require(r.output.getMagnitude(0,0,64)==0&&r.output.getMagnitude(6,0,64)>0,"Exclusive multibus routing");
    std::cout<<"PASS ten-stage loop progression, one shot, combined DSP and routing\n";
}
juce::File writeSample(const juce::File& folder) {
    const auto file=folder.getChildFile("test.wav");juce::WavAudioFormat format;
    auto stream=file.createOutputStream();std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000,2,24,{},0));
    require(bool(writer),"Test WAV writer");stream.release();juce::AudioBuffer<float> a(2,48000);
    for(int i=0;i<48000;++i){a.setSample(0,i,float(.4*std::sin(i*.041)));a.setSample(1,i,float(.3*std::cos(i*.043)));}
    require(writer->writeFromAudioSampleBuffer(a,0,a.getNumSamples()),"Test WAV written");return file;
}
void process(LSampler24AudioProcessor& p,juce::AudioBuffer<float>& a,juce::MidiBuffer& midi) {
    realtimeGuard=true;p.processBlock(a,midi);realtimeGuard=false;
}
void persistenceAndProcessorTests(const juce::File& folder) {
    LSampler24AudioProcessor p(folder.getChildFile("library"));p.prepareToPlay(48000,256);juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;midi.ensureSize(4096);
    process(p,audio,midi);require(audio.getMagnitude(0,256)==0,"Empty processor silent");
    juce::String error;const auto wav=writeSample(folder);require(p.loadSample(wav,error),"Load sample");
    auto original=SamplePool::instance().load(wav,error);require(bool(original),"Shared sample loaded");
    midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(127)),64);process(p,audio,midi);
    require(audio.getMagnitude(0,0,64)==0&&audio.getMagnitude(0,64,192)>0,"Sample accurate MIDI offset");
    p.copyCurrentSlot();p.setCurrentSlot(1);require(p.pasteCurrentSlot(),"Paste full slot");
    midi.addEvent(juce::MidiMessage::allSoundOff(1),0);midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(127)),0);process(p,audio,midi);
    require(p.getActiveVoiceCount()==2,"Overlapping slots layer");
    auto shared=SamplePool::instance().load(wav,error);require(original==shared,"Decoded audio is shared");
    p.setSlotParameter(index(P::velocity_low),127);p.setSlotParameter(index(P::velocity_high),127);
    midi.addEvent(juce::MidiMessage::allSoundOff(1),0);midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(64)),0);process(p,audio,midi);
    require(p.getActiveVoiceCount()==1,"Per-slot velocity region");
    p.setCurrentSlot(0);
    std::array<double,parameterCount> expected;
    for(int i=0;i<parameterCount;++i) {
        const auto& d=parameters[size_t(i)];const double v=sanitise(d,d.minimum+(d.maximum-d.minimum)*.371);
        p.setSlotParameter(index(P(i)),v);expected[size_t(i)]=p.getSlotParameter(index(P(i)));
    }
    // Setting sync may quantise the already-edited rate, so capture the final state.
    for(int i=0;i<parameterCount;++i)expected[size_t(i)]=p.getSlotParameter(index(P(i)));
    std::array<std::array<double,loopParameterCount>,loopCount> expectedLoops;
    for(int j=0;j<10;++j)for(int i=0;i<loopParameterCount;++i) {
        const auto& d=loopParameters[size_t(i)];const double v=sanitise(d,d.minimum+(d.maximum-d.minimum)*(.2+j*.03));
        p.setSlotParameter(loopIndex(L(i)),v,j);expectedLoops[size_t(j)][size_t(i)]=v;
    }
    auto compare=[&] {
        for(int i=0;i<parameterCount;++i)require(near(p.getSlotParameter(index(P(i))),expected[size_t(i)],1e-8),"Every slot parameter roundtrips");
        for(int j=0;j<10;++j)for(int i=0;i<loopParameterCount;++i)require(near(p.getSlotParameter(loopIndex(L(i)),j),expectedLoops[size_t(j)][size_t(i)],1e-8),"All ten loop memories roundtrip");
    };
    const auto slotFile=folder.getChildFile("Slot_Test.lsampler-24-s");require(p.saveSlotPreset(slotFile,error),"Save slot");
    auto alias=SamplePool::instance().load(p.getCurrentSampleFile(),error);require(alias==original,"Saving materialises a file without duplicating decoded audio");
    p.clearCurrentSlot();require(p.loadSlotPreset(slotFile,error),"Load slot");compare();
    p.copyCurrentSlot();for(int i=1;i<24;++i){p.setCurrentSlot(i);p.pasteCurrentSlot();compare();}
    const auto bankFile=folder.getChildFile("Bank_Test.lsampler-24-b");require(p.saveBankPreset(bankFile,error),"Save 24-slot bank");
    p.clearBank();require(p.loadBankPreset(bankFile,error),"Load 24-slot bank");for(int i=0;i<24;++i){p.setCurrentSlot(i);compare();}
    juce::MemoryBlock state;p.getStateInformation(state);p.clearBank();p.setStateInformation(state.getData(),int(state.getSize()));
    for(int i=0;i<24;++i){p.setCurrentSlot(i);compare();}
    p.cutCurrentSlot();require(p.getCurrentSampleFile().getFullPathName().isEmpty(),"Cut clears source slot");p.pasteCurrentSlot();compare();
    // Original v2 XML, missing all migrated values.
    const auto oldFile=folder.getChildFile("old.lsampler-24-s");
    oldFile.replaceWithText("<LSampler24Slot formatVersion=\"2\" samplePath=\""+wav.getFullPathName()+"\" lowKey=\"12\" highKey=\"95\" rootNote=\"60\" volume=\"0.5\"/>");
    require(p.loadSlotPreset(oldFile,error),"Legacy v2 slot loads");require(p.getLowKey()==12&&p.getHighKey()==95&&p.getRootNote()==60&&near(p.getVolume(),.5),"Legacy mapping and linear volume aliases");
    require(p.getSlotParameter(index(P::lp_on))==0,"Missing values reset to catalog defaults");
    p.clearBank();process(p,audio,midi);require(p.getActiveVoiceCount()==0,"Clear bank stops all voices");
    std::cout<<"PASS processor offsets, mapping, layering, complete slot/bank/project persistence and v2 compatibility\n";
}
void concurrentPublicationTest(const juce::File& folder) {
    LSampler24AudioProcessor p(folder.getChildFile("library"));juce::String error;require(p.loadSample(folder.getChildFile("test.wav"),error),"Load concurrent test sample");p.prepareToPlay(48000,64);
    std::atomic<bool> start{false};std::atomic<unsigned> allocations{0},deletes{0};
    std::thread audio([&] {
        juce::AudioBuffer<float> buffer(2,64);juce::MidiBuffer midi;midi.ensureSize(1024);
        while(!start.load())std::this_thread::yield();
        for(int i=0;i<3000;++i) {
            if(i%13==0)midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(100)),0);
            if(i%17==0)midi.addEvent(juce::MidiMessage::noteOff(1,60),32);
            realtimeGuard=true;p.processBlock(buffer,midi);realtimeGuard=false;
        }
        allocations=realtimeAllocations;deletes=realtimeDeletes;
    });
    start=true;
    for(int i=0;i<600;++i) {
        p.setSlotParameter(index(P::pan),(i%201)-100);
        p.setSlotParameter(index(P::lp_on),i%2);
        if(i%31==0){p.clearCurrentSlot();p.loadSample(folder.getChildFile("test.wav"),error);}
    }
    audio.join();require(allocations==0&&deletes==0,"Concurrent publication has no audio-thread allocations or frees");
    std::cout<<"PASS concurrent snapshot publication and realtime allocation/deletion guard\n";
}
// TEST58: compile/run only on the user's regression target, never as part of
// source preparation. Covers sleep/wake, pending commands and worker ownership.
void idleAndFileWorkerTests(const juce::File& folder) {
    LSampler24AudioProcessor p(folder.getChildFile("idle-worker-library"));
    juce::String error;
    require(p.loadSample(folder.getChildFile("test.wav"), error), "Idle test sample");
    p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
    for (int block = 0; block < 64; ++block) {
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) audio.setSample(ch, i, 1.0f);
        process(p, audio, midi);
        require(audio.getMagnitude(0, 128) == 0.0f, "Idle always clears host buffers");
        require(p.getActiveVoiceCount() == 0, "Idle creates no voices");
    }
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 37);
    process(p, audio, midi);
    require(audio.getMagnitude(0, 0, 37) == 0.0f, "First note after idle preserves MIDI offset");
    require(audio.getMagnitude(0, 37, 91) > 0.0f, "First note after idle is audible in same block");
    midi.addEvent(juce::MidiMessage::allSoundOff(1), 0); process(p, audio, midi);
    require(p.getActiveVoiceCount() == 0, "All sound off retires active counter");
    process(p, audio, midi);
    p.requestPreviewStart(); process(p, audio, midi);
    require(p.getActiveVoiceCount() > 0, "Slot preview wakes idle");
    p.requestPreviewStop(); process(p, audio, midi);
    require(p.getActiveVoiceCount() == 0, "Preview stop retires preview counter");
    require(p.prepareImportPreview(folder.getChildFile("test.wav"), error), "Prepare import preview");
    p.requestImportPreviewToggle(); process(p, audio, midi);
    require(p.isImportPreviewPlaying(), "Import preview publication and toggle wake idle");
    p.requestImportPreviewStop(); process(p, audio, midi);
    require(!p.isImportPreviewPlaying(), "Import preview stop processed");
    const auto preset = folder.getChildFile("idle-preview.lsampler-24-s");
    require(p.saveSlotPreset(preset, error), "Save preview test slot");
    require(p.prepareLibrarySlotPreview(preset, error), "Publish library preview");
    p.requestLibraryPreviewToggle(); process(p, audio, midi);
    require(p.isLibraryPreviewPlaying(), "Library snapshot and toggle wake idle");
    p.requestLibraryPreviewStop(); process(p, audio, midi);
    require(!p.isLibraryPreviewPlaying(), "Library preview stop processed");

    auto entered = std::make_shared<juce::WaitableEvent>();
    auto release = std::make_shared<juce::WaitableEvent>();
    auto finished = std::make_shared<juce::WaitableEvent>();
    auto separateThread = std::make_shared<std::atomic<bool>>(false);
    const auto caller = std::this_thread::get_id();
    require(p.startFileTask([entered, release, finished, separateThread, caller](LSampler24AudioProcessor&) {
        separateThread->store(std::this_thread::get_id() != caller);
        entered->signal(); release->wait(5000);
        finished->signal();
        return LSampler24AudioProcessor::FileTaskResult { true, "Test completed" };
    }, [](LSampler24AudioProcessor::FileTaskResult) {}), "Start file worker without waiting for disk job");
    require(entered->wait(2000), "Worker starts");
    require(separateThread->load(), "File task executes on another thread");
    const bool duplicate = p.startFileTask([](LSampler24AudioProcessor&) {
        return LSampler24AudioProcessor::FileTaskResult {};
    }, [](LSampler24AudioProcessor::FileTaskResult) {});
    release->signal();
    require(!duplicate, "Second file operation cannot overlap the first");
    require(finished->wait(2000), "Worker can finish independently of message dispatch");
    process(p, audio, midi);
    require(audio.getMagnitude(0, 128) == 0.0f, "Audio remains valid around file work");
    std::cout << "PASS idle wakeups and file worker isolation\n";
}

void editorTests(const juce::File& folder) {
    LSampler24AudioProcessor p(folder.getChildFile("library"));
    auto editor=std::make_unique<LSampler24AudioProcessorEditor>(p);
    LSamplerParameterComboBox* gridControl=nullptr;LSamplerValueSlider* value=nullptr;LSamplerSlotCell* slot=nullptr;
    for(auto* c:editor->getChildren()) {
        if(auto* g=dynamic_cast<LSamplerParameterComboBox*>(c))gridControl=g;
        if(auto* v=dynamic_cast<LSamplerValueSlider*>(c))value=v;
        if(!slot)slot=dynamic_cast<LSamplerSlotCell*>(c);
    }
    require(gridControl&&value&&slot,"Stable Grid/Value/Slot component types");
    auto key=[&](int code,int mods=0,juce::juce_wchar ch=0){return juce::KeyPress(code,juce::ModifierKeys(mods),ch);};
    require(!editor->keyPressed(key(juce::KeyPress::spaceKey),slot),"Slots Space belongs to host transport");
    require(editor->keyPressed(key(juce::KeyPress::returnKey),slot),"Enter parameters");
    require(gridControl->getNumItems()==int(grid.size()),"All descriptors populate grid");
    require(editor->keyPressed(key(juce::KeyPress::spaceKey),gridControl),"Parameter Space owns preview");
    require(editor->keyPressed(key(juce::KeyPress::endKey,juce::ModifierKeys::ctrlModifier),gridControl),"Ctrl End is consumed");
    require(gridControl->getSelectedItemIndex()==int(grid.size())-1,"Ctrl End reaches last parameter");
    editor->keyPressed(key(juce::KeyPress::homeKey,juce::ModifierKeys::ctrlModifier),gridControl);
    require(gridControl->getSelectedItemIndex()==0,"Ctrl Home reaches first parameter");
    editor->keyPressed(key(juce::KeyPress::rightKey),gridControl);
    require(std::strcmp(grid[size_t(gridControl->getSelectedItemIndex())].category,"Mapping")==0,"Right arrow moves category");
    require(editor->keyPressed(key('Z',juce::ModifierKeys::ctrlModifier,'z'),gridControl),"Grid consumes unrelated host Ctrl commands");
    require(editor->keyPressed(key(juce::KeyPress::tabKey),gridControl),"Grid Tab");
    require(editor->keyPressed(key(juce::KeyPress::returnKey),value),"Value Enter returns to Grid");
    require(editor->keyPressed(key(juce::KeyPress::escapeKey),gridControl),"Escape returns to slots");
    std::cout<<"PASS keyboard routing and grid catalog (NVDA speech still requires Windows host testing)\n";
}
}
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("lsampler-regression","",false);folder.createDirectory();
        catalogTests();cleanAndBypassTests();voiceTests();pitchAndStereoTests();dspReferenceTests();loopAndEffectTests();
        persistenceAndProcessorTests(folder);concurrentPublicationTest(folder);idleAndFileWorkerTests(folder);editorTests(folder);runSliceTests(folder);
        require(realtimeAllocations==0&&realtimeDeletes==0,"Zero allocations and frees across all guarded render/note-on/process calls");
        folder.deleteRecursively();
        std::cout<<"PASS "<<checks<<" checks; zero guarded realtime allocations/deletions\n";return 0;
    }catch(const std::exception& e){realtimeGuard=false;std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
