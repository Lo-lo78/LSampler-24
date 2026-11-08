#!/usr/bin/env python3
"""TEST185: verify that Windows default cp1252 cannot break cache verification."""
import ast
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
script = root / 'Tools/verify-test183-cache-cleanup.py'
source = script.read_text(encoding='utf-8')
tree = ast.parse(source)
read_calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
              and isinstance(node.func, ast.Attribute) and node.func.attr == 'read_text']
assert len(read_calls) == 6, f'Unexpected read_text count: {len(read_calls)}'
for node in read_calls:
    assert any(k.arg == 'encoding' and isinstance(k.value, ast.Constant)
               and str(k.value.value).lower().replace('_','-') == 'utf-8'
               for k in node.keywords), 'Windows platform-dependent text decoding detected'
workflow = (root / '.github/workflows/build-windows.yml').read_text(encoding='utf-8')
assert 'PYTHONUTF8:' in workflow and 'verify-test185-windows-utf8.py' in workflow
assert 'LSampler-24.0.99.26.vst3.zip' in workflow
result = subprocess.run([sys.executable, str(script)], cwd=str(root),
                        capture_output=True, text=True, encoding='utf-8')
if result.returncode:
    raise RuntimeError('TEST183 cache check failed:\n' + result.stdout + result.stderr)
print('PASS TEST185: explicit UTF-8 source reads, TEST183 passes, Windows CI UTF-8 enforced')
