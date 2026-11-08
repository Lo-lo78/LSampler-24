#!/usr/bin/env python3
"""TEST177 structural verification: async import, progress, frame-exact reload, small host state."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
processor = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
header = (root / 'Source/PluginProcessor.h').read_text(encoding='utf-8')
editor = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
workflow = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')

assert 'juce::ThreadPool fileWorker' in header
assert 'startFileTask' in processor
batch = editor.split('void LSampler24AudioProcessorEditor::pasteCopiedReaperItems(', 1)[1].split('void LSampler24AudioProcessorEditor::confirmOverwrite(', 1)[0]
assert 'runFileTask(explorer ? "Importing Explorer audio files" : "Importing REAPER items"' in batch
assert 'std::shared_ptr<SharedSample> cachedSource;' in batch
assert 'if (cachedFile != item.file)' in batch
assert '&cachedSource' in batch
assert 'p.setFileTaskProgress((double(i) + fraction) / double(total))' in batch
assert 'item.startSeconds, item.endSeconds, error, entry.midiNote' in batch
import_fn = processor.split('bool LSampler24AudioProcessor::importSampleToSlot(', 1)[1].split('bool LSampler24AudioProcessor::prepareImportPreview(', 1)[0]
assert 'loadSampleWithProgress' in import_fn
assert 'copySourceWindow' in import_fn
assert 'sourceWindowFirstFrame' in import_fn and 'sourceWindowEndFrame' in import_fn
assert 'if (batchSourceCache) *batchSourceCache = loaded;' in import_fn
assert 'progress(fraction * 0.80)' in import_fn and 'progress(0.80 + 0.20 * fraction)' in import_fn
state_fn = processor.split('juce::ValueTree LSampler24AudioProcessor::makeSlotState(', 1)[1].split('bool LSampler24AudioProcessor::restoreSlotState(',1)[0]
assert 'juce::ValueTree window("SourceWindow")' in state_fn
assert 'firstFrame' in state_fn and 'endFrame' in state_fn
assert re.search(r'if \(s.sourceWindowFirstFrame >= 0.*?\}\s*else if \(s.sampleAudioModified && s.sample\)\s*tree.addChild\(storeEditedAudio', state_fn, re.S)
assert 'if (includeSampleHashes)' in state_fn
host_state = processor.split('void LSampler24AudioProcessor::getStateInformation(',1)[1].split('void LSampler24AudioProcessor::setStateInformation(',1)[0]
assert 'makeSlotState(i, "Slot", false)' in host_state
assert 'storeEditedAudio' not in host_state
restore = processor.split('bool LSampler24AudioProcessor::restoreSlotState(',1)[1].split('bool LSampler24AudioProcessor::materialiseSlotSample(',1)[0]
assert 'tree.getChildWithName("SourceWindow")' in restore
assert 'copySourceWindow(loaded, firstFrame, endFrame, error,' in restore
assert 'slot.sourceWindowFirstFrame = firstFrame;' in restore
assert 'slot.sourceWindowEndFrame = endFrame;' in restore
assert 'slot.sampleAudioModified = (editedAudio.isValid() || sourceWindow.isValid())' in restore
assert 'slot.sourceWindowFirstFrame = -1;' in processor and 'slot.sourceWindowEndFrame = -1;' in processor
assert 'TEST177' in workflow and 'verify-test177-background-source-window.py' in workflow
assert ('LSampler-24-Windows-VST3-TEST177' in workflow or 'LSampler-24-Windows-VST3-TEST178' in workflow or 'LSampler-24-Windows-VST3-TEST184' in workflow)
assert (root / 'RELEASE_TEST177.md').exists()
print('PASS TEST177: background REAPER import, progress per chunk, source reuse, frame-exact state, no inline audio or hashing in host state')
