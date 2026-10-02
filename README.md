# LSampler-24 — TEST2 24 Slots + Slot Parameters

Diagnostic build based on the stable TEST2 audio engine.

Added in this step only:
- 24-slot 3 x 8 slot grid.
- Silent/contained arrow-key borders.
- Agreed keyboard shortcuts.
- Enter on a slot opens that slot's parameter controls.
- Esc returns to the same slot.
- Slot parameters currently stored/saved: Low Key, High Key, Original Pitch, Volume.
- New-slot defaults: Low Key 0, High Key 127, Original Pitch 60.
- MIDI note display follows the Lua convention: MIDI number + note name + octave.

Important diagnostic limitation:
The audio engine is still the original single VoiceBank from TEST2. Only the currently selected slot is connected to playback. Low/High Key are persisted but are not yet used for multi-slot MIDI routing. Overlapping regions will be enabled in the next isolated step after this build proves stable.
