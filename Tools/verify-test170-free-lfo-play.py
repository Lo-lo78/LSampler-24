#!/usr/bin/env python3
"""TEST170: source-contract guard; executable Free LFO transport test also runs in CI."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
processor=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
header=(r/'Source/PluginProcessor.h').read_text(encoding='utf-8')
voice=(r/'Source/SamplerVoice.cpp').read_text(encoding='utf-8')
voice_header=(r/'Source/SamplerVoice.h').read_text(encoding='utf-8')
workflow=(r/'.github/workflows/build-windows.yml').read_text(encoding='utf-8')
cmake=(r/'CMakeLists.txt').read_text(encoding='utf-8')
assert 'hostPlayDetector.reset();' in processor
# Regression for MSVC C2664: JUCE Optional must not be passed directly to
# the std::optional-based, JUCE-independent transport detector.
assert 'const auto hostSamplePosition = position->getTimeInSamples();' in processor
assert 'std::optional<int64_t> samplePosition;' in processor
assert 'if (hostSamplePosition.hasValue())' in processor
assert 'samplePosition = static_cast<int64_t>(*hostSamplePosition);' in processor
assert 'hostPlayDetector.observe(position->getIsPlaying(), samplePosition,' in processor
assert 'hostPlayDetector.observe(position->getIsPlaying(), position->getTimeInSamples()' not in processor
assert processor.index('hostPlayDetector.observe(') < processor.index('if (midi.isEmpty() && voicePool.activeVoiceCount() == 0')
assert 'voicePool.restartFreeLfosOnTransportPlay();' in processor
assert 'lsampler::HostPlayRestartDetector hostPlayDetector;' in header
assert 'void GlobalVoicePool::restartFreeLfosOnTransportPlay() noexcept' in voice
assert 'if((*states)[size_t(slot)].lfo[size_t(i)].trigger)continue;' in voice
assert 'if(!slot.lfo[size_t(i)].trigger)' in voice
assert 'if(!voice.active||voice.preview' in voice
assert 'for(auto& voice:retiringVoices)resetVoice(voice)' in voice
assert 'monoFreeLfoRandom' in voice and 'monoFreeLfoRandom' in voice_header
assert 'freeLfoSeed(voice.slotIndex,index,voice.note)' in voice
assert 'useIndependentFreeLfoRandom(hostFreeLfoTransportStarted,ss.lfo[size_t(i)].trigger)' in voice
assert 'useIndependentFreeLfoRandom(hostFreeLfoTransportStarted,s.trigger)' in voice
assert 'nextFreeLfoHold(randomState)' in voice
# Regression: TEST59 bitwise parity must survive without a Play event.
selector=(r/'Source/FreeLfoTransport.h').read_text(encoding='utf-8')
assert 'return hostPlaySeen && !triggered;' in selector
assert 'hostFreeLfoTransportStarted=false;' in voice
assert 'hostFreeLfoTransportStarted=true;' in voice
assert 'if(!useIndependentFreeLfoRandom(hostFreeLfoTransportStarted,l.trigger))' in voice
assert 'if(useIndependentFreeLfoRandom(hostFreeLfoTransportStarted,l.trigger))' in voice
assert 'useIndependentFreeLfoRandom(hostFreeLfoTransportStarted, s.lfo[size_t(i)].trigger)' in voice
assert 'useIndependentFreeLfoRandom(hostFreeLfoTransportStarted,ss.lfo[size_t(i)].trigger)' in voice
assert 'if((*states)[size_t(slot)].lfo[size_t(i)].trigger)continue;' in voice
assert 'LSampler24FreeLfoTests' in cmake
assert 'free-lfo-transport' in (r/'Tools/build-windows.ps1').read_text(encoding='utf-8')
assert 'Tools/verify-test170-free-lfo-play.py' in workflow
assert 'LSampler-24-Windows-VST3-TEST170.zip' in workflow
assert 'TEST170' in (r/'Help/LSampler24Help.html').read_text(encoding='utf-8')
print('PASS TEST170: Free LFO host restart, mono/poly S&H RNG isolation, idle processing and metadata')
