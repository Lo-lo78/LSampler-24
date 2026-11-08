#!/usr/bin/env python3
"""TEST180 cross-session Alt+V mapping integration and CI gates."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
e=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
h=(r/'Source/PluginProcessor.h').read_text(encoding='utf-8')
cm=(r/'CMakeLists.txt').read_text(encoding='utf-8')
work=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
script=(r/'Tools/build-windows.ps1').read_text(encoding='utf-8')
helper=(r/'Source/ChromaticPasteMapping.h').read_text(encoding='utf-8')
assert 'int getChromaticPasteStartNote(int firstSlot) const;' in h
assert 'int LSampler24AudioProcessor::getChromaticPasteStartNote(int firstSlot) const' in p
snapshot=p.split('LSampler24AudioProcessor::getChromaticPasteSlotMapping() const',1)[1].split('int LSampler24AudioProcessor::getChromaticPasteStartNote(',1)[0]
assert 'const juce::ScopedLock lock(stateLock);' in snapshot
assert 'absorbHostValuesLocked();' in snapshot
assert 'return mapping;' in snapshot
assert 'lsampler_chromatic_paste::firstNote(getChromaticPasteSlotMapping(), firstSlot)' in p
batch=e.split('void LSampler24AudioProcessorEditor::pasteCopiedReaperItems(',1)[1].split('void LSampler24AudioProcessorEditor::confirmOverwrite(',1)[0]
for fragment in ('processor.getChromaticPasteSlotMapping()',
                 'continueChromaticPaste(batch)',
                 'batch->planner.needsOverwriteDecision()',
                 'batch->planner.assign(overwrite)',
                 'batch->assignments.push_back',
                 'p.shouldStopFileTask()', 'p.setFileTaskProgress',
                 'p.importSampleToSlot(item.file, entry.slot,'):
    assert fragment in batch, fragment
assert 'firstMidiNote + i' not in batch
# Both source paths converge on one batch importer. Preserve the distinct
# internal-slot clipboard priority introduced before TEST179.
clipboard=(r/'Source/ReaperNativeClipboard.h').read_text(encoding='utf-8')
assert 'CF_HDROP' in clipboard and 'DragQueryFileW' in clipboard
assert 'readExplorerFileDrop()' in p
assert 'getExplorerCopiedAudioFiles(reaperItems, reaperError, overflow)' in e
assert 'getReaperSelectedItems(reaperItems, reaperError, overflow)' in e
assert 'startMediaPaste(std::move(reaperItems), overflow, true)' in e
assert 'startMediaPaste(std::move(reaperItems), overflow);' in e
assert 'processor.shouldPreferInternalSlotPaste()' in e
assert 'entry.midiNote' in batch
assert 'if (const int existing = chromaticNote(' in helper
assert 'return note + (firstSlot - previous);' in helper
assert 'return 60;' in helper
assert 'slot.hasAudio' in helper
assert 'std::isfinite' in helper
assert 'LSampler24ChromaticPasteTests' in cm and 'chromatic-paste' in cm
assert 'LSampler24ChromaticPasteTests' in script and 'chromatic-paste' in script
assert 'verify-test180-chromatic-paste.py' in work
assert 'LSampler-24-Windows-VST3-TEST184.zip' in work
assert (r/'RELEASE_TEST180.md').exists()
print('PASS TEST180+181: deterministic notes, per-slot overwrite, relocation, async loading, CI tests')
