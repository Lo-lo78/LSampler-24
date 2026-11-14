# LSampler-24 0.99.27 TEST187 — LFO Phase Drift e Lace

Data TEST: 14 November 2026 · Windows x64 VST3 · Source / GitHub Actions.

Sorgente derivato integralmente dalla release 0.99.26 TEST185. Non sostituisce
la release pubblica finché la build MSVC e l'ascolto in REAPER non vengono verificati.

## Correzione TEST187: Self-Lace selezionabile con qualsiasi step

La TEST186 conteneva gia il DSP Self-Lace, ma con gli step generali della
Griglia maggiori di 1 l'editor poteva saltare da Off a LFO 2. Ora la
navigazione per Lace Source avanza sempre di **una opzione** anche con
Step 5, 10, 15, 20 o PageUp/PageDown.

- LFO 1: Off / LFO 1 (Self) / LFO 2.
- LFO 2: Off / LFO 1 / LFO 2 (Self).
- Nessun cambio ai valori numerici, agli ID VST3 o al motore audio.

## Nuove funzioni su entrambi gli LFO, indipendenti per ciascuno dei 24 slot

- **LFO 1/2 Drift Amount** (0–1, default 0): sposta la fase a ogni giro.
- **LFO 1/2 Drift Cycle** (1–32, default 5): ripetizione deterministica dopo N giri.
- **LFO 1/2 Lace Source**: Off, LFO 1, LFO 2. Include Self-Lace.
- **LFO 1/2 Lace Rate** (1–32, default 4): numero di alternanze nel ciclo.
- **LFO 1/2 Lace Mix** (0–1, default 0): entità dell'intreccio.

Le sei forme d'onda già presenti (Sine, Triangle, Saw Up, Saw Down, Square,
Sample And Hold) sono tutte compatibili. In Sample Hold, con Drift attivo,
si usano celle deterministiche che si ripetono dopo N cicli.

In Mono lo stato di ciascun LFO è condiviso a livello Slot; in Poly ogni
voce ha il proprio stato. Lace legge la sorgente del campione precedente per
non creare feedback istantaneo. Una sorgente senza destinazione diretta
continua ad avanzare quando necessaria per Lace.

Free e Trigger mantengono la semantica preesistente; al Play della DAW
vengono azzerati i contatori Free insieme alle fasi, senza cambiare la
riproduzione dei campioni o i cicli di altre funzioni.

**Compatibilità**: i 160 ID parametri dello Slot rimangono invariati.
I nuovi ID 160–169 sono registrati come automazioni VST3 indipendenti
per ogni Slot **dopo** tutte le automazioni già esistenti, e la Bank Macro
riceve anch'essa dieci parametri nuovi. Valori nuovi a zero/Off mantengono
la sintesi precedente.

Test: `Tools/verify-test186-lfo-polyrhythm.py` e
`Tests/LfoPolyrhythmTests.cpp` (eseguito anche dalla build Windows).

## Pubblicazione
Build Windows via `.github/workflows/build-windows.yml`.
La verifica audio/REAPER con NVDA/OSARA deve essere ancora eseguita.
