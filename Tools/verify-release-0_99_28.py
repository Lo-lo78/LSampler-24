#!/usr/bin/env python3
"""Public 0.99.28: check metadata, all 8 accessible guides and Windows packaging."""
from pathlib import Path
import re
from html.parser import HTMLParser
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding='utf-8')
cpp=read('Source/PluginEditor.cpp')
cmake=read('CMakeLists.txt')
help=read('Help/LSampler24Help.html')
workflow=read('.github/workflows/build-windows.yml')
readme=read('README.md')
rel=read('RELEASE_0.99.28.md')
assert 'constexpr auto lsamplerVersion = "0.99.28";' in cpp
assert 'constexpr auto lsamplerReleaseDate = "16 November 2026";' in cpp
assert 'New in 0.99.28: AHDSR Hold' in cpp
assert 'project(LSampler24 VERSION 0.99.28 LANGUAGES C CXX)' in cmake
assert 'LSampler-24 v0.99.28' in workflow
assert 'LSampler-24.0.99.28.vst3.zip' in workflow
assert 'BUILD_0.99.28.txt' in workflow
assert 'RELEASE_0.99.28.md' in workflow and 'RELEASE_TEST188.md' in workflow
assert '16 November 2026' in rel
assert readme.startswith('# LSampler-24 — Versione 0.99.28')
assert 'Ctrl+Shift+A' in readme
for lang in ('en','it','es','pt','fr','ru','zh','ja'):
    match=re.search(r'<article class="manual" id="'+lang+r'".*?</article>',help,re.S)
    assert match,lang
    b=match.group(0)
    assert '0.99.28' in re.search(r'<h1>.*?</h1>',b,re.S).group(0),lang
    assert '0.99.28' in b.split('</h2>',1)[0],lang
    assert 'Version: 0.99.28' in b,lang
    assert 'Release date: 16 November 2026' in b,lang
    assert 'Env Hold' in b and 'LP Env Hold' in b and 'HP Env Hold' in b,lang
    assert 'Ctrl+Shift+A' in b and 'Self-Lace' in b,lang
    assert 'Lace Source' in b and 'Drift Cycle' in b and 'Alt+V' in b,lang
assert help.count('<article class="manual"')==8
HTMLParser().feed(help)
params=read('Source/Parameters.h')
assert 'env_hold' in params and 'lp_env_hold' in params and 'hp_env_hold' in params
assert 'python Tools/verify-test188-ahdsr-hold.py' in workflow
assert 'python Tools/verify-test187-self-lace-selection.py' in workflow
print('PASS 0.99.28: About, CMake, 8 Help languages, Hold, Self-Lace, VST3 packaging')
