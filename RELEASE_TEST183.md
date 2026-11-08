# LSampler-24 v0.99.25 TEST183 — cache estrazioni audio gestita da Avanzate

Basata sulla TEST182 e su tutta la logica TEST181: Alt+V, importazione video/AAC,
separazione stereo multicanale, nota cromatica, Y/N/Escape, import worker e NVDA
rimangono invariati.

## Alt+T / Advanced > Extracted Audio Cache

- **Open Extracted Audio Cache Folder** apre la directory in Esplora file,
  se già esiste. La cartella è `%APPDATA%\Lo-lo78\LSampler-24\ExtractedAudio`.
- **Clear Extracted Audio Cache...** chiede conferma accessibile con Y / N,
  spiegando che progetti REAPER **chiusi** potrebbero ancora dipendere dai WAV derivati.
  Scegliendo N non viene eliminato alcun file.
- Scegliendo Y, l'eliminazione si esegue sul file worker (mai nel callback audio)
  e restituisce numero di file eliminati, MB liberati, file protetti e errori.
- Si possono eliminare soltanto i file WAV con il nome creato dalla conversione
  della TEST182 e gli eventuali residui `.partial-...`. Non sono toccati i
  file originali, le cartelle personali o `Library/Samples`.
- I WAV ancora referenziati dagli slot 1–24, dai Sample Set (fino a 16 per slot)
  o dal copia-slot interno delle **istanze attualmente aperte di LSampler-24**
  vengono preservati. Anche estrazioni contemporanee sono protette da lock
  shared/exclusive; non avviene alcun tentativo di cancellazione automatica.

## Precauzione fondamentale

Non è possibile scoprire con certezza tutti i progetti REAPER **chiusi** che
possono usare i WAV in cache. Prima di cancellare, consolidare i campioni
necessari con Save Slot / Save Bank nella libreria o conservare una copia di
`ExtractedAudio`. Premere Y significa accettare il rischio per quei progetti.

## Test e build

- Tests/ExtractedCacheFilenameTests.cpp e verify-test183-cache-cleanup.py
- Build e test su Windows via GitHub Actions; nessuna compilazione VST3
  Windows eseguita durante la preparazione dell'archivio.
