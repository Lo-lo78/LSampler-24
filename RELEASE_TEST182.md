# LSampler-24 v0.99.25 TEST182 — audio dai video e audio multicanale su Alt+V

Basata integralmente sulla TEST181, inclusi dialoghi Y/N/Escape, mappatura MIDI deterministica, REAPER Items, Esplora file e progress NVDA.

## Comportamento

- Alt+V identifica file **AAC e video con audio** (ad esempio MP4, M4V, MOV, WMV e altri contenitori decodificabili da Windows Media Foundation). L'importazione usa la **prima traccia audio** del video: il video non viene importato o elaborato.
- Audio mono o stereo supportato nativamente da JUCE: file originale invariato e un solo slot (come TEST181).
- File audio multicanale WAV, AIFF, FLAC ecc. riconosciuti da JUCE: una traccia ogni coppia consecutiva di canali, senza mixdown. File 8 canali -> 4 coppie stereo e 4 slot. File 16 canali -> 8 coppie e 8 slot. Con numero dispari, l'ultimo canale viene importato in mono.
- File video / AAC non gestito da JUCE: decoder Media Foundation di Windows, produzione di un WAV PCM 24 bit per ciascuna coppia stereo; l'eventuale ultimo canale dispari resta mono. Dipende dai codec disponibili su Windows 11; non tutti i video sono supportati.
- Per tutte le sorgenti, ogni coppia conta come un nuovo campione nell'algoritmo Alt+V TEST181. La seconda sorgente parte dopo l'ultimo slot impiegato dalla prima (non semplicemente dal secondo slot). Oltre ai 24 slot e alle note MIDI ancora disponibili, l'importazione si ferma e segnala i campioni non caricati.
- Uno slot occupato continua a richiedere **Sì** per sovrascrivere, **No** per conservare e trasferire la stessa coppia stereo nel prossimo slot libero, **Escape** per annullare la pianificazione. I file originali non vengono cancellati.
- Il pretrattamento e l'estrazione avvengono su **file worker**, a blocchi (senza caricare tutto il multicanale nella RAM). In seguito il normale worker TEST178 carica i WAV estratti, mantenendo il focus e annunciando la progressione NVDA.

## Importante: persistenza dei WAV estratti

Per consentire il richiamo dopo la chiusura di REAPER, i WAV derivati non sono temporanei: sono salvati nella cartella dati utente di Windows `AppData/Roaming/Lo-lo78/LSampler-24/ExtractedAudio`, con identificativo deterministico basato su sorgente, dimensioni e data di modifica. Al successivo Alt+V le copie ancora valide vengono riutilizzate. Non eliminare questa cartella se i progetti vi fanno riferimento. Come per i normali file campione, per trasferire un progetto a un altro computer occorre portare con sé questi WAV o consolidarli nel progetto.

## Test

- `Tests/MediaPasteGroupingTests.cpp` verifica conteggio coppie 1/2/3/8/16 canali, continuità su due file da 16 canali (secondo file inizia allo slot 9), No su slot occupato e limiti di capienza.
- `Tools/verify-test182-media-expansion.py` verifica integrazione media/video, pipeline a due fasi, persistenza dei WAV, linking Media Foundation, progress NVDA e GitHub Actions.
- La build Windows e i decoder Media Foundation vanno verificati su GitHub Actions e con file reali in REAPER; i test locali non equivalgono a una prova VST3 Windows.
