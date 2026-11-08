#!/usr/bin/env python3
"""TEST176 structural gates: no Lua/SWS/clipboard needed for selected audio items."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
p = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
h = (root / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
e = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
w = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
cm = (root / 'CMakeLists.txt').read_text(encoding='utf-8')

for api in ('CountSelectedMediaItems', 'GetSelectedMediaItem', 'GetActiveTake',
            'GetMediaItemTake_Source', 'GetMediaSourceParent', 'GetMediaSourceFileName',
            'GetMediaSourceLength', 'GetMediaItemInfo_Value', 'GetMediaItemTakeInfo_Value',
            'GetTakeNumStretchMarkers', 'GetTakeName'):
    assert f'>("{api}")' in p, api
assert 'host->getReaperParent(3)' in p
assert 'void*' in p
selection = p.split('bool readSelectedItems(', 1)[1].split('bool internalSlotCopyHasPriority()', 1)[0]
for key in ('D_STARTOFFS', 'D_LENGTH', 'D_POSITION', 'D_PLAYRATE', 'D_PITCH',
            'I_CHANMODE', 'sourceParent(source) != nullptr',
            'computeUnprocessedWindow', 'std::stable_sort(candidates.begin()',
            'candidate.timelinePosition', 'candidate.data', 'lengthIsQN'):
    if key == 'candidate.timelinePosition':
        assert 'a.timelinePosition < b.timelinePosition' in selection
    else:
        assert key in selection, key
assert 'GetClipboardData' not in selection
assert 'getReaperSelectedItems' in p and 'getReaperSelectedItems' in h
assert 'shouldPreferInternalSlotPaste' in p and 'shouldPreferInternalSlotPaste' in h
assert re.search(r'!processor.shouldPreferInternalSlotPaste\(\).*?processor.getReaperSelectedItems\(reaperItems, reaperError, overflow\).*?\|\| processor.getReaperCopiedItems', e, re.S)
assert 'item.startSeconds, item.endSeconds, error, entry.midiNote' in e
assert 'processor.getChromaticPasteSlotMapping()' in e
assert ('slot.parameters[lsampler::P::root] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::root] = mappedMidiNote >= 0' in p)
assert ('slot.parameters[lsampler::P::low] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::low] = mappedMidiNote >= 0' in p)
assert ('slot.parameters[lsampler::P::high] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::high] = mappedMidiNote >= 0' in p)
assert 'ReaperItemWindow.h' in cm
assert 'Tests/ReaperItemWindowTests.cpp' in cm
assert 'verify-test176-selected-reaper-items.py' in w
assert ('LSampler-24-Windows-VST3-TEST176' in w
        or 'LSampler-24-Windows-VST3-TEST177' in w or 'LSampler-24-Windows-VST3-TEST178' in w or 'LSampler-24-Windows-VST3-TEST182' in w)
print('PASS TEST176: REAPER selected-item source windows, stable ordering, clipboard fallbacks, three matching MIDI notes')
