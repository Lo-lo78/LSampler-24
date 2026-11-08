#!/usr/bin/env python3
"""TEST183: static safety gates for user-confirmed cache cleanup."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
e=(r/'Source/PluginEditor.cpp').read_text()
p=(r/'Source/PluginProcessor.cpp').read_text()
m=(r/'Source/MediaPasteExpansion.cpp').read_text()
h=(r/'Source/ExtractedCacheFilename.h').read_text()
w=(r/'.github/workflows/build-windows.yml').read_text()
s=(r/'Tools/build-windows.ps1').read_text()
assert 'menu.addItem(21, "Clear Extracted Audio Cache...")' in e
assert 'menu.addItem(20, "Open Extracted Audio Cache Folder")' in e
assert 'confirmOverwrite(' in e[e.index('void LSampler24AudioProcessorEditor::clearExtractedAudioCache()'):e.index('void LSampler24AudioProcessorEditor::showAdvancedMenu()')]
assert 'a CLOSED REAPER project may still reference extracted WAV files' in e
assert 'LSampler24AudioProcessor::getAllOpenInstanceSampleReferences()' in e
assert 'runFileTask("Clearing extracted audio cache"' in e
assert 'getAllOpenInstanceSampleReferences()' in p
assert 'slotClipboardHasData' in p[p.index('std::vector<juce::File> LSampler24AudioProcessor::getAllOpenInstanceSampleReferences()'):]
assert 'std::unique_lock<std::shared_mutex>' in m and 'std::shared_lock<std::shared_mutex>' in m
assert 'directory.findChildFiles(children, juce::File::findFiles, false)' in m
assert 'isGeneratedFileName(file.getFileName().toStdString())' in m and 'file.deleteFile()' in m
assert 'std::regex_match' in h
assert 'LSampler24CacheCleanupTests' in s and 'extracted-cache-cleanup' in s
assert 'LSampler-24-Windows-VST3-TEST183.zip' in w and 'verify-test183-cache-cleanup.py' in w
print('PASS TEST183: confirmed user clear, generated-WAV filter, live references, worker/background, Windows CI')
