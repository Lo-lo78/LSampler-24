#!/usr/bin/env python3
"""TEST186 Drift/Lace: catalog, old IDs, Mono+Poly DSP and native C++ math."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
read=lambda f:(root/f).read_text(encoding="utf-8")
p=read("Source/Parameters.h")
a=read("Source/SlotAudioState.cpp")
v=read("Source/SamplerVoice.cpp")
h=read("Source/SamplerVoice.h")
host=read("Source/PluginProcessor.cpp")
assert "parameterCount = 170" in p and "int(P::midi_channel)==159" in p
assert "int(P::lfo1_drift_amount)==160" in p
assert "std::array<GridEntry, 194> grid" in p
for i in (1,2):
    for suffix in ("drift_amount","drift_cycle","lace_source","lace_rate","lace_mix"):
        key=f"lfo{i}_{suffix}"
        assert p.count('"'+key+'"')==1, key
        assert '{int(P::'+key+'), -1, Action::none, "LFO '+str(i)+'"}' in p
        assert 'P::'+key in a
assert "std::array<std::array<uint64_t,2>,slotCount> freeCycles" in h
assert "freeCycles[size_t(slot)][size_t(i)]" in v
assert "lfoTickState(monoLfo[size_t(slot)][size_t(i)]" in v
assert "lacePrevious(0)" in v and "lacePrevious(1)" in v
assert "monoLfoPreviousAudibleSamples" in v
assert 'for(int i=int(P::lfo1_drift_amount);i<parameterCount;++i)' in host
assert host.index('// TEST186: ten LFO Drift/Lace controls per slot') > host.index('// TEST173: append independent Sample Set automation')
import shutil
cxx=shutil.which('g++') or shutil.which('clang++')
if cxx:
    with tempfile.TemporaryDirectory() as tmp:
        binary=Path(tmp)/('lfo_test.exe' if __import__('os').name=='nt' else 'lfo_test')
        subprocess.run([cxx,'-std=c++17','-O2',str(root/'Tests/LfoPolyrhythmTests.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print('PASS TEST186 LFO catalog, automation order, Mono/Poly and deterministic math')
