"""Check the frozen reference and its shared DSP dependencies, without compiling."""
from pathlib import Path
import hashlib
import json
import sys
root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'Tests/ReferenceTEST59/SHA256.json').read_text(encoding='utf-8'))
errors = []
# TEST62 legitimately extends these live candidate-side files for the 16-sample Sample Set.
# They can no longer be byte-frozen to TEST59. The frozen TEST59 SamplerVoice sources
# remain hash-protected, and FilterParityTests still compares their single-sample output
# bit-for-bit against the TEST62 candidate.
mutable_candidate_dependencies = {
    'Source/SlotAudioState.h',
    'Source/SlotAudioState.cpp',
    'Source/PluginProcessor.cpp',
}
# TEST160 extends the candidate catalog with 14 LFO controls. The TEST59
# reference engine is still frozen and checked against its original hashes.
# Approve only the exact extended candidate header; do NOT loosen protection
# of the frozen renderer or other shared DSP dependencies.
approved_candidate_hashes = {
    'Source/Parameters.h': 'eb1985d287fe53ed9a3c5335dfb5c9b5762cf6e310f9ced66ed079f608bd9b14',
}

# The 143 parameter IDs and descriptors that existed before TEST160 must be
# byte-for-byte equivalent at the semantic-entry level, even though the header
# itself necessarily changes to append the new 14 entries. Hashes below were
# recorded from the supplied, unmodified TEST159 source catalog.
legacy_enum_sha256 = 'acc9479c38c54ac74975e3f48e54f38103cf95717d6ef2d61842f18fa9182687'
legacy_descriptors_sha256 = '405a2ca52967128ee9f57ac6d03fb01e3c26fa9a65515bbbc4b84f7d9de6433b'

def legacy_catalog_entries(source):
    enum = source.split('enum class P {', 1)[1].split('    count\n', 1)[0]
    names = [line.strip().rstrip(',') for line in enum.splitlines()
             if line.strip() and not line.strip().startswith('//')]
    descriptors_block = source.split('inline constexpr std::array<Descriptor, parameterCount> parameters {{', 1)[1].split('}};', 1)[0]
    descriptors = [line.strip().rstrip(',') for line in descriptors_block.splitlines()
                   if line.strip().startswith('{"')]
    return names, descriptors

try:
    source = (root / 'Source/Parameters.h').read_text(encoding='utf-8').replace('\r\n', '\n')
    enum_names, descriptors = legacy_catalog_entries(source)
    if len(enum_names) != 159 or len(descriptors) != 159:
        errors.append('TEST161 parameter count (expected 143 legacy + 16 added)')
    if hashlib.sha256('\n'.join(enum_names[:143]).encode('utf-8')).hexdigest() != legacy_enum_sha256:
        errors.append('TEST160 changed original 143 enum IDs or their order')
    if hashlib.sha256('\n'.join(descriptors[:143]).encode('utf-8')).hexdigest() != legacy_descriptors_sha256:
        errors.append('TEST160 changed original 143 parameter descriptors')
except (OSError, IndexError, ValueError):
    errors.append('TEST160 cannot parse parameter catalog')
for relative, expected in manifest.items():
    expected = approved_candidate_hashes.get(relative, expected)
    if relative in mutable_candidate_dependencies:
        continue
    path = root / relative
    # Git may check out CRLF on Windows; hash canonical LF text, not checkout bytes.
    actual = hashlib.sha256(path.read_text(encoding='utf-8').replace('\r\n', '\n').encode('utf-8')).hexdigest() if path.is_file() else None
    if actual != expected:
        errors.append(relative)
if errors:
    print('FAIL: frozen TEST59 reference or shared dependency changed: ' + ', '.join(errors))
    sys.exit(1)
print('PASS: frozen TEST59 renderer and unchanged shared DSP dependencies match their recorded hashes.')
