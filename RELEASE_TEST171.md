# LSampler-24 — 0.99.25 TEST171

**TEST171 sorgente, build VST3 da verificare tramite GitHub Actions.**

Alt+M, F2: Velocity Low, Velocity High, Pitch (-1200/+1200 cents), Pan (-100/+100), Volume (-120/+24 dB) per singolo campione. I valori sono memorizzati nello Slot e nei banchi. F3 Variation Mode: nuova opzione Layer, solo in Voice Mode Poly, sovrappone i campioni le cui zone velocity si intersecano. In Mono è preservata la vecchia selezione di un campione. Le elaborazioni dello Slot restano condivise.

Nella griglia iniziale, + / - variano il Level corrente di 1 dB (-120/+24) senza aprire la pagina parametri; Pagina Su / Pagina Giù navigano quattro slot nella stessa colonna e si fermano ai bordi 1/8.

Non ancora esposte all'automazione host separata le tre nuove proprietà. Nuova modalità Layer potenzialmente più esigente in CPU/numero voci.
