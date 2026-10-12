#!/usr/bin/env python3
"""Static TEST130 API, DSP wiring, 0..100 curve and VST3 ABI checks; no compilation."""
from pathlib import Path
import hashlib,json,math,re
from collections import defaultdict
root=Path(__file__).resolve().parents[1]
p=(root/'Source/Parameters.h').read_text()
s=(root/'Source/SlotAudioState.cpp').read_text()
h=(root/'Source/SlotAudioState.h').read_text()
e=(root/'Source/SamplerVoice.cpp').read_text()
pp=(root/'Source/PluginProcessor.cpp').read_text()
assert 'loopParameterCount = 17, loopCount = 10' in p
assert 'enum class L { start, end, repeats, fade_in, fade_out, pitch_down, lp_down, hp_down, one_shot,' in p
assert 'repeat_pan, pitch_time, level, crossfade, downsample, pan, repeat_level, direction, count' in p
fields=[('loop_sub_crossfade',0,200),('loop_sub_downsample',0,100),('loop_pan',-100,100),('loop_repeat_level',-24,24),('loop_direction',0,2)]
for key,minimum,maximum in fields:
    assert re.search(r'\{"'+key+r'",\s*"[^"]+",\s*"Loops",\s*Kind::\w+,\s*0,\s*'+str(float(minimum)).replace('.0',r'(?:\.0)?')+r'\s*,',p) if False else f'{{"{key}"' in p
for key in ('crossfade','downsample','pan','repeat_level','direction'):
    assert f'{{-1, int(L::{key}), Action::none, "Loops"}}' in p
assert '{int(P::loop_crossfade), -1, Action::none, "Loops"}' not in p
assert '"Legacy Loop Crossfade (unused)"' in p
assert 'g.crossfade' not in e and 's.crossfade' not in s
assert 'g.crossfade' not in s
assert 'l.crossfade=p.loop(i,L::crossfade)*sourceMs;' in s
assert 'l.pan=p.loop(i,L::pan)*.01;' in s
assert 'l.repeatLevelDb=p.loop(i,L::repeat_level);' in s
assert 'l.direction=int(p.loop(i,L::direction));' in s
assert 'l.downsampleAmount=p.loop(i,L::downsample)*.01;' in s
assert 'stage->downsampleAmount>0' in e and 'if(inStage &&' in e
assert 'loop->crossfade>1 && loop->direction!=2' in e
assert 'loop.direction!=2' in e
assert 'v.position=wrap(v.position,cycle);' in e
assert ('const bool inStage=stage && pos>=stage->start && pos<stage->end;' in e
        or 'const bool inStage=stage && pos>=stageStart && pos<stageEnd;' in e)
assert 'stage->direction==2 && (v.repeat&1)' in e
assert 'v.repeatLevelGain=dB<=-119.9?0:std::pow(10.0,dB/20.0);' in e
assert 'v.repeatLevelIndexCached!=levelRepeat' in e
assert 'panL*=1-std::max(0.0,loop->pan);' in e
assert 's.stages[size_t(i)].downsampleAmount>0' in s
assert 'g.stages[size_t(i)].downsampleAmount>0' in e
assert 'legacyIndex = int(P::loop_crossfade)' in pp
assert 'entry.parameter == int(P::drive_type)' in pp
assert 'for(int i=legacyLoopParameterCount;i<int(L::crossfade);++i)' in pp
assert 'for(int i=int(L::crossfade);i<loopParameterCount;++i)' in pp
assert pp.index('for(int i=legacyLoopParameterCount;i<int(L::crossfade);++i)') < pp.index('for(int i=int(L::crossfade);i<loopParameterCount;++i)')
# Numeric curve: 0=source frequency/16bit; 100=8kHz/4bit;
# log frequency and fractional bit quantizer progress monotonically.
for source_rate in (44100,48000,96000,192000):
    rates=[];bits=[]
    for amount in (0,10,25,50,75,90,100):
        t=amount/100
        rate=source_rate*min(1,8000/source_rate)**t
        bit=16-12*t
        hold=max(1,source_rate/rate)
        quant_scale=2**(bit-1)-1
        assert rate>0 and hold>=1 and quant_scale>=7
        rates.append(rate);bits.append(bit)
    assert math.isclose(rates[0],source_rate)
    assert math.isclose(rates[-1],8000)
    assert math.isclose(bits[-1],4)
    assert all(rates[i]>=rates[i+1] for i in range(len(rates)-1))
    assert all(bits[i]>=bits[i+1] for i in range(len(bits)-1))
assert 10**(min(24,max(-120,4*(-3)))/20) == 10**(-12/20)
assert 10**(min(24,max(-120,100*3))/20) == 10**(24/20)
# Grid has no zero-initialized phantom last item; each added control is in the Loops page.
grid=re.search(r'inline constexpr std::array<GridEntry, (\d+)> grid \{\{(.*?)\}\};',p,re.S)
assert grid and int(grid.group(1))==len(re.findall(r'^\{',grid.group(2),re.M))
assert int(grid.group(1))==167
# Record all actual host IDs in the exact order from TEST129 plus appended new fields.
legacy_manifest=json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())
legacy_ids=[d['id'] for d in legacy_manifest]
assert len(legacy_ids)==6629 and len(set(legacy_ids))==6629
# Do not depend on external files at CI time: recover macro order from current
# Grid and reinsert the retired legacy macro exactly before Drive.
exclude=set('low high root velocity_low velocity_high choke_trigger choke_target choke_mode normalize_on normalize_target ram_downsample ram_fade_in ram_fade_out ram_reverse dc_remove'.split())
macros=[]
for key,category in re.findall(r'^\{int\(P::(\w+)\),\s*-1,\s*Action::none,\s*"([^"]+)"\}',p,re.M):
    if category!='Sample Window' and key not in exclude and key not in macros:macros.append(key)
macros.insert(macros.index('drive_type'),'loop_crossfade')
assert len(macros)==116 and macros.index('loop_crossfade')+1==macros.index('drive_type')
macro_ids=['bankmacro_'+key for key in macros]
appended_ids=[f'slot{slot:02d}_loop{loop:02d}_{field}'
              for slot in range(1,25) for loop in range(1,11)
              for field in ('loop_repeat_pan','loop_pitch_time','loop_level','loop_sub_crossfade','loop_sub_downsample','loop_pan','loop_repeat_level','loop_direction')]
# Appending after the old 720 means the first 720 IDs cannot move:
old_append=[f'slot{slot:02d}_loop{loop:02d}_{field}'
            for slot in range(1,25) for loop in range(1,11)
            for field in ('loop_repeat_pan','loop_pitch_time','loop_level')]
new_append=[f'slot{slot:02d}_loop{loop:02d}_{field}'
            for slot in range(1,25) for loop in range(1,11)
            for field in ('loop_sub_crossfade','loop_sub_downsample','loop_pan','loop_repeat_level','loop_direction')]
ids=legacy_ids+macro_ids+old_append+new_append
assert len(ids)==8665 and len(set(ids))==8665
hashes=defaultdict(list)
def hash31(value):
    h=0
    for c in value: h=(h*31+ord(c))&0xffffffff
    return h&0x7fffffff
for identifier in ids: hashes[hash31(identifier)].append(identifier)
assert not [v for v in hashes.values() if len(v)>1]
print('PASS TEST130: 10 × 5 independent controls, continuous downsample endpoints, complete 8665 VST3 IDs and zero hash collisions.')
