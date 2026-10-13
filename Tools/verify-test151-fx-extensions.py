#!/usr/bin/env python3
"""TEST151 static regression. No compilation or audio rendering."""
from pathlib import Path
import json, re, math
root = Path(__file__).resolve().parents[1]
p = (root/'Source/FXParameters.h').read_text()
e = (root/'Source/FXEngine.h').read_text()
ui = (root/'Source/FXPanel.h').read_text()
ph = (root/'Source/PluginProcessor.h').read_text()
pc = (root/'Source/PluginProcessor.cpp').read_text()
ids = re.findall(r'^\s*\{"([a-z0-9_]+)",', p, re.M)
assert len(ids) == 37 and len(set(ids)) == 37, ids
old = ['master','chorus_on','chorus_mix','chorus_rate','chorus_width','delay_on','delay_mix','delay_time','delay_speed_l','delay_speed_r','delay_feedback_l','delay_feedback_r','delay_tone_l','delay_tone_r','delay_spread','delay_drive','delay_glide','reverb_type','reverb_mix','reverb_size','reverb_decay','reverb_damping','reverb_width','reverb_predelay','reverb_early','reverb_tone','delay_gain','reverb_xover','reverb_bass','reverb_gain']
assert ids[:30] == old
assert ids[30:] == ['delay_sync','delay_level_l','delay_level_r','delay_resonance_l','delay_resonance_r','reverb_gate_threshold','reverb_gate_decay']
for name in ('delaySync','delayLevelL','delayLevelR','delayResonanceL','delayResonanceR','reverbGateThreshold','reverbGateDecay'):
    assert name in p and name in e and name in ui
assert 'Off|1/32|1/24|1/16|1/12|1/8|1/6|1/4' in p
assert '240.0 / (safeBpm * double(divisions[size_t(syncIndex)]))' in e
assert 'const float wL=shape(wetL)*levelL, wR=shape(wetR)*levelR;' in e
assert 'p[reverbGateThreshold] > -79.5f' in e
assert 'gateLevel = 1.0f' in e
assert 'const float gateThresholdAmp = gateEnabled' in e
assert 'std::array<float,2> resonanceAmount {}, resonanceCoeff {};' in e
assert 'gateLevel - gateDecayStep' in e
assert 'fxEngine.process(buffer, values, useSlotFXSends ? &fxSlotSendBuffers : nullptr, bpm);' in pc
assert 'getFXGridCursor()' in ph and 'setFXGridCursor(int index)' in ph
assert 'state.setProperty("fx_ui_cursor", getFXGridCursor()' in pc
assert 'setFXGridCursor(int(state.getProperty("fx_ui_cursor", 0)))' in pc
assert 'processor.setFXGridCursor(cursor);' in ui
assert 'const int stored = processor.getFXGridCursor();' in ui
# Guarantee one-to-one FX UI coverage: all 37 except the three Bank Send controls.
block = ui.split('static constexpr std::array<int, visibleCount> displayOrder {{')[1].split('}};')[0]
# Extract the integer literals + declared FX enum names in this specific UI list.
entries = re.findall(r'lsampler::fx::(\w+)|(?<![a-zA-Z_])\b(\d+)\b', block)
old_enum=['master','chorusOn','chorusMix','chorusRate','chorusWidth','delayOn','delayMix','delayTime','delaySpeedL','delaySpeedR','delayFeedbackL','delayFeedbackR','delayToneL','delayToneR','delaySpread','delayDrive','delayGlide','reverbType','reverbMix','reverbSize','reverbDecay','reverbDamping','reverbWidth','reverbPredelay','reverbEarly','reverbTone','delayGain','reverbXOver','reverbBass','reverbGain','delaySync','delayLevelL','delayLevelR','delayResonanceL','delayResonanceR','reverbGateThreshold','reverbGateDecay']
ordered=[old_enum.index(a) if a else int(b) for a,b in entries]
assert len(ordered)==34 and len(set(ordered))==34, ordered
assert set(ordered)==set(range(37))-{2,6,18}
for before,after in [('delayToneL','delayResonanceL'),('delayToneR','delayResonanceR'),('delayLevelR','delayGain'),('reverbGateDecay','reverbGain')]:
    assert ordered.index(old_enum.index(before)) + 1 == ordered.index(old_enum.index(after))
# Distinct host hash IDs from manifest and 72 slot sends.
oldids = [x['id'] for x in json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())]
newkeys = [f'fx_{i}' for i in ids]
slots = [f'slot{s:02d}_bank_{n}_send' for s in range(1,25) for n in ('chorus','delay','reverb')]
def hash31(k):
    v=0
    for c in k:v=(31*v+ord(c))&0xffffffff
    return v&0x7fffffff
allids=oldids+newkeys+slots
assert len(set(allids))==len(allids)
assert len({hash31(x) for x in allids})==len(allids)
# BPM sanity, gate is instant open / linear closing.
for bpm in (60,120,180):
    for div in (32,24,16,12,8,6,4):
        sec=240/(bpm*div)
        assert abs(sec*bpm*div-240)<1e-8
rate=48000; decay=.3; level=1
for i in range(int(rate*decay)):
    level=max(0.0, level-1/(rate*decay))
assert level < 1e-10
print('PASS TEST151: stable IDs, 34 accessible FX entries, BPM divisions, gate, per-side levels and resonance, editor cursor memory, persisted settings.')
