#!/usr/bin/env python3
"""Static assertions for TEST169 SWS/BR path clipboard feature (runtime on Windows still required)."""
from pathlib import Path
r=Path(__file__).resolve().parent.parent
h=(r/'Source/ReaperNativeClipboard.h').read_text(encoding='utf-8')
p=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
e=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
w=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
for key in ('parseMediaSourcePaths(', 'juce::StringArray::fromLines', 'file.existsAsFile()',
            'CF_UNICODETEXT', 'CF_TEXT', 'MultiByteToWideChar(CP_ACP',
            'parseMediaSourcePaths(readText(CF_UNICODETEXT))',
            'parseMediaSourcePaths(readText(CF_TEXT))',
            'parseReaperItemText(readText(f))', 'readNativeReaperAudioItems'):
    assert key in h, key
assert h.index('parseMediaSourcePaths(readText(CF_UNICODETEXT))') < h.index('parseReaperItemText(readText(f))')
assert 'hasSupportedExtension(entry.file)' in p
assert 'slotCopyIsLatest && GetClipboardSequenceNumber() == slotCopySequence' in p
assert 'importSampleToSlot(item.file, firstSlot + i,' in e
assert '_BR_TSOURCE_PATH_TO_CLIPBOARD' in (r/'TEST169_SWS_COPY_SOURCE_PATHS_NOTE_IT.txt').read_text(encoding='utf-8')
assert any(f'LSampler-24-Windows-VST3-{v}.zip' in w for v in ('TEST169','TEST170','TEST173','TEST174'))
assert 'Tools/verify-test169-sws-source-paths.py' in w
assert 'LSampler24ClipboardTests' in (r/'CMakeLists.txt').read_text(encoding='utf-8')
assert 'clipboard-paths' in (r/'Tools/build-windows.ps1').read_text(encoding='utf-8')
assert 'charToString(0x00f2)' in (r/'Tests/ReaperClipboardPathTests.cpp').read_text(encoding='utf-8')
print('TEST169 SWS media source path clipboard checks passed')
