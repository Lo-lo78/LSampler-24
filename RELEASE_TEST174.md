# LSampler-24 0.99.25 TEST174 — Start/End Threshold on the complete Sample Set

The TEST173 shared Start/End timeline unintentionally overwrote the old threshold window obtained from sample 1. TEST174 evaluates Start Threshold and End Threshold on the combined 16-sample timeline, including their individual Delay offsets, and limits playback accordingly. Start/End Sample percentages remain the outer boundaries. End threshold uses the existing continuous 65-ms silence criterion after detecting audio activity, with a 1-ms peak envelope for the combined timeline. Changes are calculated when preparing an immutable audio state, not in the realtime renderer. Layer and Alt+S preview both use the shared threshold window.

To validate: test a single sample with leading/trailing silence; then two Layer samples with nonzero second Delay; compare End Threshold with End Sample set to 100%; finally run the audio parity comparison in GitHub Actions.
