#!/usr/bin/env python3
"""TEST159: static-only checks for About clipboard and multilingual help."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
source = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
help_page = (root / 'Help/LSampler24Help.html').read_text(encoding='utf-8')
assert 'constexpr auto lsamplerVersion = "0.99.20";' in source
assert 'constexpr auto lsamplerReleaseDate = "14 October 2026";' in source
assert 'aboutInfo.setTitle("About LSampler-24")' in source
assert 'aboutInfo.copyToClipboard();' in source
about_nav = source.split('bool LSampler24AudioProcessorEditor::navigateAboutText',1)[1].split('bool LSampler24AudioProcessorEditor::isActionButton',1)[0]
assert "lowerKeyCode == 'c'" in about_nav and "lowerKeyCode == 'a'" in about_nav
assert about_nav.index("lowerKeyCode == 'a'") < about_nav.index("lowerKeyCode == 'c'")
for text in ['"No active automations"', '"Disabled 1 automation"', '"Disabled " + juce::String(disabled) + " automations"', '"No automations disabled"', '"Unable to check automation']:
    assert text in source, text
assert '0.99.11' not in help_page
for lang in ['en','it','es','pt','fr','ru','zh','ja']:
    chapter = re.search(r'<article class="manual" id="' + lang + r'".*?</article>', help_page, re.S)
    assert chapter,lang
    block = chapter.group(0)
    assert '0.99.20' in block
    assert 'Alt+Shift+A' in block and 'Ctrl+Shift+A' in block, lang
    assert 'Alt+Shift+' in block and 'Alt+F' in block, lang
    assert 'Ctrl+C' in block and 'Ctrl+A' in block, lang
    assert len(re.findall('<h2>About</h2>',block)) == 1,lang
assert help_page.count('<article class="manual"') == 8
assert 'Alt+Shift+Su/Giù seleziona il sottoloop precedente/successivo' in help_page
print('PASS TEST159: version, release date, About clipboard, English NVDA messages, all eight help languages')
