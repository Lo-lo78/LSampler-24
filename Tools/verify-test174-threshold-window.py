from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = (root / "Source/SlotAudioState.cpp").read_text(encoding="utf-8")
processor = (root / "Source/PluginProcessor.cpp").read_text(encoding="utf-8")
assert "s.params[P::start_threshold]" in source
assert "s.params[P::end_threshold]" in source
assert "peaks[bin]=std::max" in source
assert "double(s.sampleDelayRawFrames[size_t(i)])/sr" in source
assert "windowEnd=std::max(a" in source
assert "applySampleSetGlobalWindow(state)" in processor
assert "applySampleSetGlobalWindow(target.state)" in processor
assert "applySampleSetGlobalWindow(out)" in processor
print("PASS TEST174: shared Sample Set threshold detection and delayed sample timeline")
