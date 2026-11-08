# LSampler-24 v0.99.25 TEST181 — importazione cromatica con conferma per slot

Basata sulla TEST180. Alt+V da item REAPER e file audio copiati in Esplora file usa la stessa pianificazione deterministica, leggendo una fotografia unica degli slot e delle loro zone MIDI prima del caricamento.

- Slot di destinazione libero: importa il prossimo file e assegna la prima nota MIDI non occupata a partire dalla nota cromatica prevista.
- Slot di destinazione occupato: conferma accessibile con **Y** (Sì, sovrascrive solo questo slot), **N** (No, conserva il campione esistente e importa **lo stesso file** nel prossimo slot libero successivo), **Escape** (annulla tutto l'incolla prima di modificarne gli slot).
- Più slot occupati dopo un No vengono attraversati per cercare lo slot libero; il riepilogo conta gli slot effettivamente preservati. Nessun file viene scartato per il solo fatto che l'utente ha scelto No; dopo la destinazione trovata la sequenza prosegue.
- Una nota già usata da qualunque altro slot (anche zone Low/High Key personalizzate) non viene assegnata a un nuovo slot. Se si accetta una sovrascrittura, la nota originale di uno slot cromatico è conservata quando non confligge con altri.
- I limiti fisici restano 24 slot e 128 note MIDI (0–127). Se non esistono altre destinazioni/note libere, il messaggio finale segnala esplicitamente i file non importati; non sovrascrive mai implicitamente.
- Le conferme avvengono **prima** dell'avvio del lavoro su thread separato. Progressione NVDA, focus, annullamento del file task e riuso della stessa sorgente decodificata restano invariati rispetto alle TEST177–180.
- Conferme per Alt+V non cambiano la logica dell'incolla interno degli slot.

Test C++ indipendente da JUCE aggiornato per Sì/No, slot occupati consecutivi, zone MIDI personalizzate, limiti e continuità cromatica. GitHub Actions comprende i controlli strutturali TEST181.
