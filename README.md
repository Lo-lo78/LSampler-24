# LSampler-24 — TEST2 24 Slots Only

Diagnostic build rebuilt directly from the stable TEST2 BUILD_FIX1.

This stage adds only the 24-slot data/UI layer:
- 24 real slot states
- 3 columns x 8 rows slot grid
- current-slot selection
- each slot keeps its own sample, Root Note and Volume
- slot/bank/project persistence covers all 24 slots

Audio engine is intentionally unchanged from TEST2:
- one original TEST2 VoiceBank
- 16 voices
- only the currently selected slot is connected to that VoiceBank

No 96-voice pool, no Mono/Poly, no Low/High Key and no parameter sub-grid yet.
This build exists only to verify that expanding TEST2 from one stored slot to 24 slots is stable in REAPER.
