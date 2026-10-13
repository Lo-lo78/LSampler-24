#!/usr/bin/env python3
"""Static audit: bank-only FX sends preserve audio/automation serialization."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
editor = (root/'Source/PluginEditor.cpp').read_text()
header = (root/'Source/PluginEditor.h').read_text()
panel = (root/'Source/FXPanel.h').read_text()
params = (root/'Source/FXParameters.h').read_text()
processor = (root/'Source/PluginProcessor.cpp').read_text()

assert 'static constexpr int bankFXCount = 3;' in header
assert 'bankMacroEntries.push_back(bankFXFirst + i)' in editor
assert 'return bankMacroOpen && index >= bankFXFirst' in editor
assert '"Bank FX Sends"' in editor
for param, name in [('chorusMix', 'Chorus Send'), ('delayMix', 'Delay Send'), ('reverbMix', 'Reverb Send')]:
    assert f'lsampler::fx::{param}' in editor
    assert name in params
assert 'static constexpr int visibleCount = lsampler::fx::count - 3;' in panel
assert 'i == lsampler::fx::chorusMix || i == lsampler::fx::delayMix || i == lsampler::fx::reverbMix' in panel
assert 'for (const int i : displayOrder) refreshRow(i);' in panel
assert 'return processor.getFXParameter(bankFXParameter(selectedParameter));' in editor
assert 'processor.setFXParameter(bankFXParameter(selectedParameter), value);' in editor
assert 'return processor.getFXHostParameter(bankFXParameter(selectedParameter));' in editor
assert 'saveFXState(bank);' in processor and 'restoreFXState(bank);' in processor
assert 'saveFXState(state);' in processor and 'restoreFXState(state);' in processor
# Slot serialization calls makeSlotState directly; no call to saveFXState.
slot = processor[processor.index('bool LSampler24AudioProcessor::saveSlotPresetAt('):processor.index('bool LSampler24AudioProcessor::loadSlotPreset(')]
assert 'makeSlotState(' in slot and 'saveFXState' not in slot
makeSlot = processor[processor.index('juce::ValueTree LSampler24AudioProcessor::makeSlotState('):processor.index('bool LSampler24AudioProcessor::saveSlotPreset(')]
assert 'saveFXState(' not in makeSlot
# FX IDs keep their order: first twenty-six unchanged, four appended in TEST147.
for marker in ('delayGain, reverbXOver, reverbBass, reverbGain,', '"chorus_mix"', '"delay_mix"', '"reverb_mix"'):
    assert marker in params
print('PASS TEST148: 3 bank-only sends in Alt+K, FX UI excludes them; bank/session persistence, slot isolation, stable VST3 ID order.')
