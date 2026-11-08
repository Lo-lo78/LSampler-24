#!/usr/bin/env python3
"""Structural guard: TEST168 reads REAPER's native Windows clipboard directly.
Run on GitHub Actions before VST3 build. Manual REAPER behavior is separately verified.
"""
from pathlib import Path
root = Path(__file__).resolve().parent.parent
p = (root/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
e = (root/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
h = (root/'Source/ReaperNativeClipboard.h').read_text(encoding='utf-8')
w = (root/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
for term in ['getReaperCopiedItems', 'readNativeReaperAudioItems', 'markSlotCopy',
             'getReaperApi', 'GetClipboardSequenceNumber']:
    assert term in p, term
for term in ['OpenClipboard', 'CloseClipboard', 'EnumClipboardFormats',
             'GetClipboardData', 'CF_UNICODETEXT', 'CF_TEXT', '<ITEM',
             'parseReaperItemText', 'JUCE_WINDOWS']:
    assert term in h, term
for term in ['pasteCopiedReaperItems', 'processor.getReaperCopiedItems',
             'processor.markReaperClipboardAsSlot', 'confirmChromaticPasteOverwrite',
             'p.importSampleToSlot(item.file, entry.slot,']:
    assert term in e, term
assert 'getApi<const char* (*)(const char*, const char*)>("GetExtState")' not in p
assert 'Tools/LSampler-24 - Copy REAPER items to slots.lua' not in w
assert ('LSampler-24-Windows-VST3-TEST168.zip' in w
        or 'LSampler-24-Windows-VST3-TEST169.zip' in w
        or 'LSampler-24-Windows-VST3-TEST170.zip' in w or 'LSampler-24-Windows-VST3-TEST173.zip' in w or 'LSampler-24-Windows-VST3-TEST174.zip' in w or 'LSampler-24-Windows-VST3-TEST175.zip' in w or 'LSampler-24-Windows-VST3-TEST176.zip' in w or 'LSampler-24-Windows-VST3-TEST177.zip' in w or 'LSampler-24-Windows-VST3-TEST178.zip' in w or 'LSampler-24-Windows-VST3-TEST183.zip' in w)
assert not (root/'Tools/LSampler-24 - Copy REAPER items to slots.lua').exists()
print('TEST168 native REAPER clipboard structural check passed')
