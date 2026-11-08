# LSampler-24 TEST180 — Alt+V chromatic continuation across paste sessions

- Uses the exact same logic whether audio comes from REAPER selected/copied items or from Windows Explorer Ctrl+C (`CF_HDROP`).
- Reads the destination mapping before starting the existing background load, independently of previous Alt+V invocations. This makes results stable after saving and reopening a project.
- If replacing an occupied slot whose `Low Key`, `High Key` and `Original Pitch` match, that destination note is preserved as the first note.
- Otherwise finds the closest earlier occupied slot with an unambiguous one-key mapping. The note difference equals the slot difference, accounting for empty gaps. If there is no valid previous mapping, starts at MIDI 60 (C4).
- Does not treat custom ranges as one-note chromatic references, and does not change such ranges on other occupied slots.
- MIDI note 127 is the final usable note. Excess copied files are skipped rather than clamped to 127 or silently reusing a MIDI note.
- Nonblocking decoding, accessible progress and focus handling, slot-to-slot paste, overwrite confirmation, per-source caching and original audio engine remain unchanged from TEST179.
- Pure C++ unit tests: `Tests/ChromaticPasteMappingTests.cpp`; CI script: `Tools/verify-test180-chromatic-paste.py`.
- Windows VST3 runtime, REAPER and NVDA behaviour to be checked after GitHub Actions compilation.
