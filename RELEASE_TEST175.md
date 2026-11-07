# LSampler-24 0.99.25 TEST175 — MIDI Low/High Key Learn + SWS chromatic import

7 novembre 2026.

## 1. Apprendimento MIDI Low Key / High Key

- Apri la griglia dei parametri dello slot (Enter), vai su **Low Key** oppure **High Key**.
- Premi una nota sulla tastiera MIDI con la griglia o il valore in primo piano: imposta **solo il parametro selezionato** al numero della nota ricevuta (0–127). NVDA annuncia il nome del parametro e la nota.
- La nota MIDI usata per apprendere il valore non fa partire un campione; i Note Off continuano a raggiungere il motore per non interrompere il rilascio di note precedenti.
- Non si abilita alcuna modalità MIDI Learn permanente: cambiando parametro, slot, pagina o finestra, l'intercettazione viene disattivata. Gli altri parametri, il normale MIDI e le automazioni rimangono invariati.
- Non si modifica automaticamente il valore dell'altra estremità Low/High, che resta indipendente.

## 2. Importazione tramite SWS + Alt+V

1. Seleziona uno o più item audio in REAPER.
2. Esegui **SWS/BR: Copy take media source file path of selected items to clipboard** (ID `_BR_TSOURCE_PATH_TO_CLIPBOARD`).
3. Apri gli slot di LSampler-24, seleziona quello iniziale, premi **Alt+V**.
4. Il primo file importato avrà **Low Key = High Key = 60 (C4)**. Il secondo **61 (C#4)**; poi **62 (D4)**, **63 (D#4)** e così via. Con 24 file: da 60 a 83 (B5). I numeri di nota non dipendono dallo slot di partenza.

Questa mappatura si applica **solo al percorso Alt+V che riconosce gli item e percorsi SWS di REAPER**: caricamento normale, importazione da file, libreria, Sample Set, incolla tra slot e preset rimangono come nella TEST174. Gli slot occupati richiedono conferma come prima; i file incompatibili vengono saltati. Original Pitch rimane a 60, senza cambiare intonazione di riferimento o effetti.

La modifica del parametro avviene sul thread interfaccia; il thread audio registra la nota in una variabile atomica, senza allocazioni o chiamate al sistema di automazione.

**Verifiche:** test statici e verifica dello script TEST175. **Compilazione VST3, confronto audio e prova in REAPER/OSARA vanno eseguiti tramite GitHub Actions e nell'host.**
