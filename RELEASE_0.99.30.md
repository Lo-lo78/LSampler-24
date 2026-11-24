# LSampler-24 0.99.30 — Linked Filter Cutoff and LFO Modulation

**Release date:** 24 November 2026  
**Previous public release:** 0.99.29  
**Source:** 0.99.29 TEST191 (includes TEST190)  
**Platform:** Windows x64 VST3

## New controls per Slot

- **HP Key Follow** (after HP Velocity): -1.000 to +1.000, step 0.001, default 0. Independent high-pass keyboard tracking relative to Original Pitch, matching LP Key Follow.
- **BP Cutoff** (after HP Cutoff): 0–1, step 0.001. Physical linked edit of the existing LP and HP cutoff values, not a third DSP filter. Reads their midpoint and shifts both equally on the original logarithmic scale, preserving the frequency ratio. Both stop at the first boundary. Editing either cutoff independently recalibrates the midpoint. Both filters must be enabled for a band-pass effect; their individual cutoff, resonance and modulation controls remain independent. No common Key Follow is introduced.
- **LFO 1 BP Depth** and **LFO 2 BP Depth** (after the respective HP Depth): -96 to +96 semitones, step 0.1, default 0. Each adds equal signed modulation to both enabled filters alongside individual LP/HP modulation. Unlike BP Cutoff, LFO modulation never edits stored cutoff values. Frequency limits and individual modulation can change band width.

## Example

Set LP Cutoff to 0.600 and HP Cutoff to 0.300. BP Cutoff reads 0.450; move it to 0.550 and the original cutoffs become 0.700 and 0.400. Use equal positive LP/HP Key Follow to track the keyboard, or different values for independent motion. With individual LFO cutoff depths at zero, try LFO 1 BP Depth = 12 semitones, or add LFO 2 at a different rate.

## Accessibility and compatibility

- All four controls support Grid/Value, per-Slot REAPER automation (Ctrl+Shift+A) and presets. HP Key Follow and both BP Depth controls also support Bank Macro (Alt+K); BP Cutoff does not.
- Avoid simultaneously automating BP Cutoff and its two dependent individual cutoff controls.
- Existing parameter IDs and host registration order are preserved; 178 slot catalog parameters. Old presets retain their sound: new tracking/depths default to zero and BP Cutoff is derived from the stored cutoff pair.
- DSP is unchanged from TEST191, which was confirmed working by the developer in REAPER. This release preparation updates About, eight-language Help, README, version and Windows artifact metadata only. The new release binary still requires GitHub Actions compilation and final verification.
- Windows artifact: `LSampler-24.0.99.30.vst3.zip`.

Technical details: `RELEASE_TEST190.md`, `RELEASE_TEST191.md`.
