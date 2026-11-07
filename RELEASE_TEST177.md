# LSampler-24 0.99.25 TEST177 — Importazione REAPER asincrona e chiusura rapida

7 novembre 2026 · Sorgente completo per GitHub Actions Windows x64 VST3.

## Motivo

Nella TEST176 l'importazione selezionati tramite Alt+V usava già un file worker, ma aggiornava la percentuale solo tra un campione e il successivo. Soprattutto, gli item semplicemente tagliati venivano salvati come PCM float32 codificato Base64 dentro lo stato VST3. Con registrazioni di sei minuti lo stato del progetto poteva richiedere molti megabyte e bloccare il thread UI di REAPER quando la finestra veniva chiusa con Esc o quando il progetto salvava uno snapshot.

## TEST177

- L'importazione diretta con Alt+V rimane nel file worker dedicato: nessun decode/file I/O sul thread audio o su quello della GUI.
- Percentuale NVDA progressiva durante lettura del WAV, copia delle porzioni e calcolo di picco/DC; aggiornamento del messaggio accessibile ogni quattro secondi (come per le altre operazioni file).
- Ogni serie consecutiva di item che proviene dallo stesso audio mantiene temporaneamente una sorgente condivisa nel worker: un WAV di sei minuti viene decodificato una sola volta per quella serie. Gli slot contengono solo i rispettivi tagli; la sorgente intera viene liberata quando non serve più.
- Nuovo nodo di stato `SourceWindow` con frame iniziale e finale del file originale, senza audio duplicato negli snapshot di progetto. Il reload ricrea la porzione esatta dal file, preservando taglio, Sample Start/End relativi, nome, nota, parametri e Sample Set.
- Gli snapshot del progetto/undo VST3 non calcolano più SHA-256 dei file sorgente sul thread UI. Salvataggi di Slot/Bank continuano a usare gli hash e a materializzare i WAV tagliati come prima.
- Il taglio distruttivo successivo invalida la finestra di origine e usa il precedente formato di audio editato, senza rischiare di salvare audio sbagliato.
- Low Key, High Key e Original Pitch rimangono 60/60/60, 61/61/61, 62/62/62, ecc. Le importazioni non-REAPER non cambiano.

## Compatibilità, limiti e verifica

Il nodo SourceWindow salva un **riferimento al file originale**. Se l'originale viene spostato o alterato, il ripristino richiede il file corretto. Per creare uno Slot autonomo salvare il preset Slot o Bank: il salvataggio produce nella Libreria un nuovo WAV dei tagli. Gli snapshot del progetto contengono riferimenti senza SHA per evitare blocchi nel thread del host; i preset di Libreria mantengono la verifica hash.

Gli item importati con la TEST176 e già salvati come `EditedAudio` conservano il precedente stato, anche se pesante. Per usufruire dell'ottimizzazione nel progetto esistente è necessario **reimportare** gli item da REAPER con TEST177. I tagli fatti con Zero Crossing rimangono editing PCM e sono ancora serializzati in modo legacy, come nelle versioni precedenti. I limiti della TEST176 sugli item con stretch/reverse/playrate/pitch si applicano invariati.

Verifiche Python statiche delle regressioni e test nativo sulla geometria dei tagli; compilazione Windows e test reali in REAPER/OSARA/NVDA da effettuare con GitHub Actions e utente.
