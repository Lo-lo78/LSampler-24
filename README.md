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

## Keyboard focus fix
- Slot buttons, action buttons and parameter sliders explicitly forward keyboard events to the editor KeyListener.
- Enter on a focused slot opens the slot parameter page.
- Escape from a parameter control returns to the same slot.
- Arrow/Home/End keys on Load/Save action buttons are consumed so focus cannot escape to the host.

## TEST update - LJuno value speech and preview toggle
- Parameter value changes announce only the new value, both from the parameter grid and Value.
- Value step widths match LJuno-116: 1, 5, 10, 15, 20.
- Page Up/Down uses the selected step x40.
- Space previews the current slot as a true Play/Stop toggle: Play starts from the beginning; Stop silences it; the next Play starts from the beginning. Preview voices never layer over themselves.

## Current accessibility / multi-slot test

- Slots are focusable elements in the 3x8 grid, not buttons.
- Ctrl+Home / Ctrl+End move to Slot 1 / Slot 24.
- Enter opens the LJuno-style parameter grid.
- The parameter grid is a single ComboBox-style accessible grid control; Value is a separate slider with Alt+V.
- Tab/Shift+Tab entry announcements follow the LJuno-116 Grid/Value pattern.
- Space toggles exclusive preview for the selected slot.
- All loaded slots can sound simultaneously; Low Key / High Key regions may overlap freely.
- This stage intentionally uses one proven TEST2 VoiceBank per slot (16 voices each). The global 96-voice allocator is deferred until this layering stage is validated.
