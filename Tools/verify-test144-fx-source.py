#!/usr/bin/env python3
"""TEST144 static-only audit. No CMake, compiling, DSP rendering or binaries."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
fx=(r/'Source/FXParameters.h').read_text(encoding='utf-8')
dsp=(r/'Source/FXEngine.h').read_text(encoding='utf-8')
ui=(r/'Source/FXPanel.h').read_text(encoding='utf-8')
processor=(r/'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
editor=(r/'Source/PluginEditor.cpp').read_text(encoding='utf-8')
# Verify the FX master is initially bypassed and existing projects remain dry.
assert re.search(r'\{"master",\s*"FX Master".*?Kind::enumeration,\s*0,',fx)
assert re.search(r'\{"chorus_on",\s*"Chorus".*?Kind::enumeration,\s*0,',fx)
assert re.search(r'\{"delay_on",\s*"Dual Delay".*?Kind::enumeration,\s*0,',fx)
assert re.search(r'\{"reverb_type",\s*"Reverb Type".*?Kind::enumeration,\s*0,',fx)
ids=re.findall(r'^\s*\{"([a-z0-9_]+)",',fx,re.M)
assert len(ids)==26 and len(set(ids))==26,ids
assert processor.index('for (int i=0;i<lsampler::fx::count;++i) {\n        const auto& d=lsampler::fx::parameters') > processor.index('// TEST130: Only after ALL prior')
assert all(m in processor for m in ('saveFXState(bank);','restoreFXState(bank);','saveFXState(state);','restoreFXState(state);'))
assert 'fxEngine.hasTail()' in processor
assert processor.index('fxEngine.process(buffer, values)') < processor.index('    applyOutputStage(buffer, routes);')
assert 'std::vector<float> chorusL' in dsp and 'reverbA.processStereo' in dsp and 'pan1L' in dsp
assert all(m in ui for m in ("shortcut=='f'", 'getFXHostParameter(cursor)', 'processor.setFXParameter(cursor,', 'grid.grabKeyboardFocus()'))
assert 'void LSampler24AudioProcessorEditor::openFX()' in editor
assert 'sliceEditor->onRequestFX=' in editor
assert 'fxButton.setDescription("Alt+F.' in editor
print('PASS: FX off in old projects; 26 unique host IDs; appended automation; audio tail and persistence; FX UI and Slice shortcut.')
