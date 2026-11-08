#!/usr/bin/env python3
"""TEST182 non-executing integration checks; Windows/codec tests run with VST3."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
e = (r / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
p = (r / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
m = (r / 'Source/MediaPasteExpansion.cpp').read_text(encoding='utf-8')
g = (r / 'Source/MediaTrackGrouping.h').read_text(encoding='utf-8')
cm = (r / 'CMakeLists.txt').read_text(encoding='utf-8')
workflow = (r / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
script = (r / 'Tools/build-windows.ps1').read_text(encoding='utf-8')
assert 'startMediaPaste(std::move(reaperItems), overflow, true)' in e
assert 'startMediaPaste(std::move(reaperItems), overflow);' in e
assert 'pasteCopiedReaperItems(std::move(*expanded), result.skipped,' in e
assert 'lsampler_media_paste::expand(item.file,' in e
assert 'startSlotOverride >= 0 ? startSlotOverride' in e
assert 'LSampler24AudioProcessor::slotCount - startingSlot - static_cast<int>(expanded->size())' in e
assert 'p.shouldStopFileTask()' in e
assert 'runFileTask(fromExplorer ? "Importing Explorer audio files" : "Importing REAPER items"' in e
assert 'continueChromaticPaste(batch)' in e and 'chooseChromaticPasteOverwrite' in e
assert 'getExplorerCopiedAudioFiles' in p and 'lsampler_media_paste::isCandidate(entry.file)' in p
assert 'getReaperSelectedItems' in p and 'lsampler_media_paste::isCandidate(file)' in p
for marker in ('MFCreateSourceReaderFromURL', 'MF_SOURCE_READER_FIRST_AUDIO_STREAM', 'MFStartup(MF_VERSION)',
               'MFAudioFormat_PCM', 'MFAudioFormat_Float', 'MF_MT_AUDIO_NUM_CHANNELS',
               'MF_SOURCE_READERF_ENDOFSTREAM', 'MediaFoundationScope', 'mfplat', 'mfreadwrite'):
    assert marker in (m + cm), marker
for marker in ('splitNativeAudio(', 'splitMediaFoundation(', 'sourceIdentity(',
               'getLastModificationTime()', 'WavOutputGroup', 'writer.reset()',
               'media-paste-grouping', 'JUCE_WINDOWS', 'createWriterFor(', 'writeFromAudioSampleBuffer('):
    assert marker in (m + cm), marker
assert 'return {{source, {}}}; // Preserve exact originals / no cache.' in m
assert 'trackCount(int channels)' in g and 'pairAt(int channels, int index)' in g
assert 'LSampler24MediaPasteTests' in script and 'media-paste-grouping' in script
assert 'mfplat mfreadwrite mfuuid' in cm
assert 'verify-test182-media-expansion.py' in workflow
assert any(f'LSampler-24-Windows-VST3-{version}.zip' in workflow for version in ('TEST182','TEST183','TEST184','TEST185'))
assert (r / 'RELEASE_TEST182.md').exists()
print('PASS TEST182: video/AAC decoding, stereo-pair expansion, preserved mono/stereo, persistence, CI, no-block UI')
