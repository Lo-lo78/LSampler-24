# LSampler-24

Accessible native JUCE VST3 sampler with 24 additive slots, shared decoded samples and a global 96-voice pool.

The current migration adds the non-Slice per-slot F2 sampler controls from LBPMCaptureSample while keeping the existing LSampler-24 slot, bank, project and keyboard workflow. Slice is reserved for a later dedicated editor.

See [MIGRATION_REPORT.txt](MIGRATION_REPORT.txt) for the full implementation scope, persistence notes, CPU design and deferred legacy systems.

Build with the existing GitHub Actions workflow or `Tools/build-windows.ps1` on Windows.
