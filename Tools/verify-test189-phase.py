#!/usr/bin/env python3
"""TEST189: appended slot Phase, host/macro lanes, DSP placement and rotator math."""
from pathlib import Path
import shutil, subprocess, sys, tempfile
root = Path(__file__).resolve().parents[1]
read = lambda path: (root / path).read_text(encoding='utf-8')
p = read('Source/Parameters.h')
state = read('Source/SlotAudioState.cpp')
state_h = read('Source/SlotAudioState.h')
voice = read('Source/SamplerVoice.cpp')
voice_h = read('Source/SamplerVoice.h')
host = read('Source/PluginProcessor.cpp')
rotation = read('Source/PhaseRotation.h')
tests = read('Tests/PhaseRotationTests.cpp')
cmake = read('CMakeLists.txt')
workflow = read('.github/workflows/build-windows.yml')
build = read('Tools/build-windows.ps1')

# Catalog: Phase is appended after every frozen ID and appears after Pan in the
# accessible Slot page, while the VST3 ID remains at the end of the host batch.
assert 'parameterCount = 178' in p
assert 'std::array<GridEntry, 202> grid' in p
assert 'int(P::env_hold)==170' in p and 'int(P::lp_env_hold)==171' in p
assert 'int(P::hp_env_hold)==172' in p and 'int(P::phase)==173' in p
assert '{"phase", "Phase", "Slot", Kind::continuous, 0, -180, 180, 1, 15, "°", 0, ""}' in p
assert p.index('{int(P::pan), -1, Action::none, "Slot"},') < p.index('{int(P::phase), -1, Action::none, "Slot"},')
assert p.index('{int(P::phase), -1, Action::none, "Slot"},') < p.index('{int(P::low), -1, Action::none, "Mapping"},')
assert p.index('// TEST189: slot signal phase, appended after every existing ID.') < p.index('    count\n')

# Host registration: one per-slot Phase lane plus one Bank Macro lane, both
# appended after the complete TEST188 batch.
assert 'h.values[size_t(P::phase)]=add(id+d.key' in host
assert '"slot"+juce::String(slot+1).paddedLeft(\'0\',2)+"_"' in host
assert host.index('// TEST189: one Phase control per slot') > host.index('// TEST188: three AHDSR Hold controls')
assert 'for(int i=int(P::phase);i<int(P::hp_key_follow);++i)' in host
assert 'hostBankMacros[size_t(i)]=add("bankmacro_"+juce::String(d.key)' in host

# Prepared slot state and per-voice rotator state.
assert 'PhaseRotation phase;' in state_h
assert 's.phase=phaseRotation(v(P::phase),sr);' in state
assert 'double phaseCoefficient=1.0,phaseSign=1.0;' in voice_h
assert 'double phaseX1[2]{},phaseY1[2]{};' in voice_h
assert voice.count('v.phaseCoefficient=s.phase.coefficient;v.phaseSign=s.phase.sign;') == 2
assert 'if(!phaseActive) {' in voice and 'v.phaseCoefficient=1.0;v.phaseSign=1.0;' in voice

# DSP order: after the machine stage, before de-click fades, sends and output.
machine = voice.index('if(!BasicRead&&s.machine)applyMachine(v,s,left,right);')
rotator = voice.index('if(phaseActive) {', machine)
assert machine < rotator
assert voice.index('v.phaseCoefficient+=(s.phase.coefficient-v.phaseCoefficient)*s.phase.smoothing;', rotator) > rotator
assert voice.index('const double yL=phaseAllPass(left,v.phaseX1[0],v.phaseY1[0],v.phaseCoefficient);', rotator) > rotator
assert voice.index('if(v.fadeInLeft>0)', rotator) > rotator

# Zero-latency first-order all-pass: unity magnitude, 1 kHz reference, exact
# bypass at 0 degrees and exact polarity inversion at +/-180 degrees.
assert 'struct PhaseRotation' in rotation
assert 'inline PhaseRotation phaseRotation(' in rotation
assert 'referenceHz = 1000.0' in rotation
assert 'r.coefficient = (k - t) / (k + t);' in rotation
assert 'r.sign = invert ? -1.0 : 1.0;' in rotation
assert 'const double y = coefficient * (x - previousOutput) + previousInput;' in rotation

# The standalone math test is part of CMake, the Windows build and CI.
assert 'add_executable(LSampler24PhaseRotationTests Tests/PhaseRotationTests.cpp)' in cmake
assert 'add_test(NAME phase-rotation COMMAND LSampler24PhaseRotationTests)' in cmake
assert 'LSampler24PhaseRotationTests' in build and '-R "^phase-rotation$"' in build
assert 'python Tools/verify-test189-phase.py' in workflow
assert 'std::abs(actual - requested) < 1.0e-6' in tests
assert 'std::abs(degrees(std::arg(response(positive180, reference, sr))) - 180.0)' in tests
assert 'std::abs(degrees(std::arg(response(negative180, reference, sr))) + 180.0)' in tests

cxx = shutil.which('g++') or shutil.which('clang++')
if cxx:
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / ('phase_test.exe' if sys.platform == 'win32' else 'phase_test')
        subprocess.run([cxx, '-std=c++17', '-O2', str(root / 'Tests/PhaseRotationTests.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print('PASS TEST189 Phase catalog, append-only host lanes, DSP order and native rotator math')
else:
    print('PASS TEST189 Phase catalog, append-only host lanes and DSP order; no native C++ compiler available')
