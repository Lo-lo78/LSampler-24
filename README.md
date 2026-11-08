# LSampler-24 — Versione 0.99.26

**Release date: 8 November 2026 · Windows x64 VST3 · REAPER / OSARA / NVDA**

Questa è la release pubblica successiva alla 0.99.25, basata sul sorgente TEST185
provato in REAPER. Nessuna modifica al DSP o alla logica del caricamento rispetto
alla TEST185: aggiornati versione, About, Help nelle 8 lingue e documentazione
per GitHub. Leggi `RELEASE_0.99.26.md` per la descrizione completa.

## Novità principali della 0.99.26

- **Alt+V da REAPER:** importa direttamente gli item audio selezionati dalla timeline,
  rispettando i tagli semplici e senza doverli prima copiare negli appunti.
- **Alt+V da Esplora file Windows:** importa i file audio copiati con Ctrl+C, inclusi
  i file AAC e l'audio della prima traccia dei video supportati dai decoder Windows.
- **Audio multicanale:** divide i canali in coppie stereo consecutive senza mixdown
  (16 canali = 8 Slot); il canale dispari finale resta mono. I campioni del file
  successivo continuano dagli Slot rimasti disponibili.
- **Mappatura cromatica deterministica:** prosegue dalle zone/note MIDI già assegnate
  agli Slot invece di tornare sempre a 60. Sugli Slot occupati Y sovrascrive, N
  conserva e sposta lo stesso campione al prossimo Slot libero; Escape annulla.
- **Importazione non bloccante:** elaborazione sul file worker, avanzamento NVDA,
  focus conservato e riapertura veloce delle finestre REAPER.
- **Alt+T / Advanced:** Open Extracted Audio Cache Folder e Clear Extracted Audio
  Cache... (conferma). La pulizia evita i WAV utilizzati dalle istanze aperte,
  ma i progetti REAPER chiusi potrebbero ancora richiamarli. Conserva i campioni
  necessari prima di svuotare la cache. Library/Samples e i file originali
  non vengono eliminati.

**Nota sulla persistenza:** i normali sorgenti audio restano ai percorsi originali;
le estrazioni da video e multicanale producono WAV persistenti in
`%APPDATA%\Lo-lo78\LSampler-24\ExtractedAudio`. Per un banco consolidato
usa Save Slot / Save Bank; per trasferire progetti che usano WAV estratti, porta
con te anche tali WAV fino al consolidamento.

## Cronologia tecnica delle versioni TEST

## TEST185 — controllo UTF-8 Windows / completamento GitHub Actions

La TEST184 ha compilato il VST3 con MSVC e superato i test audio/C++, ma il workflow si è fermato in `verify-test183-cache-cleanup.py` per un `UnicodeDecodeError` causato dalla codifica predefinita cp1252 su Windows. La TEST185 corregge le sei letture usando UTF-8, imposta `PYTHONUTF8=1` per i test in CI, e verifica tutto prima della compilazione. Nessuna modifica al plugin rispetto alla TEST184. Vedi `RELEASE_TEST185.md`.

## TEST184 — correzione compilazione Windows (MediaPasteExpansion)

Corretto il C++ in `Source/MediaPasteExpansion.cpp` (precedente riga 487): la
`std::unique_ptr<juce::AudioFormatReader>` viene dichiarata prima dell'`if`,
invece di utilizzare la sintassi non valida di una dichiarazione con
inizializzazione tra parentesi nella condizione. Elimina gli errori MSVC
`C4430` e `C2059` segnalati su GitHub Actions. Nessuna modifica al flusso di
Alt+V, all'estrazione video/multicanale, né ad Advanced/Svuota cache.

Vedi `RELEASE_TEST184.md`.

## TEST183 — gestione manuale cache estrazione in Advanced

Alt+T > Advanced > Extracted Audio Cache: **Open Extracted Audio Cache Folder**
oppure **Clear Extracted Audio Cache...**. Nessuna pulizia durante il normale
salvataggio. La conferma Y/N avverte dei progetti REAPER chiusi: possono ancora
richiamare WAV estratti dalla TEST182. La pulizia esclude sempre audio originale,
Library/Samples e file attualmente referenziati da istanze aperte di LSampler.
Per dettagli e limiti vedi `RELEASE_TEST183.md`.

## TEST182 — incolla da video/AAC e multicanale (Alt+V)

In REAPER usa Alt+V dopo la selezione di item o dopo Ctrl+C su file in Esplora file.
Audio mono/stereo rimane invariato. Audio multicanale e la prima traccia audio dei
video supportati da Windows vengono separati in coppie stereo e distribuiti tra
gli slot disponibili. Esempio: due sorgenti da 16 canali occupano 8 + 8 slot.
Conferme Y/N/Escape, note cromatiche e caricamento non bloccante restano quelli
della TEST181. I WAV derivati sono persistenti in AppData/Roaming/Lo-lo78/
LSampler-24/ExtractedAudio, per consentire la riapertura dei progetti.
Per dettagli e limiti di codec vedi RELEASE_TEST182.md.

## TEST178 (7 novembre 2026) — Importazione REAPER non bloccante e focus NVDA

Alt+V usa sempre gli item selezionati. Il calcolo delle soglie e degli stati DSP dello slot in arrivo avviene ora sul worker prima del lock; una cache per slot evita di riscandire tutte le sorgenti già caricate a ogni item. Durante l'importazione il focus resta sulla griglia anziché essere trasferito a Status, pur mantenendo l'annuncio accessibile della percentuale. Vedi `RELEASE_TEST178.md`. Verifica runtime in REAPER/NVDA e build Windows ancora necessarie.

## TEST177 (7 novembre 2026) — Importazione in background, avanzamento e chiusura rapida

Gli item selezionati di REAPER si importano sempre con Alt+V, senza Ctrl+C o SWS. La decodifica e la creazione dei tagli avvengono sul worker file con percentuale accessibile; gli item consecutivi dello stesso file condividono temporaneamente la decodifica. Per ogni taglio il progetto conserva unicamente percorso e frame iniziale/finale invece di ricodificare i minuti di audio nello stato VST3. Gli snapshot host evitano anche lo SHA-256 sincrono. Questo elimina le principali operazioni pesanti che potevano bloccare Esc/ritorno al TCP. Prova in REAPER necessaria; per convertire stati di clip importati con TEST176 reimportare gli item. Vedi `RELEASE_TEST177.md`.

## TEST176 (7 novembre 2026) — Importazione diretta degli item selezionati REAPER

Seleziona uno o più item audio nella timeline di REAPER. Apri LSampler-24, seleziona lo slot iniziale nella griglia degli slot e premi **Alt+V**. Non occorrono Ctrl+C, SWS o Lua. Il plug-in usa le API VST3 ufficiali di REAPER per recuperare il file sorgente, l'offset iniziale e la durata di ogni take attivo. Gli item sono disposti per posizione crescente nella timeline; anche più item ricavati dallo stesso file ottengono i propri tagli audio. Nota storica TEST176: inizialmente assegnava **Low Key = High Key = Original Pitch** partendo da MIDI 60 a ogni incolla. **Nella release 0.99.26** la mappatura continua invece dalle note esistenti ed evita le sovrapposizioni; ogni Slot occupato chiede conferma Y/N. L'importazione SWS via clipboard e la copia-incolla di slot restano alternative.

**Limiti dichiarati:** i tagli semplici sono precisi; non vengono renderizzati né riprodotti le trasformazioni di un item (stretch marker, riproduzione al contrario, variazioni di velocità/intonazione, media section o loop oltre la sorgente). Questi item vengono saltati e conteggiati, senza importazioni musicalmente sbagliate. Fade, gain, FX di item/take e inviluppi non sono stampati nell'audio importato. Per tali casi renderizzare il take in un file prima di importarlo. La build VST3 Windows deve essere verificata con GitHub Actions e REAPER/OSARA.

## TEST175 (7 novembre 2026)

Apprendimento MIDI immediato di Low Key/High Key nella griglia dello slot; importazione REAPER/SWS tramite Alt+V mappata automaticamente su singoli semitoni consecutivi, da MIDI 60 in avanti. Il resto delle importazioni non cambia. Leggi `RELEASE_TEST175.md`.

## TEST173 (6 novembre 2026)

Alt+M F2: automazione per Pitch, Octave, Pan, Volume e Delay di ogni campione. Nuovi Octave (-4/+4) e Delay (0–1000 ms). Start/End dello slot definiti sulla durata complessiva del Sample Set, includendo i ritardi. Dettagli in `RELEASE_TEST173.md`.

## Archivio TEST171 e versioni precedenti

**Nota storica: versione di prova 0.99.25 — 6 novembre 2026 (TEST171)**

## TEST171 — Sample Set: Pitch, Pan, Volume, Layer e scorciatoie Rack

- Alt+M, F2 Sample Parameters: ogni campione dei 16 dispone di Velocity Low/High, Pitch (cents), Pan e Volume (dB). Solo Pitch/Pan/Volume sono indipendenti per campione; il resto del motore resta condiviso a livello di Slot. I valori predefiniti sono 0 e i preset precedenti si aprono senza cambiare suono.
- Alt+M, F3 Variation Mode: nuova modalità Layer che sovrappone i campioni appartenenti alla stessa zona velocity in modalità Poly. Le modalità Off, Round Robin, Random e Shuffle rimangono disponibili.
- Griglia principale: + e - regolano Level dello slot selezionato di 1 dB; Pagina Su/Giù si spostano di 4 righe, senza uscire dalla colonna di 8.
- Limiti: Layer richiede Voice Mode Poly, aumenta l'uso di voci e CPU; i controlli Pitch/Pan/Volume per campione sono memorizzati nel preset, ma in questa TEST non hanno parametri di automazione host separati.
- Versione sorgente sperimentale: verificare build GitHub Actions e test audio in REAPER.

## TEST170 — LFO Free deterministici al Play della DAW

LFO 1 e LFO 2 con Trigger Off (Free) ripartono dalla fase iniziale
quando REAPER o un'altra DAW passa a Play. Anche gli stati Mono condivisi,
Poly per voce e Sample & Hold ripartono da semi definiti; Smooth, Delay
e One Shot vengono reinizializzati solo nel modo Free. Triggered non
cambia, gli altri moduli DSP e i campioni non vengono azzerati. Nessun
nuovo parametro o modifica ai preset. Il rilevamento avviene prima
anche dell'uscita CPU idle. Vedi `TEST170_FREE_LFO_HOST_PLAY_NOTE_IT.txt`.


## TEST169 — Incolla i percorsi dei campioni da SWS/BR con Alt+V (senza Lua)

In REAPER seleziona gli item audio e usa l’azione **SWS/BR: Copy take media source file path of selected items to clipboard** (`_BR_TSOURCE_PATH_TO_CLIPBOARD`). Poi scegli uno slot in LSampler-24 e premi **Alt+V**. Il plugin legge l’elenco di percorsi (uno per riga) direttamente dagli appunti Windows, importa i **file sorgente completi** in sequenza fino al 24 e chiede conferma per gli slot occupati. Accetta percorsi con spazi, lettere accentate e più item. Lo script Lua non serve. Alt+C/Alt+X/Alt+V per gli slot funzionano ancora; le altre DAW restano invariate.

**Attenzione:** l’azione REAPER Ctrl+C (40057) copia item, mentre l’azione SWS/BR copia percorsi. Questa è la via affidabile per ottenere i file audio; se un item è tagliato, il file importato rimane intero. Per FX, fade, stretch o pitch applicati agli item, prima renderizzali. Vedi `TEST169_SWS_COPY_SOURCE_PATHS_NOTE_IT.txt`. Il lettore del formato nativo di Ctrl+C della TEST168 rimane sperimentale.

## TEST168 — Copia item REAPER con Ctrl+C e incolla negli slot con Alt+V

Nota storica: la TEST168 ha introdotto un lettore sperimentale degli item copiati con Ctrl+C originale (40057). Il formato nativo di REAPER non è stato confermato: **non usare questo metodo come percorso affidabile di importazione**. Nella TEST169, per caricare i campioni, utilizza invece l'azione SWS/BR per copiare i percorsi dei file, poi Alt+V. Il codice sperimentale della TEST168 resta presente solo come fallback.

## TEST166 — Raccordo anti-click e limiti incrociati Sample Start / End

- In Mono, No Retrigger Smooth e Poly con sostituzione delle voci: la voce uscente viene mantenuta per un crossfade smoothstep di **3 ms** attraverso il normale motore audio, senza consumare una voce musicale o cambiare gli ID VST3. La voce nuova entra con fade complementare. Le code sono preallocate, senza memoria dinamica nel callback audio. Gli attacchi senza sostituzione e il Main Loop rimangono invariati.
- Con **Legato** attivo e senza retrigger, in assenza di portamento, solo il cambio d'intonazione usa un brevissimo raccordo di **1 ms** sul passo di lettura, senza riavviare il campione o l'ADSR. Il portamento configurato dall'utente resta invariato.
- Nella pagina **Sample Window**, **Sample End** non può scendere sotto **Sample Start**; **Sample Start** non può superare **Sample End**. Funziona con frecce, editor Value e immissione numerica, con valore di ritorno coerente per NVDA. Il campione PCM non viene tagliato o riscritto.
- Nessun nuovo parametro, modifica a Bank/Slot preset o cambiamento al formato del progetto. TEST166 aggiunge regressioni nel gate nativo di GitHub Actions e conserva tutti i controlli TEST59/TEST62 e TEST163–165.


**Versione di prova 0.99.24 — 4 November 2026 (TEST165, correzione Windows)**

## TEST165 — Ricerca in stile LReaParams e correzione compilazione Windows

TEST165 riproduce la ricerca progressiva di LReaParams 0.0.63 (prefissi,
parole interne, avanti/indietro con Shift, numeri e Unicode). Questa
revisione corregge gli errori MSVC C2589 dovuti alle macro Win32 min/max
importate da `IncrementalParameterSearch.h`, senza cambiare suono, salvataggi,
automazioni, guida o About. Il test nativo non imposta più artificialmente
`NOMINMAX`: verifica che l'header protegga da sé ogni unità C++ del plugin.

## TEST164 — Ricerca incrementale dei parametri da tastiera

In Grid basta scrivere più lettere consecutive per restringere la ricerca per prefisso del nome accessibile, ad esempio `m` poi `i` per trovare **MIDI Channel** (se esiste una corrispondenza). Un singolo tasto ripetuto scorre le corrispondenze in ordine, **Shift+lettera** cerca all'indietro. **Shift+numero** cerca all'indietro utilizzando il numero reale e non il simbolo prodotto dalla tastiera (`Shift+1` cerca `1`, non `!`). Il tasto `0` mantiene la scorciatoia per **Zero Crossing**. Le ricerche sono cicliche nell'ambito della vista corrente; una pausa di 1,2 secondi, una freccia, un cambio di pagina o di focus iniziano una nuova sequenza. La ricerca si applica a Grid Slot/Global, Bank Macro, Slice Edit, FX e alle due voci della pagina Sample Set F2; non modifica il Value editor né i browser. La selezione continua a usare i nomi pronunciati da NVDA. Nessun cambiamento a DSP, ID VST3, automazioni o formati dei preset.

GitHub Actions verifica la ricerca incrementale e i test precedenti e mantiene l'esistente gate di parità audio TEST62, prima di pubblicare `LSampler-24-Windows-VST3-TEST165`.



## TEST163 — Canale MIDI indipendente per Slot

La pagina **Slot** presenta **MIDI Channel** dopo **Slot Polyphony**: **Omni** (default) o **1–16**. I 24 slot possono ricevere ciascuno un canale diverso o condividere lo stesso canale per ottenere layer. Il filtro MIDI vale per tastiera normale e Slice MIDI Map; tutte le zone di nota/velocity esistenti sono rispettate. Note Off, CC sustain e pitch bend usano il canale MIDI originale, senza nuovi motori audio. I preset precedenti senza `midi_channel` mantengono il default Omni. Il valore è serializzato nei preset Slot/Bank e nei progetti e pubblicato come parametro VST3 autonomo per ciascuno slot. I 24 nuovi ID sono APPESI DOPO tutti i parametri TEST161, preservando gli indici precedenti. MIDI Channel non è un Bank Macro.

Il workflow GitHub Actions rimane sul test di parità audio TEST62 e verifica anche l'invarianza della TEST59 e la coerenza TEST163. I nuovi test funzionali di routing sono disponibili nella suite opzionale `LSAMPLER_BUILD_TESTS`.


La guida accessibile (Alt+H) è disponibile in otto lingue e documenta gli LFO 1/2 aggiornati delle TEST160/161: One Shot Percent, Phase, Env Rate, Square PWM, Upper/Lower Squash, Cross Rate e Voice Mode Mono/Poly indipendente per Slot. L’About (Alt+A) mostra versione e data aggiornate; il testo è selezionabile con Ctrl+A e copiabile con Ctrl+C. Non sono stati modificati il motore DSP o i controlli rispetto alla TEST161.


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


## TEST181 – Alt+V, conferma per slot occupato

Per item REAPER e file audio da Esplora file, Y sovrascrive lo slot richiesto; N mantiene lo slot e sposta lo stesso campione al primo slot libero successivo, continuando con la prima nota MIDI libera. Escape annulla l’intero incolla prima di avviare l’importazione in background. Dettagli in `RELEASE_TEST181.md`.
