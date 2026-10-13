#!/usr/bin/env python3
"""Source audit only; does not run CMake, compile, or execute the plugin.
Optional first argument: original JUCE 8.0.15 juce_FlagCache.h for patch audit.
"""
from pathlib import Path
import hashlib
import itertools
import re
import sys
root = Path(__file__).resolve().parents[1]
patch = (root / 'cmake/OptimiseJuceFlagCache.cmake').read_text()
old = re.search(r'set\(_lsampler_old \[==\[(.*?)\]==\]\)', patch, re.S).group(1)
new = re.search(r'set\(_lsampler_new \[==\[(.*?)\]==\]\)', patch, re.S).group(1)
original_hash = re.search(r'elseif\(_lsampler_flag_hash STREQUAL "([a-f0-9]+)"', patch).group(1)
patched_hash = re.search(r'if\(_lsampler_flag_hash STREQUAL "([a-f0-9]+)"', patch).group(1)
assert new.count('exchange (0, std::memory_order_acq_rel)') == 1
assert new.index('load (std::memory_order_acquire)') < new.index('exchange (0,')
assert 'include(cmake/OptimiseJuceFlagCache.cmake)' in (root / 'CMakeLists.txt').read_text()
if len(sys.argv) > 1:
    original = Path(sys.argv[1]).read_text()
    assert hashlib.sha256(original.encode()).hexdigest() == original_hash
    assert original.count(old) == 1
    result = original.replace(old, new)
    assert hashlib.sha256(result.encode()).hexdigest() == patched_hash
    assert result.replace(new, old) == original
    print('PASS: exact upstream hash, patch output hash, reversible one-site patch.')

# Enumerate two distinct producer writes racing the consumer load/exchange.
# Every written flag must be drained now or remain pending for the next drain.
# This is a small sequentially consistent interleaving model, not a native
# concurrency test; the native regression is included in the CI executable.
for order in itertools.permutations(('write1','write2','load','exchange')):
    if order.index('load') > order.index('exchange'):
        continue
    word = drained = 0
    take = False
    for event in order:
        if event == 'write1': word |= 1
        elif event == 'write2': word |= 2
        elif event == 'load': take = word != 0
        elif take:
            drained = word
            word = 0
    assert (drained | word) == 3
    assert (drained & word) == 0
print('PASS: two-producer/consumer flag interleavings retain every flag.')
voice = (root / 'Source/SamplerVoice.cpp').read_text()
assert 'l.phase+=phaseIncrement;' in voice
assert 'lfoTickPrepared(v,0,s.lfo[0],lfo1Step)' in voice
assert 'lfoTickPrepared(v,1,s.lfo[1],lfo2Step)' in voice
assert 's.lfo[0].rate*(s.lfo[0].sync?bpm:1)' in voice
assert 's.lfo[1].rate*(s.lfo[1].sync?bpm:1)' in voice
ci = (root / 'Tests/FilterParityTests.cpp').read_text()
assert 'runFlagCacheTests(report);' in ci and 'benchmarkFlagCache(report);' in ci
print('PASS: prepared LFO increments and native CI tests wired. Native tests NOT run by this audit.')
