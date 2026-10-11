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
# TEST128: candidate parameter catalog also appends per-subloop Loop Level,
# and clarifies the Release Loop title. The frozen TEST59 reference stays intact;
# permit only this exact candidate file, not arbitrary header changes.
approved_candidate_hashes = {
    'Source/Parameters.h': 'f9ad14ab98ddaf003ff16dd56d225901026508c5494ff808d242ad4769e06972',
}
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
