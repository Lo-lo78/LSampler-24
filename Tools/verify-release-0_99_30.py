#!/usr/bin/env python3
"""Public 0.99.30: check metadata, all 8 accessible guides and Windows packaging."""
from pathlib import Path
import re
from html.parser import HTMLParser
root = Path(__file__).resolve().parents[1]
read = lambda path: (root / path).read_text(encoding='utf-8')
cpp = read('Source/PluginEditor.cpp')
cmake = read('CMakeLists.txt')
help_page = read('Help/LSampler24Help.html')
workflow = read('.github/workflows/build-windows.yml')
readme = read('README.md')
rel = read('RELEASE_0.99.30.md')
assert 'constexpr auto lsamplerVersion = "0.99.30";' in cpp
assert 'constexpr auto lsamplerReleaseDate = "24 November 2026";' in cpp
assert 'New in 0.99.30: HP Key Follow' in cpp
assert 'project(LSampler24 VERSION 0.99.30 LANGUAGES C CXX)' in cmake
assert 'LSampler-24 v0.99.30' in workflow
assert 'LSampler-24.0.99.30.vst3.zip' in workflow
assert 'BUILD_0.99.30.txt' in workflow
assert 'RELEASE_0.99.30.md' in workflow and 'RELEASE_TEST189.md' in workflow
assert '24 November 2026' in rel and 'HP Key Follow' in rel
assert readme.startswith('# LSampler-24 — Versione 0.99.30')
assert 'BP Cutoff' in readme and 'Ctrl+Shift+A' in readme
for lang in ('en', 'it', 'es', 'pt', 'fr', 'ru', 'zh', 'ja'):
    match = re.search(r'<article class="manual" id="' + lang + r'".*?</article>', help_page, re.S)
    assert match, lang
    block = match.group(0)
    assert '0.99.30' in re.search(r'<h1>.*?</h1>', block, re.S).group(0), lang
    assert '0.99.30' in block.split('</h2>', 1)[0], lang
    assert 'Version: 0.99.30' in block, lang
    assert 'Release date: 24 November 2026' in block, lang
    assert 'Phase' in block and 'Pan' in block and 'Ctrl+Shift+A' in block, lang
    assert 'Env Hold' in block and 'Self-Lace' in block, lang
    assert all(x in block for x in ('HP Key Follow', 'BP Cutoff', 'LFO 1 BP Depth', 'LFO 2 BP Depth', '0.99.29')), lang
assert help_page.count('<article class="manual"') == 8
HTMLParser().feed(help_page)
params = read('Source/Parameters.h')
assert '"phase", "Phase", "Slot"' in params
assert 'parameterCount = 178' in params
assert 'python Tools/verify-test189-phase.py' in workflow
assert 'python Tools/verify-test190-filter-macro.py' in workflow
assert 'python Tools/verify-test191-bp-lfo.py' in workflow
assert 'python Tools/verify-test188-ahdsr-hold.py' in workflow
assert 'python Tools/verify-test187-self-lace-selection.py' in workflow
print('PASS 0.99.30: About, CMake, 8 Help languages, linked filters, LFO depths, Phase, Hold, Self-Lace, VST3 packaging')
