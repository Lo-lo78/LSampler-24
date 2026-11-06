#!/usr/bin/env python3
"""TEST166 source-contract check; native waveform assertions run by FilterTests."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
voice=(r/'Source/SamplerVoice.cpp').read_text(encoding='utf-8')
header=(r/'Source/SamplerVoice.h').read_text(encoding='utf-8')
processor=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
editor=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
tests=(r/'Tests/FilterParityTests.cpp').read_text(encoding='utf-8')
ui_tests=(r/'Tests/SamplerTests.cpp').read_text(encoding='utf-8')
workflow=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')

assert 'std::array<Voice,voiceCount> retiringVoices {}' in header
assert 'bool GlobalVoicePool::retireVoice(const Voice& previous) noexcept' in voice
assert 'sample->voiceReferences.fetch_add(1' in voice
assert 'if(v.retiring) --activeRetiringVoices' in voice
assert 'retireVoice(existing)' in voice and 'retireVoice(v)' in voice
assert 'if(v.retiring && v.fadeOutLeft<=0)stop(v)' in voice
assert 'if(v.fadeInLeft>0)' in voice and 'if(v.retiring)' in voice
assert 'if(!retrigger && note!=v.note && s.params[P::portamento]==0)' in voice
assert 'legatoPitchSlewLeft' in header and 'legatoFromIncrement' in header
assert 'transitionSamples=std::max(16,int(std::round(hostSampleRate*.003)))' in voice
assert 'for(auto& v:retiringVoices)stop(v)' in voice
assert 'if(number==120)for(auto& v:retiringVoices)' in voice
# No heap operations in the dedicated MIDI transition body.
start=voice.index('bool GlobalVoicePool::retireVoice(')
end=voice.index('void GlobalVoicePool::release(',start)
assert not re.search(r'\b(new|malloc|realloc|make_unique|make_shared)\b',voice[start:end])

assert 'p[P::sample_start]=std::min(p[P::sample_start],p[P::sample_end])' in processor
assert 'p[P::sample_end]=std::max(p[P::sample_start],p[P::sample_end])' in processor
assert 'maximum=std::min(maximum,processor.getSampleWindowEnd())' in editor
assert 'minimum=std::max(minimum,processor.getSampleWindowStart())' in editor
assert 'parameterValue.setValue(getSelectedParameterValue(),juce::dontSendNotification)' in editor

assert 'void test166DeclickTransitions()' in tests
assert 'test166DeclickTransitions();' in tests
assert all(x in tests for x in ('mono retrigger', 'mono genuine legato', 'poly same-note replace', 'poly cap steal'))
assert all(x in ui_tests for x in ('TEST166 End cannot cross Start','TEST166 Start cannot cross End',
                                   'TEST166 Start slider maximum','TEST166 End slider minimum'))
assert 'project(LSampler24 VERSION 0.99.25' in (r/'CMakeLists.txt').read_text()
assert 'constexpr auto lsamplerVersion = "0.99.25"' in editor
assert any('LSampler-24-Windows-VST3-' + v in workflow for v in ('TEST166', 'TEST167', 'TEST168', 'TEST169', 'TEST170','TEST173','TEST174'))
assert 'Tools/verify-test166-declick-bounds.py' in workflow
# Frozen reference must never be touched by bugfix tests.
assert (r/'Tests/ReferenceTEST59/SamplerVoice.cpp').is_file()
print('PASS TEST166: tail lifetime, no allocations, legato slew, editor/processor boundaries, native test registration, metadata')
