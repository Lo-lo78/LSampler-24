#!/usr/bin/env python3
"""Static regression check for TEST124 name dialog focus and cancellation; does not compile."""
from pathlib import Path
source = (Path(__file__).resolve().parents[1] / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
assert 'void prepareNameEntryDialog(' in source
for editor, label in [
    ('slotName', 'Slot name'),
    ('presetName', 'File name'),
    ('folderName', 'Folder name'),
    ('newName', 'New name'),
]:
    assert f'prepareNameEntryDialog(*' in source
    assert f', "{editor}", "{label}");' in source, editor
for required in [
    'editor->setExplicitFocusOrder(1);',
    'ok->setExplicitFocusOrder(2)',
    'cancel->setExplicitFocusOrder(3)',
    'editor->setEscapeAndReturnKeysConsumed(true);',
    'editor->onEscapeKey = [safeDialog]',
    'safeDialog->exitModalState(0);',
    'editor->onReturnKey = [safeDialog]',
    'safeDialog->exitModalState(1);',
    'dialog.setDescription(juce::String());',
]:
    assert required in source, required
for bad in [
    'dialog->getTextEditor("presetName"))\n    {\n        field->selectAll();\n        field->grabKeyboardFocus();',
    'if (auto* e = dialog->getTextEditor("newName")) { e->selectAll(); e->grabKeyboardFocus(); }',
]:
    assert bad not in source
assert source.count('prepareNameEntryDialog(*') == 4
print('PASS: All 4 name dialogs use one initial editor focus, Esc/Enter and concise accessible title.')
