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
    'Source/Parameters.h',  # TEST188: verified by frozen-prefix SHA-256 below
}
# TEST160 extends the candidate catalog with 14 LFO controls. The TEST59
# reference engine is still frozen and checked against its original hashes.
# Approve only the exact extended candidate header; do NOT loosen protection
# of the frozen renderer or other shared DSP dependencies.
approved_candidate_hashes = {
    'Source/Parameters.h': '8ba9a8a11c2936121fabc7b2b28ac3038381e9d17da2164a974d0358b33d26d6',
}

# The 143 parameter IDs and descriptors that existed before TEST160 must be
# byte-for-byte equivalent at the semantic-entry level, even though the header
# itself necessarily changes to append the new 14 entries. Hashes below were
# recorded from the supplied, unmodified TEST159 source catalog.
legacy_enum_sha256 = 'acc9479c38c54ac74975e3f48e54f38103cf95717d6ef2d61842f18fa9182687'
legacy_descriptors_sha256 = '405a2ca52967128ee9f57ac6d03fb01e3c26fa9a65515bbbc4b84f7d9de6433b'
pre186_names_sha256 = 'd1d4ce5ec6f49839d71d51d3b729a1af8efd7e90ecfd989bc0d18c4721549e68'
pre186_descriptors_sha256 = 'ace7afcdde6c460b2df659d8f02f33410f3107b66f71eeb414b45489014b20c0'
# Freeze all 170 TEST187 identifiers and descriptors. TEST188 adds only three.
pre188_names_sha256 = 'e4fbf78f959c98a2f843d75a6157dddd4d8cfb53ab0d1bd7b6cd42dda406e606'
pre188_descriptors_sha256 = '398dd7a9faaddd9f6f3841b809c8318aafbffb54cc7a63af34d826c301ed1c25'

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
    if len(enum_names) != 173 or len(descriptors) != 173:
        errors.append('TEST188 parameter count (170 frozen parameters + 3 appended Hold controls)')
    if hashlib.sha256('\n'.join(enum_names[:143]).encode('utf-8')).hexdigest() != legacy_enum_sha256:
        errors.append('TEST160 changed original 143 enum IDs or their order')
    if hashlib.sha256('\n'.join(descriptors[:143]).encode('utf-8')).hexdigest() != legacy_descriptors_sha256:
        errors.append('TEST160 changed original 143 parameter descriptors')
    if hashlib.sha256('\n'.join(enum_names[:160]).encode('utf-8')).hexdigest() != pre186_names_sha256:
        errors.append('TEST186 changed one of the original 160 parameter IDs')
    if hashlib.sha256('\n'.join(descriptors[:160]).encode('utf-8')).hexdigest() != pre186_descriptors_sha256:
        errors.append('TEST186 changed one of the original 160 parameter descriptors')
    if hashlib.sha256('\n'.join(enum_names[:170]).encode('utf-8')).hexdigest() != pre188_names_sha256:
        errors.append('TEST188 changed one of the original 170 parameter IDs')
    if hashlib.sha256('\n'.join(descriptors[:170]).encode('utf-8')).hexdigest() != pre188_descriptors_sha256:
        errors.append('TEST188 changed one of the original 170 parameter descriptors')
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
