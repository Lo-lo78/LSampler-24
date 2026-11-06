# LSampler-24 0.99.25 TEST172 — Alt+S slot preview fidelity

The Alt+S saved-slot browser now prepares the entire Sample Set (up to 16 samples) rather than auditioning only the first sample. The preview snapshot carries per-sample Velocity Low/High, Pitch, Pan and Volume plus the saved Variation Mode. In Layer mode, the preview renders every sample eligible at the audition velocity (127), including independently panned/tuned samples; the global 96-voice budget and existing main-slot DSP stay unchanged.

Loading all samples is performed off the audio thread. Edited primary audio saved in the preset is also restored for preview. A missing referenced alternate sample produces an error rather than an incorrectly incomplete slot preview.

Note: Alt+O raw-file previews are still unprocessed, intentionally. This change affects Alt+S saved-slot presets.

Static source checks were run locally. Full JUCE/VST3 Windows compilation and audio audition must be verified by GitHub Actions and in REAPER.
