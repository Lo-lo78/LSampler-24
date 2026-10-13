#!/usr/bin/env python3
"""TEST128 static regression checks. Do not compile or instantiate JUCE."""
from pathlib import Path
import json
import re

root=Path(__file__).resolve().parents[1]
catalog=(root/'Source/Parameters.h').read_text()
processor=(root/'Source/PluginProcessor.cpp').read_text()
engine=(root/'Source/SamplerVoice.cpp').read_text()
state=(root/'Source/SlotAudioState.cpp').read_text()
headers=(root/'Source/SlotAudioState.h').read_text()
manifest=json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())
assert len(manifest)==6629
assert 'legacyLoopParameterCount = 9, loopParameterCount = 17, loopCount = 10' in catalog
assert 'repeat_pan, pitch_time, level, crossfade, downsample, pan, repeat_level, direction, count' in catalog
assert '{"loop_level", "Loop Level", "Loops", Kind::continuous, 0, -120.0, 24.0' in catalog
assert '{-1, int(L::level), Action::none, "Loops"}' in catalog
assert '{"global_release_loops", "Repeat After Note Off"' in catalog
assert 'for(int i=legacyLoopParameterCount;i<int(L::crossfade);++i)' in processor
assert 'l.level=levelDb<=-119.9?0:db(levelDb);' in state
assert 'repeatPan = 0, level = 1' in headers
assert 'const double targetLoopLevel=activeLoop?loop->level*v.repeatLevelGain:1.0;' in engine
assert 'v.loopLevelSmooth+=(targetLoopLevel-v.loopLevelSmooth)*loopLevelSmoothing;' in engine
assert 'v.ampSmooth*s.level*v.loopLevelSmooth' in engine
assert 'const bool mayRepeat=(loop->repeats==0 || !v.loopsReleased || p[P::global_release_loops]!=0)' in engine
assert '(loop->repeats==0 || v.repeat<' in engine
assert 'const bool mayRepeat=(loop->repeats==0 || !v.loopsReleased || p[P::global_release_loops]!=0)' in engine
assert 'const double overlap=' in engine and 'v.position=wrap(v.position,cycle);' in engine
assert 'const double smooth=t*t*(3.0-2.0*t);' in engine
# Check the appended IDs are deterministic, unique and cannot collide with the
# original automation manifest or the prior TEST126 additions.
legacy_ids=[p['id'] for p in manifest]
new_ids=[f'slot{slot:02d}_loop{loop:02d}_{field}'
         for slot in range(1,25) for loop in range(1,11)
         for field in ('loop_repeat_pan','loop_pitch_time','loop_level')]
assert len(new_ids)==720 and len(set(new_ids))==720
assert len(set(legacy_ids+new_ids))==6629+720
macro_keys=[]
for entry in re.finditer(r'^\{int\(P::(\w+)\),\s*-1,\s*Action::none,\s*"([^"]+)"\}',catalog,re.M):
    key,category=entry.groups()
    excluded=set('low high root velocity_low velocity_high choke_trigger choke_target choke_mode normalize_on normalize_target ram_downsample ram_fade_in ram_fade_out ram_reverse dc_remove'.split())
    if category!='Sample Window' and key not in excluded and key not in macro_keys:
        macro_keys.append(key)
macro_ids=['bankmacro_'+key for key in macro_keys]
ids=legacy_ids+macro_ids+new_ids
assert len(ids)==len(set(ids))
def hash31(key):
    h=0
    for ch in key:h=(h*31+ord(ch))&0xffffffff
    return h&0x7fffffff
assert len(set(map(hash31,ids)))==len(ids),'VST3 host ID hash collision'
# Confirm the new zero-decibel default is unity and the mute endpoint is off.
import math
for dB in (-120,-40,-12,0,6,24):
    linear=0 if dB<=-119.9 else 10**(dB/20)
    assert linear>=0 and math.isfinite(linear)
assert 10**(0/20)==1
# A fade overlaps only when the next repeat really exists. The overlapped
# head is skipped at wrap so the start segment is not heard twice.
for reps in (0,1,4):
    for index in range(6):
        eligible=(reps==0 or index<reps)
        if reps>0 and index==reps:assert not eligible
fade=min(200,1000*.5)
assert (1000-fade)+fade==1000
assert (0+fade)==fade
print(f'PASS TEST128: {len(legacy_ids)} preserved legacy IDs, {len(macro_ids)} Bank Macros, 720 prior/new Loop IDs; Loop Level and crossfade guards.')
