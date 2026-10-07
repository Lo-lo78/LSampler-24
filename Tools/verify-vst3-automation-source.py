#!/usr/bin/env python3
"""Static automation-contract audit only. Does not configure/compile/run audio."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'VST3_AUTOMATION_MANIFEST.json').read_text())
ids=[p['id'] for p in manifest]
assert len(ids)==6629 and len(set(ids))==6629
hashes=set()
for p in manifest:
    assert p['minimum'] <= p['default'] <= p['maximum']
    h=0
    for c in p['id']:h=(31*h+ord(c))&0xffffffff
    h &= 0x7fffffff
    assert h not in hashes, p['id']
    hashes.add(h)
    assert not any(x in p['id'] for x in ('start_threshold','end_threshold','end_preview_length','selected','current_slot'))
assert all(p['id'].startswith('global_') for p in manifest[:5])
for slot in range(1,25):
    group=manifest[5+(slot-1)*276:5+slot*276]
    assert len(group)==276 and all(p['id'].startswith(f'slot{slot:02d}_') for p in group)
    assert sum(bool(re.search(r'_loop[0-9]{2}_',p['id'])) for p in group)==90
    assert sum('_slice_' in p['id'] for p in group)==13
    assert sum('_sample' in p['id'] and '_velocity_' in p['id'] for p in group)==32
processor=(root/'Source/PluginProcessor.cpp').read_text()
header=(root/'Source/HostParameter.h').read_text()
callback=header[header.index('    void storeReal'):header.index('    float getValue()')]
assert not any(x in callback for x in ('ScopedLock','new ','File','callAsync','triggerAsyncUpdate','NotifyingHost','sendValueChanged'))
assert 'const auto& audio = automatedAudio;' in processor
assert 'publishHostValuesLocked(false, slotIndex);' in processor
assert 'sendValueChangedMessageToListeners(p->getValue())' in processor
assert 'hostAutomatable(static_cast<P>(i))' in processor
assert 'absorbHostValuesLocked(); s = slots' in processor
assert 'getGlobalOutputParameter(static_cast<GlobalP>(i)), nullptr)' in processor
assert 'currentSlot' not in processor[processor.index('void LSampler24AudioProcessor::createHostParameters()'):processor.index('void LSampler24AudioProcessor::absorbHostValuesLocked()')]
print('PASS: 6629 fixed parameters; unique IDs/hashes; slot independence; atomic callback; state/UI/audio bridge markers.')
