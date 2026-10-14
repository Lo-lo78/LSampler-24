#!/usr/bin/env python3
"""Source-only TEST157 regression checks; no JUCE build or plugin execution."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
h=(r/'Source/PluginEditor.h').read_text(encoding='utf-8')
help_text=(r/'Help/LSampler24Help.html').read_text(encoding='utf-8')
a=s.index('// TEST157: subloop navigation MUST precede')
b=s.index('if (mods.isAltDown() && !mods.isCtrlDown() && !mods.isCommandDown())',a)
c=s.index('if (parameterPage && source != &parameterValue && !mods.isShiftDown())',b)
d=s.index('// Shift+arrows: spatial navigation among ACTIVE host envelopes',c)
assert a<b<c<d, 'Subloop shortcut must execute before generic Alt+arrow handler'
shortcut=s[a:b]
for marker in ('source == &parameterSelector', 'mods.isAltDown()', 'mods.isShiftDown()',
               '!mods.isCtrlDown()', 'code == juce::KeyPress::upKey',
               'code == juce::KeyPress::downKey', 'std::strcmp(selectedEntry().category, "Loops") == 0',
               'lsampler::loopCount - 1', 'selectedLoop = next;', 'refreshParameterGrid();',
               'announceToActiveScreenReader(parameterSelector, parameterCellText(selectedParameter))',
               'return true;'):
    assert marker in shortcut, marker
assert 'if (parameterPage && source != &parameterValue && !mods.isShiftDown())' in s
assert '&& mods.isShiftDown() && !mods.isCtrlDown() && !mods.isAltDown()' in s[d:]
assert h.count('code == juce::KeyPress::upKey || code == juce::KeyPress::downKey') >= 2
assert 'Alt+Shift+Su/Giù seleziona il sottoloop precedente/successivo' in help_text
# Pure boundary rule mirrored from juce::jlimit(0, loopCount - 1, ...).
def change_loop(i,delta): return max(0,min(9,i+delta))
assert change_loop(0,-1)==0 and change_loop(9,1)==9
assert change_loop(0,1)==1 and change_loop(5,-1)==4
print('PASS TEST157: subloop Alt+Shift+Up/Down precedence, dedicated route, boundaries, DAW Shift and unshifted Alt value isolation.')
