#!/usr/bin/env python3
"""TEST188: frozen slot IDs, accessible Hold UI, appended host lanes and actual C++ AHDSR DSP."""
from pathlib import Path
import re, subprocess, tempfile, shutil, sys
root=Path(__file__).resolve().parents[1]
read=lambda path:(root/path).read_text(encoding='utf-8')
p=read('Source/Parameters.h')
state=read('Source/SlotAudioState.cpp')
state_h=read('Source/SlotAudioState.h')
voice=read('Source/SamplerVoice.cpp')
voice_h=read('Source/SamplerVoice.h')
host=read('Source/PluginProcessor.cpp')
assert 'parameterCount = 178' in p and 'std::array<GridEntry, 202> grid' in p
assert 'int(P::lfo2_lace_mix)==169 && int(P::env_hold)==170' in p
assert 'int(P::lp_env_hold)==171 && int(P::hp_env_hold)==172' in p
for field,attack,group in [('env_hold','attack','Envelope'),('lp_env_hold','lp_env_attack','Low pass filter'),('hp_env_hold','hp_env_attack','High pass filter')]:
    assert f'{{"{field}", ' in p
    assert f'v(P::{field})' in state
    marker=f'{{int(P::{attack}), -1, Action::none, "{group}"}},\n{{int(P::{field}), -1, Action::none, "{group}"}},'
    assert marker in p,field
    assert f'"slot"+juce::String(slot+1).paddedLeft(\'0\',2)+"_"' in host
assert host.index('// TEST188: three AHDSR Hold controls') > host.index('// TEST186: ten LFO Drift/Lace controls')
assert host.count('for(int i=int(P::lfo1_drift_amount);i<int(P::env_hold);++i)') == 2
assert host.count('for(int i=int(P::env_hold);i<int(P::phase);++i)') == 2
assert 'int phase=1, holdElapsed=0;' in voice_h
assert 'if(phase==5)' in voice and 'if(++holdElapsed>=e.holdSamples)phase=2;' in voice
assert 'if(phase!=0)phase=4;' in voice_h
assert 'int holdSamples = 0;' in state_h
# Compile and execute the actual EnvelopeSettings type and envelope()/tick()
# bodies extracted from the checked-in sources: no reimplementation in the test.
def take_function(source, marker):
    pos=source.index(marker)
    start=source.index('{',pos)
    depth=0
    for i in range(start,len(source)):
        if source[i]=='{':depth+=1
        elif source[i]=='}':
            depth-=1
            if depth==0:return source[pos:i+1]
    raise AssertionError('Unclosed C++ function '+marker)
settings=re.search(r'struct EnvelopeSettings \{(.+?)\n\};',state_h,re.S)
assert settings
nested=re.search(r'    struct Envelope \{(.+?)\n    \};',voice_h,re.S)
assert nested
converter=take_function(state,'EnvelopeSettings envelope(')
ticker=take_function(voice,'double GlobalVoicePool::Envelope::tick(')
cpp=r'''#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
namespace lsampler {
struct EnvelopeSettings {
__SETTINGS__
};
}
using namespace lsampler;
namespace { constexpr double offAmplitude=.00003162277660168379; }
class GlobalVoicePool { public: struct Envelope {
__NESTED__
}; };
namespace { __CONVERTER__ }
__TICKER__
int main() {
    constexpr double sr=1000.0;
    auto amp=envelope(.001,.003,.008,.25,.010,sr,false);
    auto lp =envelope(.001,.005,.008,.25,.010,sr,true);
    auto hp =envelope(.001,.002,.008,.25,.010,sr,true);
    assert(amp.holdSamples==3 && lp.holdSamples==5 && hp.holdSamples==2);
    GlobalVoicePool::Envelope a,l,h;
    assert(a.tick(amp)==1 && a.phase==5);
    assert(l.tick(lp)==1 && l.phase==5);
    assert(h.tick(hp)==1 && h.phase==5);
    for(int i=1;i<3;++i)assert(a.tick(amp)==1);
    assert(a.phase==2 && a.tick(amp)<1);
    for(int i=1;i<5;++i)assert(l.tick(lp)==1);
    assert(l.phase==2 && l.tick(lp)<1);
    assert(h.tick(hp)==1 && h.phase==2 && h.tick(hp)<1);
    // Existing ADSR with Hold=0 decays on the same tick as completed Attack.
    auto old=envelope(.001,0,.008,.25,.010,sr,false);
    GlobalVoicePool::Envelope zero;
    assert(zero.tick(old)<1 && zero.phase==2);
    // Note Off interrupts Hold without going through Decay or Sustain.
    GlobalVoicePool::Envelope off;
    assert(off.tick(lp)==1 && off.phase==5);
    off.release(); assert(off.phase==4);
    assert(off.tick(lp)<1 && off.phase==4);
    // Restarting a voice resets its Hold counter via existing = {} retrigger.
    off={}; assert(off.holdElapsed==0 && off.phase==1 && off.value==0);
    // A changed Hold time while playing is read from the live per-slot snapshot.
    GlobalVoicePool::Envelope update;
    update.tick(lp); update.tick(lp);
    update.tick(hp); assert(update.phase==2);
    std::cout << "PASS TEST188: 3 independent Holds, 0 parity, Note Off, reset, automation" << std::endl;
}
'''
cpp=(cpp.replace('__SETTINGS__',settings.group(1))
       .replace('__NESTED__',nested.group(1))
       .replace('__CONVERTER__',converter)
       .replace('__TICKER__',ticker))
cxx=shutil.which('g++') or shutil.which('clang++')
if not cxx:
    print('PASS TEST188 static checks; no native C++ compiler available')
else:
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)/'ahdsr.cpp'; binary=Path(tmp)/('ahdsr.exe' if sys.platform=='win32' else 'ahdsr')
        path.write_text(cpp,encoding='utf-8')
        subprocess.run([cxx,'-std=c++17','-O2',str(path),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    print('PASS TEST188 AHDSR catalog, grid, host registration, real C++ DSP')
