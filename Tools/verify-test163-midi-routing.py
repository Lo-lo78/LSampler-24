#!/usr/bin/env python3
"""TEST163: static build gate for 24-channel selectors, compatibility and Help."""
from pathlib import Path
import re
from html.parser import HTMLParser

root = Path(__file__).resolve().parent.parent
parameters = (root / 'Source/Parameters.h').read_text(encoding='utf-8')
processor = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
voices = (root / 'Source/SamplerVoice.cpp').read_text(encoding='utf-8')
voice_h = (root / 'Source/SamplerVoice.h').read_text(encoding='utf-8')
macro_h = (root / 'Source/BankMacroParameters.h').read_text(encoding='utf-8')
editor = (root / 'Source/PluginEditor.cpp').read_text(encoding='utf-8')
help_html = (root / 'Help/LSampler24Help.html').read_text(encoding='utf-8')
workflow = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')

# No renumbering: the only new parameter is appended AFTER both TEST161 LFO modes.
enum = parameters.split('enum class P {', 1)[1].split('    count\n', 1)[0]
keys = [line.strip().rstrip(',') for line in enum.splitlines()
        if line.strip() and not line.strip().startswith('//')]
assert len(keys) == 160 and keys[-3:] == ['lfo1_voice_mode', 'lfo2_voice_mode', 'midi_channel']
assert 'parameterCount = 160' in parameters
assert 'inline constexpr std::array<GridEntry, 184> grid' in parameters
assert '{int(P::midi_channel), -1, Action::none, "Slot"}' in parameters
assert '{"midi_channel", "MIDI Channel", "Slot", Kind::enumeration, 0, 0, 16' in parameters
assert '"Omni|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16"' in parameters
assert 'assigned == 0 || assigned == zeroBasedChannel + 1' in parameters

# All legacy TEST160/TEST161 registrations must finish before the 24 new entries.
creation = processor.split('void LSampler24AudioProcessor::createHostParameters()', 1)[1].split('    hostParametersReady=true;', 1)[0]
assert creation.count('for(int i=test160Last;i<int(P::midi_channel);++i)') == 2
assert creation.count('h.values[size_t(P::midi_channel)] = add(') == 1
assert creation.index('h.values[size_t(P::midi_channel)] = add(') > creation.index('hostBankMacros[size_t(i)] = add("bankmacro_"', creation.index('// TEST161:'))
assert 'p == P::midi_channel' in macro_h
assert 'hostBankMacros[size_t(P::midi_channel)]' not in processor

# Performance-path filtering: normal key zones + Slice MIDI Map share a guard.
assert processor.count('acceptsMidiChannel(channel)') >= 2
assert re.search(r'if\(!s\.sampleSet\[0\].*?s\.params\.acceptsMidiChannel\(channel\)',processor,re.S)
assert re.search(r'if\(!slotHasAudio\(s\).*?!p\.acceptsMidiChannel\(channel\)',processor,re.S)
assert 'voicePool.choke(note, channel);' in processor
assert 'void choke(int note, int channel=0);' in voice_h
assert 'v.channel==channel' in voices.split('void GlobalVoicePool::choke(',1)[1].split('double GlobalVoicePool::random',1)[0]
assert 'if (!p.acceptsMidiChannel(ch)) continue;' in voices

# Exposed version and new localized documentation must correspond to the artifact.
assert 'constexpr auto lsamplerVersion = "0.99.25";' in editor
assert any(f'constexpr auto lsamplerReleaseDate = "{date}";' in editor for date in ('4 November 2026','5 November 2026','7 November 2026'))
assert 'project(LSampler24 VERSION 0.99.25' in (root/'CMakeLists.txt').read_text()
assert any(f'LSampler-24-Windows-VST3-{version}.zip' in workflow for version in ('TEST166','TEST167','TEST168','TEST169','TEST170','TEST173','TEST174','TEST175','TEST176','TEST177','TEST178','TEST180','TEST182','TEST183','TEST185'))
assert any(f'BUILD_{version}.txt' in workflow for version in ('TEST166','TEST167','TEST168','TEST169','TEST170','TEST173','TEST174','TEST175','TEST176','TEST177','TEST178','TEST180','TEST182','TEST183','TEST185'))
assert 'FILTER_TEST62_REPORT.txt' in workflow
assert 'python Tools/verify-test163-midi-routing.py' in workflow
for lang in ('en','it','es','pt','fr','ru','zh','ja'):
    matches = re.findall(r'<article class="manual" id="'+lang+r'".*?</article>',help_html,re.S)
    assert len(matches)==1,lang
    text = matches[0]
    assert '0.99.24' in text and 'MIDI Channel' in text and 'Omni' in text,lang
class CheckHTML(HTMLParser):
    pass
parser = CheckHTML()
parser.feed(help_html)
parser.close()
print('PASS TEST163: stable VST3 append, 24 MIDI selectors, Slice/normal routing, channel isolation, eight-language Help and Windows package')
