# LSampler-24 — accessible 24-slot layering + global 96-voice pool

This build continues from the stable TEST2-derived line.

## Slots and layering
- 24 real slots in a 3 x 8 accessible grid.
- Slots are focusable elements, not buttons.
- Ctrl+Home / Ctrl+End move to Slot 1 / Slot 24.
- Low Key / High Key regions may overlap freely; every matching slot sounds.
- All slots share one global pool of 96 voices. Voices are allocated dynamically and the oldest voice is stolen only when all 96 are active.
- Original Pitch remains per slot and defaults to MIDI 60 (C 4).

## Parameter page / LJuno-style navigation
- Enter on a slot opens the parameter grid.
- Grid parameters: Low Key, High Key, Original Pitch, Volume.
- Alt+V enters Value.
- Tab cycle: Grid -> Value -> numeric edit -> Grid.
- Shift+Tab moves in the reverse direction.
- Enter from Value returns to Grid.
- Enter from numeric edit commits the typed value and returns to Grid.
- Alt+V from numeric edit commits and returns to Value using the same accessible entry path as Grid -> Value.
- Escape from numeric edit returns directly to the current slot.
- Alt+L is not used.
- Grid entry metadata no longer advertises Alt+L.
- Value entry keeps the LJuno-style Alt+V accessibility hint.
- Value changes announce only the new value.
- Value step widths: 1, 5, 10, 15, 20; Page Up/Down uses current step x40.

## Preview
- Space is NOT intercepted on the 24-slot page, so REAPER transport can use Space normally.
- Space is intercepted only while inside the current slot parameter page (Grid / Value / numeric edit).
- Preview is a true Play/Stop toggle: first Space starts from the beginning, second stops, next starts from the beginning. Preview voices do not stack with themselves.

## Load focus
- After loading a sample, focus returns to the current slot and its slot label is announced.
