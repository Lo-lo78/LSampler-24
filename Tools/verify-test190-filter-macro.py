#!/usr/bin/env python3
"""TEST190 physical cutoff pair: static routing plus actual standalone C++ math."""
from pathlib import Path
import shutil, subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
read=lambda f:(root/f).read_text(encoding='utf-8')
p=read('Source/Parameters.h'); host=read('Source/HostParameter.h'); proc=read('Source/PluginProcessor.cpp'); voice=read('Source/SamplerVoice.cpp')
assert 'int(P::hp_key_follow)==174' in p and 'int(P::bp_cutoff)==175' in p
assert '(v.note-p[P::root])*p[P::hp_key_follow]' in voice
assert 'bp->linkedLow=h.values[size_t(P::lp_cutoff)]' in proc
assert 'bp->linkedHigh=h.values[size_t(P::hp_cutoff)]' in proc
assert 'linkedLow->storeReal(pair[0]); linkedHigh->storeReal(pair[1]);' in host
assert proc.index('// TEST190: append') > proc.index('// TEST189: one Phase control')
assert 'p == P::bp_cutoff' in read('Source/BankMacroParameters.h')
assert 'bp_key_follow' not in p
cxx=shutil.which('g++') or shutil.which('clang++')
if cxx:
    with tempfile.TemporaryDirectory() as tmp:
        exe=Path(tmp)/('filter_test.exe' if sys.platform=='win32' else 'filter_test')
        subprocess.run([cxx,'-std=c++17','-O2',str(root/'Tests/FilterMacroTests.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
else:
    print('PASS static checks; no standalone compiler available')
