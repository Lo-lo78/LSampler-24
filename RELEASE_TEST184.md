# LSampler-24 v0.99.25 TEST184 — correzione build Windows dopo TEST182/183

## Correzione

Nel log di GitHub Actions (8 novembre 2026) la compilazione MSVC della
TEST183 fallisce in `Source/MediaPasteExpansion.cpp` (riga 487) con `C4430`
e `C2059`. La TEST182 contiene la stessa dichiarazione C++ non valida:

```cpp
if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source)))
```

La TEST184 dichiara `reader` come variabile locale, poi controlla se è nullo.
La vita dell'oggetto è gestita dalla stessa `unique_ptr`, e la priorità del
decoder JUCE rispetto a Media Foundation resta invariata.

## Funzionalità conservate

- TEST181: conferma Y/N/ESC per ciascuno slot occupato, ricollocazione cromatica.
- TEST182: file video/AAC tramite decoder Windows e file audio multicanale divisi in coppie stereo.
- TEST183: voci Avanzate per aprire/svuotare la cache delle estrazioni, con protezioni.
- Importazione in background, avanzamento accessibile e gestione del focus invariati.

## Verifica

Include controllo statico TEST184 eseguito nella pipeline Windows prima della
compilazione VST3. La riuscita della build Windows deve essere confermata da
GitHub Actions; i test locali non sostituiscono il compilatore MSVC/JUCE.
