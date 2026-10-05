"""Check the frozen reference and its shared DSP dependencies, without compiling."""
from pathlib import Path
import hashlib
import json
import sys
root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'Tests/ReferenceTEST59/SHA256.json').read_text(encoding='utf-8'))
errors = []
for relative, expected in manifest.items():
    path = root / relative
    # Git may check out CRLF on Windows; hash canonical LF text, not checkout bytes.
    actual = hashlib.sha256(path.read_text(encoding='utf-8').replace('\r\n', '\n').encode('utf-8')).hexdigest() if path.is_file() else None
    if actual != expected:
        errors.append(relative)
if errors:
    print('FAIL: frozen TEST59 reference or shared dependency changed: ' + ', '.join(errors))
    sys.exit(1)
print('PASS: frozen TEST59 renderer and shared DSP dependencies match their recorded hashes.')
