# LSampler-24 0.99.24 — TEST165

Date: 4 November 2026.

Search navigation aligned with LReaParams 0.0.63. The parameter search window is 1000 ms, matches the beginning of the entire parameter name or a later word (full-name match has priority), supports Unicode, Shift reverse and shifted digit keys, and preserves `0` for Zero Crossing while also allowing `Z`. A single letter repeated cycles results, with safe fallback for impossible prefixes.

Coverage: Slot/Global Grid, Bank Macro, Slice Editor parameter pages, FX, Sample Set F2. This update does not alter audio, MIDI routing, presets, VST3 parameter IDs or DSP. Multilingual accessible Help and About version bumped. Includes standalone C++ tests and GitHub Actions build gate. Windows build and REAPER/NVDA tests should still be confirmed before publishing.

Note: key auto-repeat suppression requires raw key-repeat event metadata; the pure search core supports it, but JUCE KeyPress does not itself report a native repeat bit. Do not claim exact native-repeat parity without REAPER/NVDA verification.
