"""Structural guard for TEST178 REAPER Alt+V nonblocking and NVDA focus.

This verifies the integration contracts; Windows VST3 compilation and runtime
REAPER/NVDA interaction are still required before claiming performance results.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
processor = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
header = (root / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
editor = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
workflow = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')

assert 'std::array<std::weak_ptr<SharedSample>, sampleSetSize> owners' in header
assert 'SlotPreparationCache::matches(' in processor
assert 'SlotPreparationCache::remember(' in processor
assert 'preparedSampleRate == buildRate' in processor
assert 'auto audioState = buildPreparedSlotAudio(incoming, buildRate, 0);' in processor
assert 'preparationCache[size_t(slotIndex)].remember(slot, buildRate, audioState);' in processor
assert 'cache.matches(slot, preparedSampleRate)' in processor
assert 'state = cache.prepared;' in processor
assert 'buildPreparedSlotAudio(slot, preparedSampleRate, ++nextRevision)' in processor
assert 'applySampleSetGlobalWindow(state);' in processor
assert 'rate != sampleRate' in processor
for field in ('velocityLow !=', 'velocityHigh !=', 'pitchCents !=', 'octave !=',
              'pan !=', 'volumeDb !=', 'delayMs !=', 'thresholdStartFrame !=',
              'thresholdEndFrame !=', 'slice.boundaries !=', 'slice.steps[step].values !='):
    assert field in processor, field
assert 'remembered.owner_before(owner) || owner.owner_before(remembered)' in processor
assert 'fileTaskTitle == "Importing REAPER items" || fileTaskTitle == "Importing Explorer audio files"' in editor
assert 'status.setWantsKeyboardFocus(!nonModalReaperImport);' in editor
assert 'if (!nonModalReaperImport && isShowing()' in editor
assert 'status.addKeyListener(this)' in editor
assert 'fileTaskTitle == "Importing Explorer audio files"' in editor
assert 'hasKeyboardFocus(true) : status.hasKeyboardFocus(true)' in editor
assert 'runFileTask(explorer ? "Importing Explorer audio files" : "Importing REAPER items"' in editor
assert 'Verify TEST178' in workflow
assert 'LSampler-24-Windows-VST3-TEST181' in workflow
print('PASS TEST178: per-slot weak-owner cache, background DSP preparation, preserved NVDA progress, no focus hijack for REAPER Alt+V')
