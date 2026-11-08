#!/usr/bin/env python3
"""TEST184: avoid the invalid MSVC if-condition from TEST182/183."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
src = (root / "Source/MediaPasteExpansion.cpp").read_text(encoding="utf-8")
assert "std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));" in src
assert "if (reader != nullptr)" in src
assert "return splitNativeAudio(source, *reader, maxTracks, stop, progress, error, discardedTracks);" in src
assert not re.search(r"if\s*\(\s*std::unique_ptr<juce::AudioFormatReader>", src)
assert src.index("std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));") < src.index("if (reader != nullptr)") < src.index("return splitNativeAudio(source, *reader, maxTracks, stop, progress, error, discardedTracks);")
assert (root / "RELEASE_TEST184.md").exists()
workflow = (root / ".github/workflows/build-windows.yml").read_text(encoding="utf-8")
assert "verify-test184-media-reader-declaration.py" in workflow
assert "LSampler-24-Windows-VST3-TEST185.zip" in workflow
print("PASS TEST184: MSVC-invalid reader declaration replaced; Windows workflow and release metadata updated")
