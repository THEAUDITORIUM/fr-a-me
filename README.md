# SYNC — Offkey555

Plugin de warp pour FL Studio : recale un sample dans les temps (comme le Warp d'Ableton), puis l'exporte.
VST3 (Windows / macOS), AU (macOS) et application standalone.

## Utilisation

1. **Import** — `+ Import` ou glisse un fichier (wav, aif, flac, mp3, ogg) n'importe où sur le plugin.
   SYNC détecte le tempo d'origine et chaque coup (les traits verticaux).
2. **Warp** — cale le sample sur le tempo cible et sur la grille.
   - `Host` : suit le tempo de FL Studio (re-warp automatique si tu changes le tempo du projet).
   - `Free` : tempo cible au choix.
3. **Export** — clic = enregistre un WAV 24 bits. **Drag** = glisse le résultat directement dans la playlist ou le channel rack de FL.

`A / B` compare l'original et le résultat. `Preview` fait tourner la boucle quand FL est à l'arrêt ;
quand FL joue, SYNC lit en rythme avec le projet.

## Réglages

| Réglage | Rôle |
|---|---|
| Quantise | Grille sur laquelle les coups sont calés (1/4, 1/8, 1/16, 1/32). Clic = suivant, clic droit = précédent |
| Amount | Force du calage (100 % = pile sur la grille, moins = garde un peu de groove) |
| Source | Tempo d'origine détecté. Glisser pour ajuster, double-clic pour taper, `½` / `×2` si lu au double ou à la moitié |
| Target | Tempo cible (affiche celui de FL en mode Host) |
| Beats / Flow | Beats = découpe sans aucun artefact (drums, percs). Flow = étirement fluide (mélodies, voix, pads) |
| Soft / Medium / Hard | Sensibilité de détection des coups |
| Level | Volume d'écoute (n'affecte pas l'export) |

**Marqueurs** : glisser un trait pour déplacer un coup (aimanté à la grille, `Shift` = libre),
double-clic sur la waveform pour ajouter un marqueur, double-clic sur un marqueur pour le supprimer.

Le fichier, les réglages et les marqueurs sont sauvegardés dans le projet FL.

## Compiler

Le dossier `.github/workflows/build.yml` compile automatiquement Windows (installateur `.exe`) et macOS (`.pkg`)
sur GitHub Actions à chaque push. En local :

```
cmake -S . -B build
cmake --build build --config Release --target SYNC_VST3 SYNC_Standalone
```

Nécessite CMake ≥ 3.22 et Visual Studio 2022 (Windows) ou Xcode (macOS). JUCE 8 est téléchargé automatiquement.
