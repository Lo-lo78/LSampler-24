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

## TEST31: Slice Engine, Slice Edit e Slice Sequencer

Integrazione sorgente basata su TEST30, non compilata in questa consegna.
Alt+E apre Slice Edit sul campione già caricato; F6 apre il sequencer.
Tab passa tra confini, globali e step. F1 annuncia l'aiuto dell'editor.
Alt+O resta il browser di importazione esistente.

Leggere `SLICE_TEST31_NOTE_IT.txt` per strutture dati, shortcut, persistenza,
DSP, differenze dal riferimento JSFX e collaudo Windows/REAPER/NVDA.
Il workflow di build è invariato; i test Slice sono nel target opzionale.


TEST52
- Properties Ctrl+Up/Down now uses silent physical boundaries: skips empty slots only in the requested direction and never wraps from 24 to 1 or 1 to 24.

## TEST62: Sample Set / Velocity / Round Robin

Ogni Slot puo' ora contenere fino a 16 campioni. Alt+M apre il Sample Set Editor dello Slot corrente; al suo interno Alt+S riusa il browser campioni esistente per caricare il file nella posizione selezionata. Ogni posizione ha Velocity Low/High e lo Slot offre Variation Mode: Off, Round Robin, Random, Random No Repeat e Shuffle No Repeat (un ciclo casuale che utilizza tutte le varianti eleggibili, senza ripetizioni tra cicli).

Il DSP resta condiviso dallo Slot e per ogni Note On viene scelta una sola sorgente. I vecchi Slot a campione singolo restano compatibili come Sample 1 con Variation Mode Off. Slice Engine/Edit/Sequencer restano legati a Sample 1 in questa prova. Vedere `SAMPLE_SET_TEST62_NOTE_IT.txt`.

## TEST74: Stretch smoothing nel Slice Sequencer
Il percorso overlap-add di Stretch Amount/Frequency ora è condiviso anche da Slice Engine / Alt+E. Il processing resta confinato alla slice corrente e rispetta Reverse; a Stretch Amount 0 resta bypassato.
