# LSampler-24 0.99.28 — AHDSR Hold and Self-Lace accessibility

**Release date:** 16 November 2026  
**Previous public release:** 0.99.27 (14 November 2026)  
**Source:** 0.99.27 TEST188 (including TEST187 Self-Lace fix)  
**Platform:** Windows x64 VST3

## AHDSR Hold for all three Slot envelopes (TEST188)

- Each of the **24 Slots** now has **three independent Hold parameters** after the respective Attack stage: **Env Hold** (amplitude), **LP Env Hold** (Low Pass filter) and **HP Env Hold** (High Pass filter).
- The stages follow **Attack → Hold → Decay → Sustain → Release**. Hold ranges from **0.000 to 5.000 seconds**, in **0.001 s (1 ms)** increments, with a **0.000 s default** to retain the sound of older presets.
- Note Off immediately starts Release, even during Hold. Mono/Poly and legato voice handling remain supported.
- Each Hold control can be edited via accessible Grid/Value, exposed as a new independent VST3 automation parameter per Slot, and stored in Slot/Bank presets and Bank Macro assignments.
- **Ctrl+Shift+A**, the existing shortcut that toggles an envelope's active state in REAPER while preserving its points, is also supported on these new Hold parameters. NVDA continues to announce **", Auto"** for active automation.
- All previously published VST3 parameter IDs and indices remain unchanged. The new Hold parameters are appended after the existing LFO automation descriptors, not inserted into earlier host indices.
- Existing Gate Hold, dynamics processors, Sample Set handling, and other modulators are unchanged.

## Self-Lace selection accessibility (TEST187)

- **LFO 1 Lace Source**: Off, LFO 1 (Self), LFO 2.
- **LFO 2 Lace Source**: Off, LFO 1, LFO 2 (Self).
- Every keyboard navigation action moves by one selection even when Grid Step is 5, 10, 15 or 20. Previously saved source values are unchanged.

## Documentation, accessibility and publishing

- Updated the accessible **About (Alt+A)** to version **0.99.28**, dated **16 November 2026**, with these new features.
- Updated the **eight-language integrated Help**, README and release metadata (`CMakeLists.txt`).
- Updated GitHub Actions version checks and Windows VST3 artifact name: **LSampler-24.0.99.28.vst3.zip**.
- Added the 0.99.28 release note to the build artifact, retaining earlier test/release histories.
- The entire Windows VST3 build, audio comparison and REAPER/NVDA behavior should be verified on GitHub Actions and in the DAW before publication.

More technical information: `RELEASE_TEST188.md`, `RELEASE_0.99.27.md` and the test scripts under `Tools/`.
