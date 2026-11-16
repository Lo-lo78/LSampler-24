# LSampler-24 0.99.27 TEST188 — AHDSR Hold

**Test source revision; public release remains 0.99.27.**

- Three independent AHDSR envelopes in every Slot: Amp, Low Pass Filter, High Pass Filter.
- New controls directly after each Attack: Env Hold, LP Env Hold and HP Env Hold.
- Time 0.000–5.000 s, 0.001 s step, default zero. Hold keeps the peak level between Attack and Decay.
- Note Off immediately starts Release even if Hold is running. Mono/Poly retriggers initialise separate per-voice counters.
- DAW automation through stable new VST3 IDs `slotXX_env_hold`, `slotXX_lp_env_hold`, `slotXX_hp_env_hold` (24 Slots). Three appended Bank Macro lanes.
- All previously published parameter IDs and host indices are frozen; new IDs follow TEST186, no renumbering. Older preset/slot/bank/project states default to zero Hold.
- Existing Gate Hold, Dynamics, LFO Sample & Hold, and Sample Sets are unchanged.
- TEST188 executable C++ envelope regression + source verification added to GitHub Actions.
- TEST59 frozen DSP reference remains untouched; previous 170 catalog descriptors are checked by SHA-256.

Compile the Windows VST3 through the existing GitHub Actions workflow; a complete Windows VST3 build is not asserted by the source-level tests.
