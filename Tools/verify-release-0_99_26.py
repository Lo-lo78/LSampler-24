#!/usr/bin/env python3
"""Release v0.99.26: keep About, CMake, 8 Help translations, README, and artifact aligned."""
from pathlib import Path
import re
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
read=lambda name:(root/name).read_text(encoding='utf-8')
cpp=read('Source/PluginEditor.cpp'); cmake=read('CMakeLists.txt'); help=read('Help/LSampler24Help.html')
workflow=read('.github/workflows/build-windows.yml'); readme=read('README.md'); rel=read('RELEASE_0.99.26.md')
assert 'constexpr auto lsamplerVersion = "0.99.26";' in cpp
assert 'constexpr auto lsamplerReleaseDate = "8 November 2026";' in cpp
assert 'project(LSampler24 VERSION 0.99.26 LANGUAGES C CXX)' in cmake
assert 'New in 0.99.26: Alt+V imports' in cpp
assert 'LSampler-24 v0.99.26' in workflow
assert 'LSampler-24.0.99.26.vst3.zip' in workflow
assert 'BUILD_0.99.26.txt' in workflow
assert 'RELEASE_0.99.26.md' in workflow
assert '8 November 2026' in rel
assert '## Novità principali della 0.99.26' in readme
for lang in ('en','it','es','pt','fr','ru','zh','ja'):
    match=re.search(r'<article class="manual" id="'+lang+'".*?</article>',help,re.S)
    assert match, lang
    block=match.group(0)
    assert '0.99.26' in re.search(r'<h1>.*?</h1>',block,re.S).group(0),lang
    assert 'Alt+V' in block and 'Clear Extracted Audio Cache' in block and '16' in block,lang
    assert 'Version: 0.99.26' in block,lang
    assert 'Release date: 8 November 2026' in block,lang
class Checker(HTMLParser): pass
Checker(convert_charrefs=True).feed(help)
print('PASS: 0.99.26 consistent in About, CMake, 8 guides, README and Actions packaging')
