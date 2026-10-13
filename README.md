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

## TEST98: Sample Set a tre pagine e caricamenti protetti

Alt+M apre il Sample Set dello Slot corrente. Tre pagine navigabili con F1, F2, F3 oppure Tab / Shift+Tab:
- F1 Samples: le 16 posizioni campione; Invio apre il browser; Space ascolta il campione gia' caricato secondo il motore esistente.
- F2 Sample Parameters: Velocity Low e Velocity High relativi alla posizione selezionata, con Grid e Value (Invio, Alt+V, frecce e step).
- F3 Global Parameters: Variation Mode riferito allo Slot intero, con le cinque voci Off, Round Robin, Random, Random No Repeat, Shuffle No Repeat.

Il browser aperto da F1 riprende la navigazione accessibile di Alt+S ma e' confinato dentro `Library\Samples`. Elenca esclusivamente file audio e cartelle. In questo browser Space fa Play/Stop del file originale tramite il trasporto raw, senza Slice, filtro, inviluppo, pitch o effetti di Slot; Invio lo carica nella posizione Sample Set selezionata. Il browser non modifica lo Slot quando si naviga o si ascolta.

Quando una destinazione e' gia' occupata, i caricamenti Alt+O (file singolo o import pianificato), Alt+S (preset singolo o selezioni multiple), Alt+B (intero banco), Alt+M F1 (Sample Set) e Paste Slot richiedono una conferma modale con Yes (Y), No (N), Esc (annulla). Le operazioni su una posizione libera restano immediate. Le conferme sono risolte prima di avviare i worker asincroni. Per i banchi viene indicato il numero di Slot occupati e il nome del banco selezionato.

Non modificato DSP, ID di automazione, formato preset, logica Shuffle. Non compilato localmente.

## TEST74: Stretch smoothing nel Slice Sequencer
Il percorso overlap-add di Stretch Amount/Frequency ora è condiviso anche da Slice Engine / Alt+E. Il processing resta confinato alla slice corrente e rispetta Reverse; a Stretch Amount 0 resta bypassato.


### TEST149 — Bank Sends indipendenti per Slot

Le griglie dei 24 slot terminano con Bank Chorus Send, Bank Delay Send e Bank Reverb Send (0–100%). Ogni valore è salvato nel proprio file Slot; i parametri dei motori FX Alt+F **non** sono salvati nello Slot. In Alt+K restano i tre Send generali del banco, che si moltiplicano per quelli degli slot. I Bank salvano sia le 72 mandate dei 24 slot, sia gli FX condivisi e i tre Send generali. Le automazioni VST3 degli slot sono aggiunte in fondo agli ID esistenti. I vecchi slot sul master caricano mandate al 100%; quelli indirizzati alle uscite separate hanno 0% per mantenere il precedente routing, ma possono ora inviare agli FX condivisi. Nessuna compilazione viene effettuata durante la preparazione del sorgente.


## TEST151 — Gate Reverb, Delay Sync, Level, Resonance e memoria Alt+F

- **Alt+F / Dual Delay**: Sync Off / 1/32 / 1/24 / 1/16 / 1/12 / 1/8 / 1/6 / 1/4, legato al BPM di REAPER. Off utilizza Delay Time in millisecondi; con Sync attivo le due velocità L/R moltiplicano la durata della divisione. I tempi che superano la capacità del buffer sono limitati alla durata massima del Delay.
- **Delay Tone L/R + Resonance L/R**: ciascuna risonanza è immediatamente dopo il rispettivo Tone, 0–100%, default 0 (suono precedente invariato).
- **Delay Level L/R**: da -inf a 0 dB, default 0 dB, collocati prima del Delay Gain globale. Agiscono soltanto sull'uscita wet del rispettivo delay e non sul feedback.
- **Reverb Gate Threshold**: Off oppure da -79 a 0 dB; Off è il default ed equivale all'algoritmo precedente. **Reverb Gate Decay**: 0–5000 ms, default 300 ms. Apertura immediata, chiusura lineare dopo che il segnale inviato al Reverb scende sotto la soglia; non esiste Attack.
- La posizione corrente di Grid in Alt+F è mantenuta nell'istanza VST3 anche quando REAPER distrugge/ricrea la finestra, ed è conservata anche nello stato del progetto. Non viene inserita in uno Slot.
- Nuovi ID FX sempre aggiunti **dopo** i 30 esistenti; send Slot/Bank, riverbero di default, salvataggio slot e bank rimangono invariati.
- Nessuna compilazione locale: usare GitHub Actions. I controlli statici da soli non dimostrano parità sonora né assenza di problemi di build.
