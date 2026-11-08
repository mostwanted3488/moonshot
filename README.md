# MOONSHOT — clipper Hard / Soft (© buyorbealoser)

Un seul knob **BUILD** pousse le signal dans un clipper ; à 100 % la planète explose.

| Contrôle | Rôle |
|---|---|
| **BUILD** (lame de scie, glisser haut/bas, double-clic = 0 %) | Drive de 0 à +20 dB. Le plafond reste à **-0,3 dBFS**. |
| **MIX** (petite lame, double-clic = 100 %) | Dosage son sec / son clippé. Le sec est recalé sur la latence. |
| **HARD / SOFT** (stickers) | Mode de clipping. Changement sans clic (fondu de 15 ms). |

## Le son
- **HARD** : transparent jusqu'au plafond, puis mur net.
- **SOFT** (esprit *Fruity Soft Clipper*) : linéaire jusqu'à un genou, puis épaule en `tanh` qui tend vers le plafond. La pente est continue au genou. Le genou descend quand BUILD monte (plafond à 0 %, moitié du plafond à 100 %) : BUILD 0 % est transparent, la saturation s'installe progressivement.
- Oversampling x4, latence de 4 échantillons annoncée à l'hôte.
- La sortie ne dépasse jamais -0,3 dBFS, même avec +18 dBFS en entrée.

### Volumes mesurés (bruit à -18 dB RMS, MIX 100 %)
| BUILD | RMS HARD | RMS SOFT | Crête |
|---|---|---|---|
| 0 % | -18,0 | -18,0 | -7,1 |
| 20 % | -14,0 | -14,0 | -3,1 |
| 50 % | -8,1 | -8,1 | -0,3 |
| 80 % | -4,1 | -4,3 | -0,3 |
| 100 % | -2,9 | -3,1 | -0,3 |

Le niveau monte d'environ 15 dB sur cette matière : comme un clipper classique, le volume augmente avec le drive. Baisse le fader de la piste ou utilise MIX si tu veux garder le même volume.

## Compiler (GitHub Actions)
1. Pousse ce dossier sur GitHub.
2. Onglet **Actions → Build MOONSHOT** (se lance à chaque push).
3. Télécharge l'artefact `MOONSHOT-windows` → `MOONSHOT.vst3`, à copier dans `C:\Program Files\Common Files\VST3`. L'artefact `MOONSHOT-macos` contient VST3 et AU (Intel + Apple Silicon).

## Tests (Linux)
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DMOONSHOT_BUILD_TESTS=ON
    cmake --build build --target MoonshotDspTest MoonshotPluginTest MoonshotEditorRender
Sur une machine sans les en-têtes X11 optionnels, ajoute `-DMOONSHOT_MINIMAL_X11=ON`.

- `MoonshotDspTest` : courbes, transparence à 0 %, plafond, niveaux, DC, aliasing, silence, valeurs extrêmes.
- `MoonshotPluginTest` : le vrai processeur (paramètres, état, latence, blocs de 1 à 4096, automation, clic HARD/SOFT, glisser des lames).
- `MoonshotEditorRender` : dessine l'éditeur hors écran à plusieurs états.

Licences des images et polices : voir `ASSETS-LICENSES.md`.
