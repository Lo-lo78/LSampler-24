# LSampler-24 0.99.25 — TEST166

4 November 2026 — test build for Windows x64 VST3.

## Correzioni

- Click occasionali durante la sostituzione di una voce (Mono retrigger, No Retrigger Smooth, Poly Same Note Replace e voice stealing): crossfade audio a due voci reali di 3 ms con curve smoothstep complementari, coda DSP/filtri inclusa. Le code sono preallocate, escluse dalla polifonia musicale, fermate correttamente da All Notes Off, Stop Slot e All Sound Off.
- Nel Mono con Legato On e Portamento Off, raccordo di 1 ms del passo di lettura al cambio nota, senza resettare il sample head o l’ADSR.
- Sample Start ed End sono vincolati reciprocamente nell’editor e nel motore dei parametri; i valori digitati fuori limite vengono corretti, annunciati e rispecchiati in REAPER.

## Compatibilità e test

Nessuna modifica a numero e identificatori dei parametri VST3, preset Slot/Bank, progetti, tasti o funzionamento del Main Loop. Audio continuo e CPU vanno verificati in REAPER su Windows con una sinusoide di 1 s, prove legato Mono, velocity Poly, Same Note Replace e sustain.

Il workflow mantiene il gate TEST59/TEST62, i controlli TEST163/164/165 e aggiunge il controllo TEST166 e una prova del crossfade nel test nativo. Le compilazioni e l’ascolto Windows/REAPER non sono stati eseguiti nell’ambiente di modifica.
