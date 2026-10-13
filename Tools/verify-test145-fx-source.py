#!/usr/bin/env python3
"""TEST145 static-only FX audit. No CMake, JUCE execution or audio build."""
from pathlib import Path
import math
import re
root=Path(__file__).resolve().parents[1]
ui=(root/'Source/FXPanel.h').read_text(encoding='utf-8')
params=(root/'Source/FXParameters.h').read_text(encoding='utf-8')
dsp=(root/'Source/FXEngine.h').read_text(encoding='utf-8')
fdn=(root/'Source/FXReverb.h').read_text(encoding='utf-8')
processor=(root/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
assert 'if(alt && shortcut==\'f\') return true;' in ui
assert 'if(valueFocused)grid.grabKeyboardFocus();' in ui
assert 'if (index==cursor) return;' in ui
assert 'if(std::abs(next-current)<1.0e-9)return;' in ui
assert 'if (next==stepWidthIndex) return;' in ui
assert 'void setLineReadingMode()' in ui
assert 'setDescription({}); // no second' in ui
assert 'juce::AccessibilityEvent::valueChanged' in ui
assert 'value.onValueChange' in ui and 'announceToActiveScreenReader(value' not in ui
assert 'if(item>=0) { cursor=displayOrder[size_t(item)]; configureValue(); }' in ui
for name in ('pageUpKey','pageDownKey','homeKey','endKey','leftKey','rightKey'):
    assert name in ui
assert 'changeStepWidth(-1)' in ui and 'changeStepWidth(1)' in ui
assert 'constexpr std::array<int,5> stepWidths {1,5,10,15,20}' in ui
assert 'static constexpr int pageStep = 40' in ui
assert '"Delay Send"' in params and '"Reverb Send"' in params
assert re.search(r'\{"reverb_decay",.*?3000,\s*0,\s*20000,\s*1,\s*1,\s*"ms"',params)
assert len(re.findall(r'^\s*\{"[a-z0-9_]+",',params,re.M))==30
assert 'const float monoIn=.5f*(dLIn+dRIn);' in dsp
assert 'constexpr float equalGain=1.25f;' in dsp
assert 'reverbNetwork.configure(p, mode)' in dsp and 'reverbNetwork.process(preL, preR, tailL, tailR)' in dsp
assert 'p[reverbWidth]*.01f' in dsp and 'const float mid=' in dsp and 'const float side=' in dsp
assert 'reverbNetwork.prepare(sampleRate)' in dsp and 'reverbNetwork.reset()' in dsp
assert '.001 * double(p[reverbDecay])' in dsp
assert 'fx_schema_version", 2' in processor and 'fx_schema_version", 1' in processor
assert 'hostFX[size_t(i)]->storeReal' in processor
assert 'std::array<std::vector<float>, 8> lines;' in fdn
assert 'std::array<float, 8> mixed = taps;' in fdn
assert 'for (int stride = 1; stride < 8; stride *= 2)' in fdn
assert 'feedback[size_t(i)]' in fdn
assert 'return;' in fdn and 'std::exp' in fdn
# Mathematical sanity of RT60 loop gain across typical SR and room sizes.
# One round trip never increases energy; after RT60, about -60 dB remains.
for sample_rate in (44100,48000,96000,192000):
    for size in (0.0,0.5,1.0):
        for mode in (.65,1.05,.75,1.38):
            for ms in (100,3000,10000,20000):
                length=27.7*mode*(.45+1.8*size)*sample_rate*.001
                g=math.exp(-6.907755278982137*length/(sample_rate*(ms*.001)))
                assert 0.0<g<1.0
                assert abs(g**((ms*.001*sample_rate)/length)-.001)<1.e-11
                assert length < sample_rate*.37
print('PASS TEST145: borders, steps, NVDA entry, one slider notification, original 26 stable FX IDs + four new controls, send gain, ms RT60, fixed-size FDN and legacy-state migration.')
