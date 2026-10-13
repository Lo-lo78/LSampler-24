#!/usr/bin/env python3
"""TEST149 static contract: individual Slot sends, Bank master sends, no FX in Slot."""
from pathlib import Path
import json
import re
root = Path(__file__).resolve().parents[1]
source = lambda name: (root / 'Source' / name).read_text(encoding='utf8')
cpp = source('PluginProcessor.cpp')
h = source('PluginProcessor.h')
ui = source('PluginEditor.cpp')
uih = source('PluginEditor.h')
voice = source('SamplerVoice.cpp')
voices = source('SamplerVoice.h')
fx = source('FXEngine.h')
params = source('FXParameters.h')
assert 'static constexpr int slotFXSendCount = 3' in h
assert 'slotBankSendFirst = int(lsampler::grid.size()) - lsampler::globalParameterCount' in uih
assert 'static constexpr int slotBankSendCount = 3' in uih
assert 'normalGridSize + slotBankSendCount' in ui
assert '"Bank Chorus Send", "Bank Delay Send", "Bank Reverb Send"' in ui
assert 'processor.getSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(' in ui
assert 'processor.setSlotFXSend(processor.getCurrentSlot(), slotBankSendIndex(' in ui
assert 'processor.getSlotFXSendHostParameter(processor.getCurrentSlot()' in ui
assert 'if (isSlotBankSend(index)) return slotBankSendFirst' in ui
assert 'void LSampler24AudioProcessor::setSlotFXSend(' in cpp
assert 'slotClipboardFXSends' in cpp
assert 'saveFXState(bank);' in cpp and 'restoreFXState(bank);' in cpp
assert 'saveFXState(state);' in cpp and 'restoreFXState(state);' in cpp
slot_state = cpp[cpp.index('juce::ValueTree LSampler24AudioProcessor::makeSlotState('):cpp.index('bool LSampler24AudioProcessor::materialiseSlotSample(')]
assert 'saveFXState(' not in slot_state and 'fx_master' not in slot_state
for name in ('bankChorusSend','bankDelaySend','bankReverbSend'):
    assert f'tree.setProperty("{name}"' in slot_state
    assert name in cpp[cpp.index('bool LSampler24AudioProcessor::restoreSlotState'):]
assert 'tree.getProperty(sendKeys[send], oldDefault)' in cpp
assert 'slots[size_t(slotIndex)].parameters[P::output_route] < 0.5 ? 100.0 : 0.0' in cpp
assert 'for (int send = 0; send < slotFXSendCount; ++send)' in cpp
assert cpp.index('hostFX[size_t(i)] = add(') < cpp.index('hostSlotFXSends[size_t(slot)][size_t(send)] = add(')
assert 'fxSlotSendBuffers' in cpp and 'fxVoiceOnlyBuffer.copyFrom' in cpp
assert 'voicePool.setFXSendContext(buffers, &audioSlotFXSends)' in cpp
assert 'fxEngine.process(buffer, values, useSlotFXSends ? &fxSlotSendBuffers : nullptr, bpm)' in cpp
assert 'if (sendToFX)' in voice and 'fxSendBuses[size_t(send)]->addSample' in voice
assert 'const bool sendToFX = fxSendLevels != nullptr' in voice
assert 'void setFXSendContext(' in voices
assert 'const std::array<juce::AudioBuffer<float>,3>* slotSends = nullptr' in fx
assert 'const float monoIn=.5f*(dLIn+dRIn);' in fx
assert 'chorusL[size_t(chorusWrite)]=cLIn;' in fx
assert 'predelayL[size_t(preWrite)]=slotSends ? rLIn : l;' in fx
assert 'if (slotSends == nullptr)' in fx
assert 'getFXParameter(lsampler::fx::master) >= 0.5' in cpp
# Verify new IDs cannot collide with the frozen VST3 6629 or previously added effects.
old_ids=[x['id'] for x in json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())]
fx_names=re.findall(r'^\s*\{"([a-z0-9_]+)",',params,re.M)
assert len(fx_names)==37
new=[f'slot{s:02d}_bank_{part}_send' for s in range(1,25) for part in ('chorus','delay','reverb')]
assert len(new)==72 and len(set(new))==72
assert not set(old_ids).intersection(new)
assert not {f'fx_{name}' for name in fx_names}.intersection(new)
def hash31(key):
    v=0
    for char in key:v=(31*v+ord(char))&0xffffffff
    return v&0x7fffffff
new_h=set(map(hash31,new));assert len(new_h)==72
assert not new_h.intersection(set(map(hash31,old_ids + [f'fx_{name}' for name in fx_names])))
print('PASS TEST149: 72 independent Slot FX send lanes; previous IDs safe; slot/bank/state persistence; independent FX routing and accessible grid.')
