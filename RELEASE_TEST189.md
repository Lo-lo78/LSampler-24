# LSampler-24 0.99.28 TEST189 — Phase per Slot

**Test source revision; public release remains 0.99.28 until the Windows VST3 is built and checked.**

- One **Phase** control per Slot, immediately after Pan in the Slot page.
- Range **-180° to +180°**, **1°** step, default **0°**. 0° is an exact bypass; ±180° is polarity inversion.
- Rotates the **Slot audio signal**, not an LFO, with a zero-latency first-order all-pass referenced to **1 kHz**. Magnitude is unity; the phase response varies with frequency because no latency is added.
- DSP order: after the Machine stage, before de-click fades, FX sends and Slot output. 10 ms smoothing for automation; the filter state is reset while Phase is 0°.
- New appended VST3 ID `slotXX_phase` (24 Slots), one appended Bank Macro lane, and Slot/Bank preset support. No earlier ID or index moves.
- `P::phase = 173`, `parameterCount = 174`, Grid size **198**; Phase is placed after Pan in the accessible grid.
- The first **173** parameter names and descriptors are frozen by SHA-256; TEST59 reference DSP is untouched.
- New `phase-rotation` C++ test and `Tools/verify-test189-phase.py`; TEST188 Hold, TEST187 Self-Lace and all earlier tests remain in the workflow.
- Updated About, CMake, README, eight-language Help and GitHub Actions packaging for **LSampler-24.0.99.29.vst3.zip**.

Compile the Windows VST3 through the existing GitHub Actions workflow; a complete Windows VST3 build is not asserted by the source-level tests.
