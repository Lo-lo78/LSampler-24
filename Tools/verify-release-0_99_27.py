#!/usr/bin/env python3
"""Release v0.99.27: keep About, CMake, 8 Help translations, README, and artifact aligned."""
from pathlib import Path
import re
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
read=lambda name:(root/name).read_text(encoding='utf-8')
cpp=read('Source/PluginEditor.cpp'); cmake=read('CMakeLists.txt'); help=read('Help/LSampler24Help.html')
workflow=read('.github/workflows/build-windows.yml'); readme=read('README.md'); rel=read('RELEASE_0.99.27.md')
assert 'constexpr auto lsamplerVersion = "0.99.27";' in cpp
assert 'constexpr auto lsamplerReleaseDate = "14 November 2026";' in cpp
assert 'project(LSampler24 VERSION 0.99.27 LANGUAGES C CXX)' in cmake
assert 'New in 0.99.27 TEST186: LFO1/2 Phase Drift and Lace' in cpp
assert 'LSampler-24 v0.99.27' in workflow
assert 'LSampler-24.0.99.27.vst3.zip' in workflow
assert 'BUILD_0.99.27.txt' in workflow
assert 'RELEASE_0.99.27.md' in workflow
assert '14 November 2026' in rel
assert '## Nuovo in 0.99.27 TEST186' in readme
for lang in ('en','it','es','pt','fr','ru','zh','ja'):
    match=re.search(r'<article class="manual" id="'+lang+'".*?</article>',help,re.S)
    assert match, lang
    block=match.group(0)
    assert '0.99.27' in re.search(r'<h1>.*?</h1>',block,re.S).group(0),lang
    assert 'Lace Source' in block and 'Drift Cycle' in block and 'Alt+V' in block,lang
    assert 'Version: 0.99.27' in block,lang
    assert 'Release date: 14 November 2026' in block,lang
class Checker(HTMLParser): pass
Checker(convert_charrefs=True).feed(help)
print('PASS: 0.99.27 consistent in About, CMake, 8 guides, README and Actions packaging')
