#!/usr/bin/env python3
"""TEST164: verify integration, keyboard digit normalization and release; no JUCE needed."""
from pathlib import Path
import re
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
key=(root/'Source/ParameterSearchKey.h').read_text(encoding='utf-8')
core=(root/'Source/IncrementalParameterSearch.h').read_text(encoding='utf-8')
editor=(root/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
header=(root/'Source/PluginEditor.h').read_text(encoding='utf-8')
slice_cpp=(root/'Source/SliceEditor.cpp').read_text(encoding='utf-8')
fx=(root/'Source/FXPanel.h').read_text(encoding='utf-8')
help_html=(root/'Help/LSampler24Help.html').read_text(encoding='utf-8')
workflow=(root/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
assert core.count('class IncrementalParameterSearch')==1
assert '1000' in core and 'lastTime_' in core
assert 'sameLetterCycle' in core
assert 'buffer_.assign(1, c)' in core and 'candidate.size() == 1' in core
assert 'const bool backwards = direction < 0;' in core
# Most important regression: unshifted physical digit beats shifted glyph.
assert key.index("if (code >= '0' && code <= '9')") < key.index('const auto typed = key.getTextCharacter();')
assert key.index('F1Key') < key.index("if (code >= '0' && code <= '9')")
assert 'mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()' in key
assert 'parameterSearchCharacter(key) != 0' in header, 'Native ComboBox must forward typing'
assert 'mainParameterSearch.next(' in editor
assert 'bankMacroParameterSearch.next(' in editor
assert 'sampleSetParameterSearch.next(' in editor
assert 'parameterSearch.next(' in slice_cpp and 'parameterSearch.next(' in fx
assert 'name.equalsIgnoreCase("Zero Crossing") ? std::wstring(L"0 Zero Crossing")' in editor
assert 'displayedGridParameterName(lsampler::grid[size_t(next)])' in editor
assert 'std::wstring(bankEntryName(bankMacroEntries[size_t(cursor)]).toWideCharPointer())' in editor
assert 'parameterSearch.reset();' in slice_cpp and 'parameterSearch.reset();' in fx
assert 'mainParameterSearch.reset();' in editor and 'bankMacroParameterSearch.reset();' in editor
assert 'constexpr auto lsamplerVersion = "0.99.25";' in editor
assert 'LReaParams-style incremental search:' in editor
assert 'project(LSampler24 VERSION 0.99.25 LANGUAGES C CXX)' in (root/'CMakeLists.txt').read_text()
assert any(f'LSampler-24-Windows-VST3-{version}.zip' in workflow for version in ('TEST166','TEST167','TEST168','TEST169'))
assert any(f'name: LSampler-24-Windows-VST3-{version}' in workflow for version in ('TEST166','TEST167','TEST168','TEST169'))
assert any(f'BUILD_{version}.txt' in workflow for version in ('TEST166','TEST167','TEST168','TEST169'))
assert 'python Tools/verify-test164-incremental-search.py' in workflow
assert 'python Tools/verify-test163-midi-routing.py' in workflow
assert 'FILTER_TEST62_REPORT.txt' in workflow
assert 'TEST164_INCREMENTAL_PARAMETER_SEARCH_NOTE_IT.txt' in {p.name for p in root.iterdir()}
langs=('en','it','es','pt','fr','ru','zh','ja')
for lang in langs:
    block=re.search(r'<article class="manual" id="'+lang+r'".*?</article>',help_html,re.S)
    assert block is not None,lang
    content=block.group(0)
    assert '0.99.24' in content and '0.99.22' in content,lang
    for phrase in ('MIDI Channel','Zero Crossing','Sample Set F2'):
        assert phrase in content,(lang,phrase)
    assert any(t in content for t in ('Shift+1','Maj+1','Mayús+1')),(lang,'reverse digit shortcut')
assert help_html.count('<article class="manual"')==8
parser=HTMLParser();parser.feed(help_html);parser.close()
print('PASS TEST164: shared incremental search, reverse digits, no host ID changes, 8 Help languages, About and Windows workflow')
