#!/usr/bin/env python3
"""Source-only TEST110 ABI and macro integration audit; intentionally no build."""
from pathlib import Path
import json, re
root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'VST3_AUTOMATION_MANIFEST.json').read_text())
old_ids = [p['id'] for p in manifest]
assert len(old_ids) == 6629 and len(set(old_ids)) == 6629
text = (root / 'Source/Parameters.h').read_text()
entries = re.findall(r'^\{int\(P::(\w+)\),\s*-1,\s*Action::none,\s*"([^"]+)"\}', text, re.M)
excluded = set('low high root velocity_low velocity_high choke_trigger choke_target choke_mode normalize_on normalize_target ram_downsample ram_fade_in ram_fade_out ram_reverse dc_remove'.split())
macro_keys = {p for p, cat in entries if cat != 'Sample Window' and p not in excluded}
assert len(macro_keys) == 117, len(macro_keys)
new_ids = ['bankmacro_' + key for key in sorted(macro_keys)]

def vst_hash(name):
    result = 0
    for ch in name:
        result = (31 * result + ord(ch)) & 0xffffffff
    return result & 0x7fffffff
all_ids = old_ids + new_ids
assert len(set(all_ids)) == len(all_ids)
assert len({vst_hash(x) for x in all_ids}) == len(all_ids)
processor = (root / 'Source/PluginProcessor.cpp').read_text()
editor = (root / 'Source/PluginEditor.cpp').read_text()
assert processor.index('    // Append after the existing 6629') > processor.index('for(int slot=0;slot<slotCount;++slot) {')
assert 'bankMacroEligible(entry)' in processor and 'bankMacroEligible(e)' in editor
assert 'saveBankMacroState(bank);' in processor and 'saveBankMacroState(state);' in processor
assert 'restoreBankMacroState(bank);' in processor and 'restoreBankMacroState(state);' in processor
assert 'const bool macroChanged' in processor and 'const double offset' in processor
assert 'p.values[size_t(i)] = sanitise' in processor
assert 'processor.setBankMacroOffset(parameter, value);' in editor
assert 'processor.getBankMacroOffset(' in editor
print('PASS: 6629 original parameter IDs retained + 117 new macro IDs (6746 total); hash uniqueness, persistence, audio and UI routing.')
