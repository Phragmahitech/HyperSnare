# HyperSnare by Phragma

Instrument VST3 de caisse claire en synthèse pure, pensé pour la psytrance et la hi-tech.

- **MIDI** : n'importe quelle note joue le même son ; la hauteur de la note est ignorée, seule la vélocité compte (accents). Une frappe coupe la précédente.
- **BODY** : SINE (deux sinus façon 808), MEMBRANE (deux modes de peau), FM (porteuse modulée, plus sale et plus hi-tech).
- **BALANCE** : du corps au bruit (le timbre).
- **CHARACTER** : du synthétique à l'acoustique (jusqu'à 16 modes de membrane, timbre qui suit la peau, petite pièce).
- **TONE** : TUNE (100 à 400 Hz), PITCH (chute de hauteur, 0 à 36 demi-tons), COLOR (centre du timbre, 1 à 12 kHz), SNAP (claquement).
- **DECAY** : BODY (20 à 800 ms), NOISE (20 ms à 1,5 s), SHAPE (queues naturelles → gatées).
- **WIDTH**, **VARIATION** (différences d'une frappe à l'autre ; à 0, toutes les frappes sont identiques), **DRIVE**, **CRUSH**, **LEVEL**.
- Points de départ : 808, 909, HITECH, REAL.
- **Presets** (en-tête) : ‹ › pour passer d'un preset à l'autre (usine puis les tiens), clic sur le nom pour la liste, **SAVE** pour enregistrer. Une étoile `*` signale un preset modifié. Un fichier `.hspreset` par preset :
  - macOS : `~/Library/Audio/Presets/Phragma/HyperSnare/`
  - Windows : `%APPDATA%\Phragma\HyperSnare\Presets\`

JUCE 8, C++17, CMake. Les polices (Michroma, IBM Plex Mono, licence OFL : voir `Assets/`) sont téléchargées à la configuration depuis `google/fonts` à un commit fixe et vérifiées par SHA-256.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target HyperSnare_VST3
```

Tests : `-DHYPERSNARE_BUILD_TESTS=ON`, puis `ctest --test-dir build -C Release`.
