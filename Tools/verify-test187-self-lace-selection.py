#!/usr/bin/env python3
"""TEST187: Self-Lace must be reachable with every global grid step width."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
p=(root/'Source/Parameters.h').read_text(encoding='utf-8')
e=(root/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
a=(root/'Source/SlotAudioState.cpp').read_text(encoding='utf-8')
v=(root/'Source/SamplerVoice.cpp').read_text(encoding='utf-8')
for i,labels in [(1,'Off|LFO 1 (Self)|LFO 2'),(2,'Off|LFO 1|LFO 2 (Self)')]:
    line=next(x for x in p.splitlines() if x.startswith('{"lfo%d_lace_source"'%i))
    assert 'Kind::enumeration, 0, 0, 2, 1, 1' in line
    assert '"'+labels+'"' in line
    assert 'P::lfo%d_lace_source'%i in a
    assert 'src>=1 && src<=2?previousLfo[src-1]:0.0' in v
assert 'laceSourceNavigationStep' in p
assert 'lsampler::laceSourceNavigationStep(selectedEntry().parameter, requestedStep)' in e
assert '(!bankMacroOpen || bankMacroUniform) && selectedEntry().loop < 0' in e
assert 'previousAudible[src-1]:0.0' in v
print('PASS TEST187 self-Lace options, one-step navigation and stored mapping')
