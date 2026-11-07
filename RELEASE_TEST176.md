# LSampler-24 0.99.25 TEST176 — Importazione diretta degli item selezionati REAPER

7 novembre 2026.

## Utilizzo senza SWS e senza Lua

1. Seleziona uno o più item audio nella timeline di REAPER. **Non premere Ctrl+C.**
2. Apri l'interfaccia di LSampler-24 e porta il focus nella griglia degli Slot.
3. Seleziona lo slot di destinazione iniziale (ad esempio slot 5) e premi **Alt+V**.
4. REAPER fornisce, tramite l'estensione VST3 già esistente, l'elenco degli item selezionati nel **progetto a cui appartiene questo plug-in**, il take attivo, il file audio sorgente e l'intervallo dell'item in secondi sorgente.
5. Gli item importabili vengono ordinati per posizione crescente nella timeline; in caso di posizioni identiche conserva l'ordine di enumerazione di REAPER.
6. Un campione viene caricato per Slot, seguendo la vecchia logica fino allo Slot 24. Se esistono Slot occupati compare la normale conferma Y/N.
7. Ogni Slot importato riceve **Low Key = High Key = Original Pitch**: primo 60 C4, secondo 61 C#4, terzo 62 D4, e così via (fino a 83 B5 per 24 item), indipendentemente dallo Slot iniziale. Questo vale anche se l'importazione contiene un solo item.

### Tre item tagliati dallo stesso file

Per un WAV lungo 10 s, con tre item sorgente pari a 0–1,2 s, 1,2–3,5 s e 3,5–6,8 s, il caricamento produce tre slot con tre campioni diversi. Il file WAV originale è lo stesso, ma ogni slot contiene la porzione audio corrispondente al proprio item. Il sistema di salvataggio già esistente preserva le porzioni audio tagliate nello stato del progetto.

### Compatibilità

- **Nessun nuovo plugin, estensione, script Lua o comando SWS.** Si usa `IReaperHostApplication::getReaperApi`, che LSampler-24 impiega già per le automazioni.
- Quando NON ci sono item selezionati, Alt+V mantiene il percorso SWS/clipboard esistente e, dove appropriato, la copia-incolla interna degli slot.
- Dopo aver premuto Alt+C / Alt+X dentro LSampler-24, Alt+V continua prioritariamente a incollare lo Slot copiato, finché gli appunti del sistema non cambiano.
- Le altre DAW e tutti gli altri caricamenti (Alt+O, biblioteca, Sample Set, preset, copia/incolla tra slot) conservano il comportamento originale.
- **MIDI Key Learn di Low Key e High Key introdotto nella TEST175 rimane invariato.**
- Nessuna modifica a ID VST3, parametri pubblicati, formato degli slot o DSP delle voci.

### Limiti previsti nella TEST176

Il trasferimento senza render è fedele al **taglio nella sorgente audio** soltanto per take normali con playrate 1, pitch 0, nessuno stretch marker, sorgenti non invertite/non sezionate e senza ripetizione oltre la durata del file. Gli item con trasformazioni che cambierebbero la posizione o la durata dei campioni sono **saltati**, non interpretati in modo sbagliato; il numero degli item saltati viene annunciato. I fade, volumi, take/item FX e inviluppi non sono renderizzati. È possibile renderizzarli in REAPER prima di importare se si desidera incorporarli nell'audio.

### Verifiche

- TEST175 aggiornato per il nuovo Original Pitch.
- TEST176 controlla selezione per progetto, sorgente del take, geometria dei tagli, ordinamento e priorità dell'incolla interno.
- Test C++ compilabile autonomamente dei tre tagli sullo stesso sorgente, dei limiti e dei casi non supportati.
- Restano necessari il compilatore MSVC/Windows su GitHub Actions e la prova VST3 in REAPER con NVDA/OSARA.
