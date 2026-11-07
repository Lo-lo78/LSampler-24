# LSampler-24 0.99.25 TEST178 — Alt+V senza blocco dell'interfaccia

7 novembre 2026 · Sorgente completo Windows VST3 GitHub Actions.

## Analisi

La TEST177 decodificava già il file su un thread separato. Tuttavia, a ogni nuovo slot, `markAudioStateDirty()` ricostruiva **tutti** gli stati audio dei 24 slot mentre manteneva `stateLock`. Dalla TEST174, `applySampleSetGlobalWindow()` scandisce i campioni per applicare le soglie condivise. Start Threshold è inizialmente -60 dB e un WAV di 6 minuti può contenere milioni di frame. Le scansioni ripetute impedivano a REAPER e a NVDA di interrogare tempestivamente i controlli del plugin.

Il problema non dimostra un conflitto intrinseco con NVDA. Durante Alt+V, la TEST177 disabilitava anche i controlli e trasferiva forzatamente il focus a Status, aumentando gli eventi di accessibilità e la sensazione di congelamento.

## Modifiche

- Cache di preparazione audio **per singolo slot**: i parametri e i Sample Set invariati riutilizzano lo stato DSP preparato; solo gli slot cambiati vengono ricalcolati. Confronto esatto di parametri, Slice, soglie, rate, proprietari dei campioni, velocity, pitch/octave/pan/volume/delay e variation. La cache usa `weak_ptr`, senza tenere artificialmente in RAM audio non più in uso.
- **Importazione REAPER sul worker anche per il calcolo delle soglie e dello stato DSP**: dopo il decode e il taglio, prepara la nuova struttura audio *prima* di acquisire `stateLock`. Durante il commit, il lock è usato soltanto per sostituire lo slot e pubblicare lo snapshot. Se il sample rate cambia durante l'operazione, il motore ricalcola in sicurezza la struttura al nuovo rate.
- Durante l'importazione REAPER, Alt+V **non sposta più il focus** da griglia/slot a Status. L'elemento già focalizzato rimane abilitato (le modifiche da tastiera sono bloccate durante il file task) e Esc rimane disponibile alla DAW. Gli altri lavori su file conservano il comportamento precedente.
- La percentuale rimane leggibile in Status e viene annunciata da UI Automation con l'intervallo già previsto, quando il focus è all'interno del plugin. Aggiunto key listener allo Status per garantire la gestione di Esc anche nelle altre importazioni.
- Mapping MIDI 60, 61, 62..., percorso originale, tagli, SWS, compatibilità con TEST177 e i parametri VST3 non cambiano.

## Verifiche e limiti

La cache è un'ottimizzazione interna e non modifica il suono previsto; i test statici dalla TEST163 alla TEST177 e la verifica TEST178 controllano la struttura. **Non è ancora stata eseguita la compilazione Windows di GitHub Actions né una prova diretta con REAPER + OSARA + NVDA**: un blocco non può essere escluso completamente fino a questi test.

Per provare: selezionare più item anche derivati da un WAV lungo, aprire griglia slot, Alt+V, controllare che NVDA annunci la percentuale senza cambiare focus, verificare Esc durante il caricamento e dopo il completamento. Il thread può continuare l'importazione se si chiude la finestra FX; non cancellare l'istanza VST3 durante un file task salvo voler attendere l'arresto ordinato.
