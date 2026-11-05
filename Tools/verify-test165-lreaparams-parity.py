#!/usr/bin/env python3
from pathlib import Path
from html.parser import HTMLParser
import re
r=Path(__file__).resolve().parents[1]
core=(r/'Source/IncrementalParameterSearch.h').read_text(encoding='utf8')
key=(r/'Source/ParameterSearchKey.h').read_text(encoding='utf8')
editor=(r/'Source/PluginEditor.cpp').read_text(encoding='utf8')
fx=(r/'Source/FXPanel.h').read_text(encoding='utf8')
slice=(r/'Source/SliceEditor.cpp').read_text(encoding='utf8')
workflow=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf8')
help=(r/'Help/LSampler24Help.html').read_text(encoding='utf8')
assert 'intervalMs = 1000' in core
assert 'const bool atWord = i == 0 || !std::iswalnum' in core
assert 'if (quality == 0) return index;' in core and 'quality == 1' in core
assert 'sameLetterCycle' in core and 'if (repeated ||' in core
assert 'eventTime - lastTime_' in core
assert 'buffer_.assign(1, c);' in core
assert 'std::wstring' in core and 'CharLowerBuffW(&c, 1)' in core
# A missing NOMINMAX in the VST3 target broke std::min/max and yielded MSVC C2589.
assert '#ifndef NOMINMAX' in core and '#define NOMINMAX' in core
assert '#ifdef min\n#undef min' in core and '#ifdef max\n#undef max' in core
assert 'target_compile_definitions(LSampler24SearchTests PRIVATE NOMINMAX' not in (r/'CMakeLists.txt').read_text()
assert 'must not leak Windows min/max macros' in (r/'Tests/IncrementalParameterSearchTests.cpp').read_text()
assert "if (code >= '0' && code <= '9')" in key
assert 'numberPad0' in key
assert 'std::wstring(name.toWideCharPointer())' in editor
assert 'L"0 Zero Crossing"' in editor
assert 'std::wstring(bankEntryName(' in editor and 'std::wstring(L"Velocity Low")' in editor
assert 'std::wstring(juce::String(' in fx
assert 'parameterSearch.next(' in slice
assert 'constexpr auto lsamplerVersion = "0.99.25"' in editor
assert any(f'LSampler-24-Windows-VST3-{version}' in workflow for version in ('TEST166','TEST167','TEST168'))
assert 'verify-test165-lreaparams-parity.py' in workflow
assert 'project(LSampler24 VERSION 0.99.25 ' in (r/'CMakeLists.txt').read_text()
assert 'add_test(NAME incremental-search COMMAND LSampler24SearchTests)' in (r/'CMakeLists.txt').read_text()
assert 'LSampler24SearchTests' in (r/'Tools/build-windows.ps1').read_text()
for lang in ('en','it','es','pt','fr','ru','zh','ja'):
 block=re.search(r'<article class="manual" id="'+lang+r'".*?</article>',help,re.S)
 assert block,lang
 assert '0.99.24' in block.group() ,lang
 assert 'Zero Crossing' in block.group(),lang
HTMLParser().feed(help)
print('PASS TEST165: LReaParams parity of prefix, word boundaries, shift digits, Unicode, 1s, workflow, guide')
