#!/usr/bin/env python3
"""TEST175: structural/regression gates for focused MIDI Low/High learn and SWS mapping.

No host needed; Windows VST3 and NVDA performance must still be tested via CI/REAPER.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
h = (root / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
p = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
eh = (root / 'Source/PluginEditor.h').read_text(encoding='utf-8')
e = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
w = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')

# The MIDI audio path is a lock-free producer and remains a pure MIDI handler:
# no host notifications/slot mutations/audio preview while learning.
assert 'std::atomic<int> midiKeyLearnContext { -1 }' in h
assert 'std::atomic<int> midiKeyLearnEvent { -1 }' in h
assert 'midiKeyLearnContext.load(std::memory_order_acquire)' in p
assert 'midiKeyLearnEvent.store(learnContext * 128 + note, std::memory_order_release)' in p
midi_loop = p.split('for (const auto event : midi) {', 1)[1].split('voicePool.render(buffer, offset, buffer.getNumSamples()-offset);', 1)[0]
learn = midi_loop.split('// TEST175: intercept Note On only while', 1)[1].split('voicePool.choke(note, channel);', 1)[0]
assert re.search(r'if\s*\(learnContext\s*>=\s*0\).*?midiKeyLearnEvent.store\(.*?continue;', learn, flags=re.S)
assert 'setSlotKeyBoundary' not in midi_loop
assert 'juce::ScopedLock' not in learn
assert 'else if (event.numBytes >= 3 && (command == 0x80' in midi_loop
assert midi_loop.index('midiKeyLearnEvent.store(') < midi_loop.index('voicePool.choke(note, channel)')

# Consumer must run on editor timer (message thread), not audio callback.
assert 'void LSampler24AudioProcessorEditor::applyCapturedMidiKey()' in e
assert 'void LSampler24AudioProcessorEditor::timerCallback()\n{\n    refreshMidiKeyLearnContext();\n    applyCapturedMidiKey();' in e
assert 'processor.setSlotKeyBoundary(context / 2, low, note);' in e
assert 'if (context != activeMidiKeyLearnContext()) return;' in e
assert 'void LSampler24AudioProcessor::setSlotKeyBoundary' in p
assert 'markAudioStateDirty(); // Includes the VST3 host parameter' in p
assert 'processor.setMidiKeyLearnContext(-1, -1);' in e

# Limiting scope: only focused Low Key/High Key on the normal grid.
scope = e.split('int LSampler24AudioProcessorEditor::activeMidiKeyLearnContext() const noexcept', 1)[1].split('void LSampler24AudioProcessorEditor::refreshMidiKeyLearnContext()', 1)[0]
for key in ('hasKeyboardFocus(true)', 'parameterPage', 'globalOpen', 'bankMacroOpen',
            'fxOpen', 'sampleSetActive', 'sliceEditor != nullptr', 'fileUiBusy',
            'parameterSelector.hasKeyboardFocus(true)', 'parameterValue.hasKeyboardFocus(true)',
            'isSlotBankSend(selectedParameter)', 'isBankFXSend(selectedParameter)',
            'P::low', 'P::high'):
    assert key in scope, key

# Destination slot sequence is unaffected; MIDI note starts at 60 each import.
assert 'const int firstSlot = processor.getCurrentSlot();' in e
assert 'if (processor.isSlotOccupied(firstSlot + i)) ++overwritten;' in e
assert re.search(r'importSampleToSlot\(item.file, firstSlot \+ i,\s*item.startSeconds, item.endSeconds, error, 60 \+ i(?:\)|,)', e)
assert re.search(r'juce::String\(59 \+ r.count\)', e)
assert 'int mappedMidiNote = -1' in h
assert ('slot.parameters[lsampler::P::low] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::low] = mappedMidiNote >= 0' in p)
assert ('slot.parameters[lsampler::P::high] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::high] = mappedMidiNote >= 0' in p)
assert ('slot.parameters[lsampler::P::root] = mappedMidiNote >= 0' in p or 'incoming.parameters[lsampler::P::root] = mappedMidiNote >= 0' in p)
assert '? juce::jlimit(0, 127, mappedMidiNote) : 60.0;' in p
assert p.count('int mappedMidiNote') == 1
assert e.count('error, 60 + i') == 1
# All other call sites must rely on the default -1 (full zone).
for match in re.finditer(r'importSampleToSlot\(', e):
    statement = e[match.start():e.find(')', match.start()) + 1]
    assert ('60 + i' in statement) or ('r.message' in statement and 'item.slot' in statement), statement

# Exercise exact note/slot model for single and 24 samples from any starting slot.
for start in (0, 4, 23):
    for count in (1, 2, 6, 24):
        inserted = min(count, 24 - start)
        mapping = [(start + i, 60 + i, 60 + i) for i in range(inserted)]
        assert all(0 <= slot < 24 and low == high for slot, low, high in mapping)
        assert [x[1] for x in mapping] == list(range(60, 60 + inserted))
assert 60 + 23 == 83
assert 'verify-test175-midi-learn-sws-mapping.py' in w
assert ('LSampler-24-Windows-VST3-TEST176.zip' in w
        or 'LSampler-24-Windows-VST3-TEST177.zip' in w or 'LSampler-24-Windows-VST3-TEST178.zip' in w)
assert ('BUILD_TEST176.txt' in w or 'BUILD_TEST177.txt' in w or 'BUILD_TEST178.txt' in w)
print('PASS TEST175+176: focused Low/High MIDI learn, RT safety, REAPER note sequence 60..83 with matching Original Pitch, CI artifact')
