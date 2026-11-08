#!/usr/bin/env python3
"""TEST181: UI-thread per-slot decisions, N relocation, optional Escape abort."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
e=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
h=(r/'Source/ChromaticPasteMapping.h').read_text(encoding='utf-8')
p=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
w=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
assert 'getExplorerCopiedAudioFiles(reaperItems, reaperError, overflow)' in e
assert 'getReaperSelectedItems(reaperItems, reaperError, overflow)' in e
assert 'continueChromaticPaste(batch)' in e
assert 'confirmChromaticPasteOverwrite(message,' in e
assert 'chooseChromaticPasteOverwrite(batch, false)' in e
assert 'chooseChromaticPasteOverwrite(batch, true)' in e
assert 'warning->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));' in e
assert 'batch->assignments.push_back({batch->items[batch->nextItem++], placed.slot, placed.note});' in e
assert 'batch->protectedSlots += placed.protectedSlots;' in e
assert 'safe->pendingChromaticPaste.reset();' in e
assert e.index('void LSampler24AudioProcessorEditor::finishChromaticPaste(') < e.index('runFileTask(explorer ?',e.index('void LSampler24AudioProcessorEditor::finishChromaticPaste('))
worker=e.split('runFileTask(explorer ?',1)[1].split('void LSampler24AudioProcessorEditor::confirmChromaticPasteOverwrite(',1)[0]
assert 'confirmChromaticPasteOverwrite(' not in worker
assert 'p.shouldStopFileTask()' in worker
assert 'p.importSampleToSlot(item.file, entry.slot,' in worker
assert 'entry.midiNote' in worker
assert 'std::shared_ptr<SharedSample> cachedSource' in worker
assert 'if (mapping.lowKey <= double(note) && double(note) <= mapping.highKey)' in h
assert 'if (slots[static_cast<std::size_t>(destination)].hasAudio && !overwrite)' in h
assert 'if (static_cast<std::size_t>(destination) >= N) return {};' in h
assert 'LSampler-24-Windows-VST3-TEST181.zip' in w
assert 'verify-test181-overwrite-choice.py' in w
print('PASS TEST181: per-slot Y/N/Escape planning, keep occupied audio on N, relocate same file, unused keys, async worker')
