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
