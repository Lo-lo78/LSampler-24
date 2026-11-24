# LSampler-24 0.99.29 TEST191 — Common cutoff LFO modulation

- LFO 1 BP Depth and LFO 2 BP Depth (after each LFO HP Depth): bipolar -96 to +96 semitones, default zero, step 0.1, coarse step 1. Per-slot automation, presets and bank macro support.
- Each common destination adds the same signed depth to both enabled filters, alongside the existing individual LP and HP depths. The effective depth is calculated at snapshot preparation, so the existing audio-rate LFO path is unchanged. A disabled filter stays disabled; with both enabled and individual depths zero, both cutoffs receive the same pitch-relative modulation, subject to existing filter frequency limits. Individual tracking, envelopes, velocity and other modulation can change their relative separation.
- Unlike the physical BP Cutoff macro, LFO modulation never edits the stored/host LP, HP or BP Cutoff values. No separate filter or common Key Follow is introduced.
- The 176 prior slot parameter IDs, descriptors and previous VST3 host registration batches are preserved; the two new controls are appended after the whole TEST190 host batch.
- Suggested test: enable LP and HP, set LP=.65 and HP=.30, clear individual LFO cutoff depths and set LFO 1 BP Depth=12. Try LFO 2 BP Depth=-12 at a different rate. Confirm the base cutoff values stay unchanged. Disable either filter to check independent enable gating.
- Verification: standalone C++ depth test, structural checks and existing source gates. Added actual render parity coverage to the JUCE sampler tests; Windows compilation and REAPER audition remain necessary.
