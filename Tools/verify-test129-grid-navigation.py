#!/usr/bin/env python3
"""TEST129 static navigation regression checks; requires no JUCE build."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
cpp=(root/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
hdr=(root/'Source/PluginEditor.h').read_text(encoding='utf-8')
help_text=(root/'Help/LSampler24Help.html').read_text(encoding='utf-8')

# Both screen-reader naming paths and both initial searches consult precisely
# the same helper.  No underlying host descriptor name leaks into type-ahead.
assert 'juce::String displayedGridParameterName(const GridEntry& entry)' in cpp
assert 'entry.parameter == int(P::global_one_shot)' in cpp
assert 'juce::String("Main Playback Mode")' in cpp
assert 'const juce::String name = displayedGridParameterName(e);' in cpp
assert cpp.count('displayedGridParameterName(') == 5  # declaration, two labels, two searches
assert 'const auto name=isSlotBankSend(next) ? slotBankSendName(next)' in cpp
assert ': displayedGridParameterName(lsampler::grid[size_t(next)]);' in cpp
# TEST148: Alt+K also contains 3 bank-only FX sends, resolved by bankEntryName.
assert 'const auto name = bankEntryName(bankMacroEntries[size_t(cursor)]);' in cpp
assert 'return displayedGridParameterName(lsampler::grid[size_t(index)]);' in cpp
assert 'descriptor(lsampler::grid[size_t(next)]).name' not in cpp

# Main Grid: plain Left/Right retains row; Alt+Shift always starts row zero.
assert 'const auto moveParameterPage = [this](int direction, bool firstParameter)' in cpp
assert 'const int row = firstParameter ? 0 : selectedParameter - begin;' in cpp
assert 'moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1, true);' in cpp
assert 'moveParameterPage(code == juce::KeyPress::leftKey ? -1 : 1, false);' in cpp

# Bank Macro: same semantics, bounded to the existing page and its neighbours.
assert 'void moveBankMacroPage(int direction, bool firstParameter);' in hdr
assert 'const int row = firstParameter ? 0 : bankMacroCursor - begin;' in cpp
assert 'moveBankMacroPage(code == juce::KeyPress::rightKey ? 1 : -1, true);' in cpp
assert 'moveBankMacroPage(code == juce::KeyPress::rightKey ? 1 : -1, false);' in cpp

# Verify page-column preservation rules at extreme and differing page lengths.
pages=[['Level','Pan','Pitch'],['Attack','Decay'],['Main Playback Mode','Sample End','Start','Fine']]
start=[0]
for page in pages: start.append(start[-1]+len(page))
def move(current, step, first):
    page=next(i for i in range(len(pages)) if start[i] <= current < start[i+1])
    target=page+step
    if not 0 <= target < len(pages): return current
    row=0 if first else current-start[page]
    return start[target]+min(row,len(pages[target])-1)
assert move(2, 1, False)==4  # Last of page 1 to last of page 2.
assert move(2, 1, True)==3   # Alt+Shift jumps to first of page 2.
assert move(4, 1, False)==6  # Keep second row.
assert move(4, 1, True)==5   # First of page 3.
assert move(3,-1,False)==0
assert move(3,-1,True)==0
assert move(0,-1,True)==0 and move(8,1,True)==8

# Human-facing documentation must describe the difference.
assert 'Alt+Shift+Left/Right changes category/page and selects its first parameter' in help_text
assert 'Alt+Shift+Freccia Sinistra/Destra cambia pagina andando sempre alla prima voce' in help_text
print('PASS TEST129: accessible initial search and both page navigation modes (Grid + Bank Macro).')
