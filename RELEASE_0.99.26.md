# LSampler-24 0.99.26 — 8 November 2026

GitHub release tag: `v0.99.26`  
Windows VST3 artifact: `LSampler-24.0.99.26.vst3.zip`

## GitHub Release description (English)

LSampler-24 is an accessible, 24-slot VST3 sampler for Windows, designed for keyboard-only workflows in REAPER with screen readers such as NVDA and OSARA.

### What's new in 0.99.26

- **Paste selected REAPER items with Alt+V.** Import selected timeline items directly into slots, preserving ordinary source trims. Decoding runs in the background with accessible progress and stable keyboard focus.
- **Paste audio files from Windows File Explorer.** Copy one or more files with Ctrl+C and paste into LSampler-24 using Alt+V. Supported audio-only AAC and video files are decoded via Windows Media Foundation where the required codecs are installed; only the first audio stream is used.
- **Multichannel import as separate stereo samples.** Each successive pair of channels occupies its own slot, without a downmix. An unpaired channel becomes mono. A 16-channel file uses eight slots; the next source continues after those slots.
- **Deterministic chromatic mapping.** Consecutive imports continue from the MIDI key mapping already stored in the slots. On occupied slots, Y overwrites, N keeps the previous sample and relocates the incoming sample to the next available slot, and Escape cancels before the load.
- **Manual extraction-cache management.** Advanced (Alt+T) offers Open Extracted Audio Cache Folder and Clear Extracted Audio Cache... with confirmation. Cache cleaning protects files referenced by open LSampler instances and never deletes user source files or Library/Samples.
- **Non-blocking import and accessible feedback.** Background decoding, percentage announcements and focus handling are retained. Free LFO Play-reset behavior and the existing DSP remain unchanged.
- **Updated About and multilingual Help.** Version 0.99.26, release date 8 November 2026, with guidance in English, Italian, Spanish, Portuguese, French, Russian, Chinese and Japanese.

### Important notes

- Supported codecs depend on the Windows installation; not every video format can be decoded.
- Generated audio from video and multichannel sources is stored as persistent WAV files in `%APPDATA%\Lo-lo78\LSampler-24\ExtractedAudio`. **Closed REAPER projects may still reference these files**; save/consolidate the samples or back them up before clearing the cache.
- Audio sources with non-rendered REAPER item effects, fades, stretch markers or speed/pitch changes are not rendered by direct item import. Render these items to a file first when those transformations are required.
- The sampler has 24 slots. Excess stereo pairs are not imported once slots or available MIDI notes are exhausted.

### Installation

Download `LSampler-24.0.99.26.vst3.zip`, extract the VST3 bundle, and install it in your normal Windows VST3 location, usually `C:\Program Files\Common Files\VST3`. Restart or rescan plugins in REAPER.

**Build:** Windows x64 via GitHub Actions. This release is based on tested TEST185 code with version, About, Help and release metadata updated. No further DSP changes.

---

## Note per la pubblicazione (Italiano)

**Tag:** `v0.99.26`  
**Titolo:** `LSampler-24 0.99.26 – Video/AAC, Multi-Channel Audio, Chromatic Paste & Cache Management`

La 0.99.26 raccoglie le funzionalità verificate nelle TEST176–185: importazione item REAPER e file copiati da Esplora file con Alt+V, audio da video/AAC, suddivisione dei multicanale in coppie stereo, progressione cromatica senza ripartenza obbligata da MIDI 60, conferme Y/N per gli Slot occupati, importazione non bloccante e gestione manuale della cache nelle opzioni Avanzate.

L'About è aggiornato alla versione 0.99.26, 8 November 2026, così come la guida integrata in otto lingue. I campioni estratti su disco rimangono necessari ai progetti REAPER chiusi che li referenziano: non svuotare la cache senza prima salvarli o consolidarli.

**Attenzione:** un artefatto prodotto dal workflow GitHub Actions non è automaticamente una GitHub Release. Per pubblicare la release, crea il tag `v0.99.26` su GitHub, allega il VST3 ZIP compilato e incolla la sezione inglese di queste note.
