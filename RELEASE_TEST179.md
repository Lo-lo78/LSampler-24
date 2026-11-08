# LSampler-24 TEST179 — Windows Explorer audio clipboard

- Ctrl+C in Windows Explorer on one or multiple audio files, then Alt+V in the LSampler slot grid imports the files into successive slots.
- Native CF_HDROP is read before selected REAPER items, avoiding stale project selections. Latest internal Alt+C/Alt+X keeps its existing priority until the Windows clipboard changes.
- One file per MIDI note, 60/C4 upwards, with low/high/root all set to that note, matching REAPER item import.
- The existing background loader, progress indicator, accessible NVDA announcements, overwriting confirmation and keyboard focus preservation are reused.
- Windows only. Unsupported files are skipped; destination capacity is at most 24 slots.
