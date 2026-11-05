# LSampler-24 0.99.25 — TEST170

**5 novembre 2026 — determinismo degli LFO Free sul Play della DAW**

- Free (Trigger Off): LFO 1/2 reinizializzati all'avvio Play, con fase, Delay,
  Smooth, One Shot e sequenza Sample & Hold a seme fisso.
- 24 slot indipendenti; stati Mono e Poly trattati separatamente.
- Nessuna modifica a LFO Triggered, campioni, inviluppi, FX, preset, ID VST3,
  compatibilità con le altre DAW o importazione SWS/BR della TEST169.
- Il reset avviene nel callback audio, anche prima dell'uscita idle CPU.
- Nuovi test di trasporto e Sample & Hold; workflow e Help aggiornati.

Test native su Windows/GitHub Actions ancora da eseguire.

## Correzione compilazione TEST170 (5 novembre 2026)

- Risolto l'errore MSVC C2664 in `PluginProcessor.cpp` causato dal passaggio
  diretto di `juce::Optional<int64_t>` al rilevatore che riceve
  `std::optional<int64_t>`. Conversione esplicita senza allocazioni in audio thread.
- Aggiunto controllo di regressione al test TEST170 per prevenire il ripetersi
  dell'errore; nessun cambiamento alle funzioni musicali, preset o parametri.
- Compilazione VST3 completa da verificare su GitHub Actions.

## Correzione gate audio TEST59 / Build Fix (5 novembre 2026)

- Il VST3 compila correttamente, ma il test `filter-parity` in GitHub Actions
  rilevava una differenza audio in `varied_velocity_envelope_wave4`.
- Causa: TEST170 cambiava l'ordine e il generatore dei numeri pseudo-casuali
  di Sample & Hold anche quando il trasporto della DAW non era mai partito.
- Corretto: prima del primo Play si conserva esattamente la generazione casuale
  originale della TEST169/TEST59, anche per Slice e le altre funzioni che
  condividono il generatore delle voci. Dopo Play, soltanto gli LFO Free
  passano ai propri generatori deterministici con reset al Play.
- Il controllo `filter-parity` resta **rigoroso e invariato**: confronto
  bit-per-bit, nessun caso rimosso, nessuna soglia allargata.
- Aggiunti controlli sul selettore del generatore e sulla compatibilità audio
  precedente all'avvio del trasporto.
- Verifica completa Windows/GitHub Actions ancora necessaria.
