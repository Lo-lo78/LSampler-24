#!/usr/bin/env python3
"""Check actual prepared depth helper, routing and append-only host registration."""
from pathlib import Path
import shutil, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
read=lambda f:(root/f).read_text(encoding='utf-8')
p=read('Source/Parameters.h'); proc=read('Source/PluginProcessor.cpp'); state=read('Source/SlotAudioState.cpp');voice=read('Source/SamplerVoice.cpp')
assert 'int(P::lfo1_bp_depth)==176' in p and 'int(P::lfo2_bp_depth)==177' in p
for i in (1,2):
    assert f'{{int(P::lfo{i}_hp_depth), -1, Action::none, "LFO {i}"}},\n{{int(P::lfo{i}_bp_depth), -1, Action::none, "LFO {i}"}},' in p
assert 'for(int i=int(P::hp_key_follow);i<int(P::lfo1_bp_depth);++i)' in proc
assert proc.index('// TEST191: append') > proc.index('hostBankMacros[size_t(hpIndex)]=add(')
assert 'const auto filterDepths=lfoFilterDepths(p,i,s.lp,s.hp);' in state
assert 'l.lp=filterDepths[0];l.hp=filterDepths[1];' in state
assert 'l1*s.lfo[0].lp+l2*s.lfo[1].lp' in voice and 'l1*s.lfo[0].hp+l2*s.lfo[1].hp' in voice
assert 'l.lp!=0||l.hp!=0' in state
cxx=shutil.which('g++') or shutil.which('clang++')
if cxx:
    with tempfile.TemporaryDirectory() as tmp:
        exe=Path(tmp)/('bp_lfo.exe' if sys.platform=='win32' else 'bp_lfo')
        subprocess.run([cxx,'-std=c++17','-O2',str(root/'Tests/BpLfoDepthTests.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
else:
    print('PASS static routing; no standalone compiler available')
