# LSampler-24

Experimental accessible 24-slot sampler for REAPER/VST3.

## TEST2 scope

TEST2 consolidates the persistent file model before the GitHub repository is created.
The audio material and the sampler configuration are deliberately separate.

### Library layout

The root is resolved from the current user's Documents folder, never from a hard-coded user name:

- `Documents/LSampler-24/Library/Samples`
- `Documents/LSampler-24/Library/Slots`
- `Documents/LSampler-24/Library/Banks`

### File roles

- **Audio sample**: the raw source material. It keeps its real audio extension (`.wav`, `.aif`, `.aiff`, `.flac`, `.ogg`).
- **Slot preset**: `.lsampler-24-s`. It contains the complete configuration of one slot and a reference to its audio sample. It does not embed or rename the audio.
- **Bank preset**: `.lsampler-24-b`. It is a snapshot of 24 slots plus, later, bank/global properties. It does not embed 24 copies of the audio.

The same `Kick.wav` can therefore be used by any number of slot presets and bank presets. The sound is determined by the slot/bank parameters, while the WAV remains the shared raw material.

### Load / Save rules

- **Load Sample** may open an audio file from any disk/folder. Until the slot/bank is saved, the running project may reference that external absolute path.
- **Save Slot** materialises an external sample into `Library/Samples`, switches the live slot to that Library copy, and writes a `.lsampler-24-s` file into `Library/Slots`.
- **Load Slot** reads `.lsampler-24-s` files from `Library/Slots` and restores the slot configuration.
- **Save Bank** materialises any external audio used by the bank and writes a `.lsampler-24-b` file into `Library/Banks`.
- **Load Bank** reads `.lsampler-24-b` files from `Library/Banks`.

Saved Library presets use a path relative to `Documents/LSampler-24`, e.g. `Library/Samples/Kick.wav`, rather than embedding a user-specific absolute Documents path.

### Bank contract

TEST2 still has only one playable slot, but `.lsampler-24-b` already stores **24 explicit Slot children**. Slot 1 contains the current TEST2 state and slots 2..24 are empty placeholders. This fixes the bank-file contract now, before the engine grows to 24 playable slots.

### Current audio engine

- Windows x64 VST3 using JUCE 8.0.15 and CMake.
- One playable slot for TEST2.
- MIDI sample playback with 16 voices.
- MIDI 60 default root note with chromatic pitch tracking.
- Volume.
- Load Sample from any disk/folder.
- Shared process-wide SamplePool keyed by source path.
- REAPER project-state restore without Lua or a startup loader.
- Missing samples fail safely and report `Sample missing`.

Not implemented yet:

- 24 simultaneously playable slots.
- Internal accessible Library browser.
- Sample Start/End, filters, envelopes, LFOs, loops, Slice Sequencer, routing and the other LBPMCaptureSample engine features.
- Advanced Library management beyond the current SHA-256 content deduplication used when saving audio.

## Build

Push to `main` on GitHub. The included workflow builds the VST3 and uploads `LSampler-24-Windows-VST3.zip` as an Actions artifact.

## Build fix 1

- Replaced the unavailable generated `JuceHeader.h` include with JUCE module headers, matching the LR-608 CMake style.
- Linked `juce_cryptography` explicitly because TEST2 uses `juce::SHA256` to deduplicate Library samples.
