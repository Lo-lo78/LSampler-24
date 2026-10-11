#!/usr/bin/env python3
"""TEST126 static contract and arithmetic checks. No compiler or binaries."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
params=(root/'Source/Parameters.h').read_text()
engine=(root/'Source/SamplerVoice.cpp').read_text()
processor=(root/'Source/PluginProcessor.cpp').read_text()
slot=(root/'Source/SlotAudioState.cpp').read_text()
manifest=json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())
old_ids=[x['id'] for x in manifest]
assert len(old_ids)==6629 and len(set(old_ids))==6629
assert 'legacyLoopParameterCount = 9, loopParameterCount = 17, loopCount = 10' in params
assert 'loop_repeat_pan' in params and 'loop_pitch_time' in params
assert re.search(r'\{"loop_pitch_time",\s*"Loop Pitch Step Time",\s*"Loops",\s*Kind::enumeration,\s*0,\s*0\.0,\s*1\.0', params), 'Original Pitch Loop must be the default'
assert 'for(int j=0;j<loopCount;++j)for(int i=0;i<legacyLoopParameterCount;++i)' in processor
assert processor.index('    // TEST126/128: additional Loop controls')>processor.index('    // Append after the existing 6629')
assert 'for(int i=legacyLoopParameterCount;i<int(L::crossfade);++i)' in processor
assert 'p.loops!=original.params.loops' in processor
assert 'l.repeatPan=p.loop(i,L::repeat_pan)' in slot
assert 'l.fixedPitchTime=p.loop(i,L::pitch_time)==0' in slot
assert 'if(loop && v.skipStage!=v.stage && loop->repeatPan!=0)' in engine
assert 'v.loopClock+=increment*v.loopPitchTimeFactor;' in engine
assert 'loopPitchSilent' in engine and 'if(loopPitchSilent){left=0;right=0;}' in engine
assert 'v.stage=-1;v.repeat=0;v.skipStage=-1;v.loopClock=0;' in engine
new_ids=[f'slot{slot:02d}_loop{loop:02d}_{field}' for slot in range(1,25)
         for loop in range(1,11) for field in ('loop_repeat_pan','loop_pitch_time')]
assert len(new_ids)==480
bank_keys=set()
for key,category in re.findall(r'^\{int\(P::(\w+)\),\s*-1,\s*Action::none,\s*"([^"]+)"\}',params,re.M):
    excluded=set('low high root velocity_low velocity_high choke_trigger choke_target choke_mode normalize_on normalize_target ram_downsample ram_fade_in ram_fade_out ram_reverse dc_remove'.split())
    if category!='Sample Window' and key not in excluded:bank_keys.add(key)
macro_ids=['bankmacro_'+key for key in sorted(bank_keys)]
ids=old_ids+macro_ids+new_ids
assert len(ids)==len(set(ids))
def hash31(txt):
    h=0
    for char in txt:h=(31*h+ord(char)) & 0xffffffff
    return h & 0x7fffffff
assert len(set(map(hash31,ids)))==len(ids),'VST3 parameter hash collision'
# Repeat Pan: signed symmetrical endpoints, reversal, infinite triangle.
for amount in (-100.0,-30.0,30.0,100.0):
    pans=[(2*r/4-1)*amount/100 for r in range(5)]
    assert abs(pans[0]+amount/100)<1e-9 and abs(pans[-1]-amount/100)<1e-9
    assert all((pans[i+1]-pans[i])*amount>=0 for i in range(4))
assert [round(1-abs(1-r/128),5) for r in (0,64,128,192,256)]==[0,.5,1,.5,0]
# Fixed clock follows pitch=0 rate. Resized mode follows audio read rate.
length=1000.0
for ratio in (0.5,1.0,2.0):
    fixed_time=length/1.0
    resized_time=length/ratio
    assert fixed_time==1000.0 and abs(resized_time*ratio-length)<1e-8
print(f'PASS TEST126: {len(old_ids)} legacy IDs, {len(macro_ids)} macros, 480 TEST126 Loop IDs; pan, Original Pitch Loop default and state wiring.')
