# LSampler-24 0.99.29 — Phase per Slot

**Release date:** 20 November 2026  
**Previous public release:** 0.99.28 (16 November 2026)  
**Source:** 0.99.28 TEST189  
**Platform:** Windows x64 VST3

## Phase for every Slot (TEST189)

- Each of the **24 Slots** now has a **Phase** control in the Slot page, immediately after **Pan**.
- Range **-180° to +180°**, step **1°**, default **0°**. At 0° the rotator is an exact bypass, so older presets and projects keep their original sound.
- Phase rotates the **Slot audio signal**, not an LFO. It uses a **zero-latency first-order all-pass referenced to 1 kHz** with unity magnitude.
- **+180° and -180° are exact polarity inversion.** Intermediate angles have the requested phase at 1 kHz; because the rotator adds no latency, the phase response necessarily varies with frequency.
- The rotator is applied per voice after the Machine stage and before de-click fades, FX sends and the Slot output. Coefficient and polarity changes are smoothed over **10 ms** for host automation.
- **Phase is available in Grid/Value, REAPER automation (Ctrl+Shift+A), Bank Macro and Slot/Bank presets.**
- The new VST3 ID is `slotXX_phase` (one per Slot), appended **after the complete TEST188 batch**. All previously published IDs, indices, automation and presets remain unchanged.

## Compatibility and verification

- `P::phase = 173`, `parameterCount = 174`, Grid size **198**; Phase appears after Pan in the accessible grid but remains appended in the host ID order.
- The first **173** parameter names and descriptors are frozen by SHA-256 in `Tools/verify-test59-reference.py`; the TEST59 reference renderer is untouched.
- New standalone `phase-rotation` C++ test checks unity magnitude, the 1 kHz reference angle and exact ±180° polarity. `Tools/verify-test189-phase.py` checks the catalog, host/macro lanes, DSP placement and rotator math.
- The TEST188 AHDSR Hold, TEST187 Self-Lace and every earlier feature remain unchanged.

## Documentation, accessibility and publishing

- Updated the accessible **About (Alt+A)** to version **0.99.29**, dated **20 November 2026**, with the new Phase feature.
- Updated the **eight-language integrated Help**, README, release metadata (`CMakeLists.txt`) and GitHub Actions checks.
- Windows VST3 artifact name: **LSampler-24.0.99.29.vst3.zip**.
- The complete Windows VST3 build, audio comparison and REAPER/NVDA behavior should be verified on GitHub Actions and in the DAW before publication.

More technical information: `RELEASE_TEST189.md`, `RELEASE_0.99.28.md` and the test scripts under `Tools/`.
