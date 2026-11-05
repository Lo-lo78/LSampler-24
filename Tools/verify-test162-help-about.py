#!/usr/bin/env python3
"""TEST162: guard LSampler-24's localized Help, About and VST3 release metadata."""
from pathlib import Path
from html.parser import HTMLParser
import re

root = Path(__file__).resolve().parents[1]
editor = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
help_html = (root / 'Help/LSampler24Help.html').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
workflow = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
parameters = (root / 'Source/Parameters.h').read_text(encoding='utf-8')

assert 'constexpr auto lsamplerVersion = "0.99.25";' in editor
assert any(f'constexpr auto lsamplerReleaseDate = "{date}";' in editor for date in ('4 November 2026','5 November 2026'))
assert 'project(LSampler24 VERSION 0.99.25 LANGUAGES C CXX)' in cmake
assert 'aboutInfo.setTitle("About LSampler-24")' in editor
assert 'aboutInfo.copyToClipboard();' in editor
assert 'aboutInfo.setReadOnly(true);' in editor
assert 'void LSampler24AudioProcessorEditor::openAbout()' in editor
assert 'void LSampler24AudioProcessorEditor::showHelpLanguageMenu()' in editor
assert 'void LSampler24AudioProcessorEditor::openHelp(' in editor
assert 'LB-303' not in help_html
assert any(f'LSampler-24-Windows-VST3-{version}.zip' in workflow for version in ('TEST166','TEST167','TEST168'))
assert any(f'BUILD_{version}.txt' in workflow for version in ('TEST166','TEST167','TEST168'))
assert 'FILTER_TEST62_REPORT.txt' in workflow, 'Do not change historical TEST62 audio gate'

languages = ('en', 'it', 'es', 'pt', 'fr', 'ru', 'zh', 'ja')
assert help_html.count('<article class="manual"') == len(languages)
assert '<meta charset="utf-8"/>' in help_html
for lang in languages:
    blocks = re.findall(r'<article class="manual" id="'+lang+r'"[^>]*>(.*?)</article>', help_html, re.S)
    assert len(blocks)==1, (lang, len(blocks))
    block = blocks[0]
    assert all(v in block for v in ('0.99.24', '0.99.22', '0.99.21', '0.99.20')), f'current release and historical notes: {lang}'
    assert 'LFO 1 Voice Mode' in block and 'LFO 2 Voice Mode' in block, lang
    assert all(token in block for token in (
        'One Shot Percent', 'Phase', 'Env Rate', 'Square PWM', 'Upper Squash', 'Lower Squash', 'Cross Rate',
        'Mono', 'Poly', 'Alt+H', 'Alt+A', 'Ctrl+A', 'Ctrl+C')), lang
    assert '0.99.25' in re.findall(r'<h1>(.*?)</h1>',block,re.S)[0], lang
    assert re.findall(r'<h2>About</h2>',block) == ['<h2>About</h2>'],lang
    assert ('4 November 2026' in block or '4 novembre 2026' in block or lang not in ('en','it')),lang
    assert re.search(r'<h2>.*?0\.99\.20.*?</h2>',block), f'history missing {lang}'
for i in (1,2):
    for suffix in ('one_shot_percent','phase','env_rate','square_pwm','upper_squash','lower_squash','cross_rate','voice_mode'):
        assert f'"lfo{i}_{suffix}"' in parameters, (i,suffix)

class TagChecker(HTMLParser):
    def error(self, message): raise AssertionError(message)
parser = TagChecker(convert_charrefs=True)
parser.feed(help_html)
parser.close()
print('PASS TEST162: eight localized guides, LFO TEST160/161, About clipboard, version, date, workflow')
