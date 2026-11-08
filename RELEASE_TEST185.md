# LSampler-24 0.99.25 TEST185 — correzione verifica UTF-8 su Windows

## Problema confermato dal log GitHub Actions TEST184 (8 novembre 2026)

La TEST184 ha **compilato il VST3 MSVC correttamente**, ha completato il confronto audio TEST59/TEST62 e i test C++ (free LFO, clipboard, chromatic paste, media grouping, cache cleanup). Il workflow è tuttavia fallito al controllo Python successivo:

`Tools/verify-test183-cache-cleanup.py` leggeva sei file sorgente con `Path.read_text()` senza `encoding`. Su Windows, con locale `cp1252`, la lettura di `Source/PluginEditor.cpp` falliva con `UnicodeDecodeError: 'charmap' codec can't decode byte 0x81 ...` prima che potesse essere creato l'archivio VST3.

## Correzione

- Tutte le sei letture del controllo TEST183 ora usano `read_text(encoding='utf-8')`.
- La pipeline Windows dichiara `PYTHONUTF8=1` come protezione aggiuntiva per i test storici.
- Nuovo controllo TEST185 posto **prima della build**, che verifica le sei letture UTF-8 e rilancia il controllo della cache, per rilevare immediatamente ricadute.
- Aggiornati riferimenti ai pacchetti TEST185 nei controlli e nella pipeline.

## Compatibilità

Nessuna modifica al DSP, ai campioni, alle cartelle, ai preset, all'estrazione video/AAC, alla suddivisione stereo, al caricamento su worker, alla gestione cromatica né ad Avanzate/Svuota cache rispetto alla TEST184.

Il workflow Windows produrrà `LSampler-24-Windows-VST3-TEST185.zip` dopo che tutti i passi avranno avuto successo.
