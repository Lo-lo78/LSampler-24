#!/usr/bin/env python3
"""TEST173 structural verification and independent reference for delayed sample-window math."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
editor=(r/'Source/PluginEditor.cpp').read_text(encoding="utf-8")
proc=(r/'Source/PluginProcessor.cpp').read_text(encoding="utf-8")
voice=(r/'Source/SamplerVoice.cpp').read_text(encoding="utf-8")
state=(r/'Source/SlotAudioState.cpp').read_text(encoding="utf-8")
for marker in ('L"Octave"','L"Delay"','info.octave','info.delayMs','sampleSetField <= 6','sampleSetField,7,'):
    assert marker in editor,marker
for marker in ('"octave"','"delayMs"','sampleDelayRawFrames','applySampleSetGlobalWindow(state)','applySampleSetGlobalWindow(target.state)','h.sampleMix[size_t(field)][size_t(j)]','if (field >= 2 && field <= 6)'):
    assert marker in proc,marker
for marker in ('sampleOctave[size_t(v.sampleIndex)]*12','delayFramesRemaining>0','sampleDelayFrames[size_t(sampleIndex)]'):
    assert marker in voice,marker
for marker in ('timelineSeconds=std::max','sampleDelayRawFrames','endInSource','startInSource','prepareSamplePlaybackState(s.params,sample,sr,first,last)'):
    assert marker in state,marker
# Example discussed with user: absolute end is the last sample end plus its delay.
tracks=[(0,1.0),(0.3,1.5),(0.5,2.0)]
end=max(delay+length for delay,length in tracks)
assert abs(end-2.5)<1e-10
start,endcut=.2*end,.8*end
assert abs(start-.5)<1e-10 and abs(endcut-2.0)<1e-10
# If Start is later than a sample's onset, skip the elapsed part of that sample.
assert max(0,start-tracks[1][0])==.2
# If Start precedes onset, retain only the unelapsed delay.
assert max(0,tracks[2][0]-start)==0
print('PASS TEST173: sample mixer automation, octave, independent delay and global window structural checks')
