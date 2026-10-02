# LSampler-24 TEST4

24-slot accessible sampler build with corrected neutral Original Pitch calibration.

## Library

`Documents/LSampler-24/Library/`

- `Samples/` raw audio files with their real extensions.
- `Slots/` `.lsampler-24-s` single-slot instrument snapshots.
- `Banks/` `.lsampler-24-b` complete 24-slot snapshots.

Audio is raw shared material. Slot and Bank files contain configuration and references to audio; they do not embed or duplicate audio.

## TEST4

- 24 real slots in the same 3 x 8 layout as LBPMCaptureSample.
- 96 dynamically shared voices per plugin instance.
- Slot parameter order: Low Key, High Key, Original Pitch, Voice Mode, Mono Mode, Volume.
- New slots default to the full MIDI range: Low Key 0, High Key 127 (128 MIDI notes total).
- Original Pitch factory default: MIDI 60, spoken/displayed as `60 C 4`, for every slot.
- Notes inside Low Key..High Key play chromatically relative to Original Pitch.
- Existing saved slot/bank/project states keep their explicitly stored Original Pitch.
- Full project persistence for all 24 slots.
- Save/Load Slot affects the current slot.
- Save/Load Bank affects all 24 slots.

### Note representation

The three note parameters use the LBPMCaptureSample representation:

- `36 C 2`
- `37 C sharp 2`
- `60 C 4`

### Keyboard

- Arrows: move in the 3 x 8 slot grid.
- Home/End: first/last row in the current column.
- Ctrl+Home/Ctrl+End: Slot 1 / Slot 24.
- Page Up/Page Down: move four rows in the current column.
- Enter: open current slot parameters.
- Escape: return to the same slot.
- Alt+L: Slots.
- Alt+O: Load Sample.
- Alt+S: Load Slot.
- Alt+Shift+S: Save Slot.
- Alt+B: Load Bank.
- Alt+Shift+B: Save Bank.

In the parameter view, Up/Down moves between parameters; Left/Right edits the current parameter.
