#!/usr/bin/env python3
"""TEST158 source-only regression checks; do not build the VST3."""
from pathlib import Path
r = Path(__file__).resolve().parent.parent
proc = (r / 'Source/PluginProcessor.cpp').read_text()
head = (r / 'Source/PluginProcessor.h').read_text()
editor = (r / 'Source/PluginEditor.cpp').read_text()
ui = (r / 'Source/PluginEditor.h').read_text()
fx = (r / 'Source/FXPanel.h').read_text()
slice_cpp = (r / 'Source/SliceEditor.cpp').read_text()
slice_h = (r / 'Source/SliceEditor.h').read_text()
help_page = (r / 'Help/LSampler24Help.html').read_text()
# One instance only: host parent + currently focused FX validated with an
# existing parameter's name, never blindly use last-touched other plugins.
batch = proc[proc.index('    bool deactivateAll(LSampler24AudioProcessor&'):proc.index('\nprivate:\n    template <class T> T getApi', proc.index('    bool deactivateAll(LSampler24AudioProcessor&'))]
assert 'findFocusedFx(processor, parameters[0])' in batch
assert 'getNumParams(fx.track, fx.index)' in batch
assert 'if (hostCount < parameters.size()) return false;' in batch
assert 'getEnvelope(fx.track, fx.index, i, false)' in batch
assert 'getEnvelope(fx.track, fx.index, i, true)' not in batch
assert '"ACTIVE", active, false' in batch
assert '"ACTIVE", zero, true' in batch
assert '"ACTIVE", after, false' in batch
assert '"ARM", zero, true' in batch and '"VISIBLE", zero, true' in batch
assert '++disabled;' in batch and '++failed;' in batch
assert 'TrackList_AdjustWindows' in batch and 'UpdateArrange' in batch
assert 'deactivateAllReaperEnvelopes(int& disabled, int& failed);' in head
# Main handler is BEFORE FX/Modal (including Sample Set), unlike the previous
# routing bug with Alt+Shift+arrows.
keyboard = editor[editor.index('bool LSampler24AudioProcessorEditor::handleKeyPress'):]
assert keyboard.index('deactivateAllHostAutomations(source);') < keyboard.index('if (fxOpen) return fxPanel.keyPressed(key);')
assert '"No active automations"' in editor
assert '"Disabled 1 automation"' in editor
assert '"Disabled " + juce::String(disabled) + " automations"' in editor
assert ui.count('|| (mods.isAltDown() && mods.isShiftDown() && !mods.isCtrlDown()') >= 1
assert ui.count("juce::juce_wchar(code)) == 'a'") >= 4  # Ctrl+Shift+A and Alt+Shift+A, Grid and Value
# Children handle keys themselves; they must invoke the parent callback.
assert 'fxPanel.onDisableAllAutomations=' in editor
assert 'sliceEditor->onDisableAllAutomations =' in editor
assert 'onDisableAllAutomations(valueFocused ?' in fx
assert 'onDisableAllAutomations(this)' in slice_cpp
assert 'number.disableAllAutomations = [this]' in slice_cpp
assert 'if (disableAllAutomations) disableAllAutomations();' in slice_h
# Old single-envelope controls must be preserved.
assert 'processor.toggleReaperEnvelopeActive(param)' in editor
assert 'Ctrl+Shift+A' in help_page and 'Alt+Shift+A' in help_page
print('PASS TEST158: scoped REAPER ACTIVE disable; safe count; Grid, Value, FX, Slice, numeric and NVDA wiring')
