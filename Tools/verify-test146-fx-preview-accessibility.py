#!/usr/bin/env python3
"""TEST146 static-only regression check. No compilation or audio execution."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
panel = (root/'Source/FXPanel.h').read_text(encoding='utf-8')
editor = (root/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
header = (root/'Source/PluginEditor.h').read_text(encoding='utf-8')
assert 'fxButton.setDescription("Alt+F");' in editor
assert 'Master effects Grid' not in editor
# Cue shown when entering Value, cleared afterward like the main grid.
entry = panel.split('void setEntryAccessibility()',1)[1].split('void setParameterName',1)[0]
line = panel.split('void setLineReadingMode()',1)[1].split('void setEntryAccessibility',1)[0]
assert 'setTitle("Value. " + parameterName);' in entry
assert 'setDescription("Alt+V");' in entry
assert 'setDescription({});' in line
assert 'setLineReadingMode();' in panel
# The FX panel owns key events and forwards preview mode to shared slots code.
assert "if(alt && shortcut=='p')" in panel
assert 'onToggleSlotPreview' in panel and 'onPreviewSpace' in panel
assert 'fxPanel.onToggleSlotPreview=' in editor and 'fxPanel.onPreviewSpace=' in editor
assert 'slotPreviewMode = !slotPreviewMode;' in editor
assert 'void LSampler24AudioProcessorEditor::toggleSlotPreview' in editor
assert 'bool LSampler24AudioProcessorEditor::toggleSlotPreviewIfEnabled' in editor
assert 'toggleSlotPreview(*announceSource);' in editor
assert 'return toggleSlotPreviewIfEnabled();' in editor
assert 'processor.requestPreviewStart();' in editor
assert 'processor.requestPreviewStop();' in editor
assert 'processor.requestPreviewToggle();' in editor
assert 'void toggleSlotPreview(juce::Component& announcementTarget);' in header
# If preview is off, native controls must not eat Space (return to DAW).
assert panel.count('return false; // DAW Space when Alt+P slot audition is off') == 2
assert 'return onPreviewSpace ? onPreviewSpace() : false;' in panel
# Announcements must not be retained in the active focus control.
assert 'if (valueFocused) value.setLineReadingMode();' in panel
assert 'else grid.setDescription({});' in panel
print('PASS TEST146: FX accessible button, entry Alt+V, shared Slots Alt+P preview, Space routing and clean NVDA descriptions.')
