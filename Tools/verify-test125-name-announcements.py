#!/usr/bin/env python3
"""Static guard for TEST125 concise JUCE modal names; does not build the VST3."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
checks = [
    'for (auto* child : dialog.getChildren())',
    'if (auto* label = dynamic_cast<juce::Label*>(child))',
    'label->setAccessible(false);',
    'dialog.setDescription(juce::String());',
    'ok->setTitle("OK");',
    'ok->setDescription(juce::String());',
    'cancel->setTitle("Cancel");',
    'cancel->setDescription(juce::String());',
    'forBank ? "Save Bank" : "Save Slot"',
    'directory ? "Rename folder" : "Rename file"',
    'new juce::AlertWindow("Rename Slot", {},',
    'new juce::AlertWindow("New folder", {},',
    '"Rename folder (F2)" : "Rename file (F2)"',
    'editor->setExplicitFocusOrder(1);',
    'editor->onEscapeKey = [safeDialog]',
    'editor->onReturnKey = [safeDialog]',
    'editor->setSelectAllWhenFocused(true);',
]
for marker in checks:
    assert marker in s, marker
assert s.count('prepareNameEntryDialog(*') == 4
assert 'new juce::AlertWindow("Rename Element"' not in s
assert 'new juce::AlertWindow(forBank ? "Save Bank Name" : "Save Slot Name"' not in s
print('PASS: concise names and single accessible prompt; Save, Rename, New Folder, Enter/Esc focus retained.')
