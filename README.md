# LSampler-24

Accessible native JUCE VST3 sampler with 24 additive slots, shared decoded samples and a global 96-voice pool.

The current migration adds the non-Slice per-slot F2 sampler controls from LBPMCaptureSample while keeping the existing LSampler-24 slot, bank, project and keyboard workflow. Slice is reserved for a later dedicated editor.

See [MIGRATION_REPORT.txt](MIGRATION_REPORT.txt) for the full implementation scope, persistence notes, CPU design and deferred legacy systems.

Build with the existing GitHub Actions workflow or `Tools/build-windows.ps1` on Windows.

TEST15 dynamic library:
- Alt+O folder entries no longer add the word Folder.
- Alt+S keeps Folder for directories and supports Ctrl+C/Ctrl+X/Ctrl+V inside Library\Slots.
- Slot recipes store SHA-256 sample identity and recover moved audio anywhere below Library\Samples.
- Old recipes self-upgrade when successfully loaded/previewed.
- Browser accessibility line metadata is cleaned so NVDA current-line reading does not duplicate the entry.
