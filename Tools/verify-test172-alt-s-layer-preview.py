#!/usr/bin/env python3
"""Verify Alt+S saved slot preview carries Sample Set and independent mixer controls."""
from pathlib import Path
source = Path("Source/PluginProcessor.cpp").read_text(encoding="utf-8")
header = Path("Source/PluginProcessor.h").read_text(encoding="utf-8")
required = [
    'target.owners = std::move(owners)',
    'target.state.sampleSet[size_t(index)] = sample',
    'target.state.samplePitchCents[size_t(index)] = pitchCents[size_t(index)]',
    'target.state.samplePan[size_t(index)] = pan[size_t(index)] * .01',
    'target.state.sampleVolumeGain[size_t(index)]',
    'target.state.sampleVelocityLow[size_t(index)]',
    'target.state.sampleVelocityHigh[size_t(index)]',
    'target.state.variationMode = variationMode',
    'previewState.variationMode == variationLayer',
    'libraryPreviewVoicePool.noteOn(0, previewNote, 1.0f, 0, true,',
    'restoreEditedAudio(tree.getChildWithName("EditedAudio"), file, error)',
]
for marker in required:
    assert marker in source, marker
assert 'std::array<std::shared_ptr<SharedSample>, sampleSetSize> owners {}' in header
print("PASS TEST172: Alt+S complete Sample Set preview including saved per-sample Pitch/Pan/Volume and Layer")
