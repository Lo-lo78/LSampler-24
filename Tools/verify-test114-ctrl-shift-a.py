#!/usr/bin/env python3
"""Source-only keyguard regression check; does not build VST3."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/'Source/PluginEditor.cpp').read_text()
slice=(root/'Source/SliceEditor.cpp').read_text()
assert 'if (sampleSetActive && mods.isCtrlDown() && mods.isShiftDown()\n        && !mods.isAltDown()\n' in main
assert '&& parameterPage && !sourceIsValueEditor)' in main
assert 'if (mods.isCtrlDown() && mods.isShiftDown() && !mods.isAltDown()\n        && juce::CharacterFunctions::toLowerCase(juce::juce_wchar(code)) == \'a\')' in slice
for src in (main,slice):
    for match in __import__('re').finditer(r'if \(mods\.isCtrlDown\(\) && mods\.isShiftDown\(\)', src):
        guard=src[match.start():src.find(')',src.find('== \'a\'',match.start()))+1] if '== \'a\'' in src[match.start():match.start()+350] else src[match.start():match.start()+190]
        assert '!mods.isCommandDown()' not in guard, guard
print('PASS: Ctrl+Shift+A keyboard guards do not reject Ctrl on Windows')
