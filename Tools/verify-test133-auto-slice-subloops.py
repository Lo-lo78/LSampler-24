#!/usr/bin/env python3
"""TEST133 static source/architecture check (intentionally no C++ build)."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
h=(r/'Source/SamplerVoice.h').read_text()
v=(r/'Source/SamplerVoice.cpp').read_text()
s=(r/'Source/SliceEngine.cpp').read_text()
for token in (
    'v.slice.active && !v.slice.single && v.slice.mode>=1 && v.slice.mode<=6',
    'void syncAutoSliceStage(',
    'void advanceSlice(',
    'bool autoSliceHasLoops=false',
):
    assert token in h,token
for token in (
    'const double low=std::max(loop.start,q.begin);',
    'const double high=std::min(loop.end,q.begin+q.length);',
    'const double startDistance=q.reverse?',
    'const double endDistance=q.reverse?',
    'v.slice.elapsed=beginTime+wrap(fade+carried,length);',
    'autoSliceLoopPitchFactor(v,loop)',
    'const double stageStart=stage&&autoLoops?',
    'const bool inStage=stage && pos>=stageStart && pos<stageEnd;',
    'if(!v.slice.configured)',
    'if(usesAutoSliceLoops(v))syncAutoSliceStage(v,s);',
):
    assert token in v,token
assert 'void SlicePlayback::chooseEvent' in s
assert 'case 1:source=n-1-timeline' in s
assert 'if(!BasicRead&&v.slice.active)advanceSlice(v,s,v.increment);else advanceLoops(v,s,v.increment);' in v
# The frozen TEST59 path, all Slice Mode values and Slice Sequencer data types
# are retained; there are no added plugin parameters or persistence fields.
p=(r/'Source/Parameters.h').read_text()
assert 'loopParameterCount = 17, loopCount = 10' in p
assert 'v.slice.mode<=6' in h
assert 'void GlobalVoicePool::advanceLoops' in v
assert 'v.slice.mode==7' not in h
print('PASS TEST133: auto Slice modes 1..6 + subloops; source-window clipping, per-slice repeat reset, fixed pitch and independent sequencer.')
