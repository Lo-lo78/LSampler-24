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

# TEST181 continues TEST180 chromatic mapping but computes the target for each
# source in a UI-thread plan with per-slot confirmations.
assert 'const int startingSlot = processor.getCurrentSlot();' in e and 'startSlotOverride >= 0 ? startSlotOverride' in e
assert 'processor.getChromaticPasteSlotMapping()' in e
assert 'batch->planner.needsOverwriteDecision()' in e
assert 'batch->planner.assign(overwrite)' in e
assert 'item.startSeconds, item.endSeconds, error, entry.midiNote' in e
assert 'int mappedMidiNote = -1' in h
for key in ('root', 'low', 'high'):
    assert f'incoming.parameters[lsampler::P::{key}] = mappedMidiNote >= 0' in p
assert 'juce::jlimit(0, 127, mappedMidiNote)' in p
assert p.count('int mappedMidiNote') == 1
assert e.count('p.importSampleToSlot(item.file, entry.slot,') == 1
# Other calls preserve the legacy -1/full-keyboard behaviour.
assert 'p.importSampleToSlot(item.file, item.slot,' in e
assert (root / 'Source/ChromaticPasteMapping.h').exists()
assert 'verify-test175-midi-learn-sws-mapping.py' in w
assert 'LSampler-24.0.99.27.vst3.zip' in w
assert 'BUILD_0.99.27.txt' in w
print('PASS TEST175+181: focused MIDI learn, RT safety, and destination-dependent chromatic pitch for all imported samples')
