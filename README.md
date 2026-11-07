# Ampire — NINE HANDS | DSP

Chargeur de captures **Neural Amp Modeler** (A1 et A2) pour guitare et basse, en VST3 (Windows, macOS) et AU (macOS).
Neural Amp Modeler capture loader (A1 and A2) for guitar and bass — VST3 (Windows, macOS) and AU (macOS).

## Fonctions

- Captures `.nam` : WaveNet A1, **A2** (chemin rapide A2 de NeuralAmpModelerCore 0.6), LSTM.
- **A2 SIZE** : choix de la taille des captures A2 « slimmables » (FULL = meilleure qualité, LITE = moins de CPU) ; sur les conteneurs à plusieurs sous-modèles, le potard devient **GAIN STEP** (n / N). Grisé si la capture n'en a pas.
- Potards : INPUT (−20…+20 dB), BASS / MIDDLE / TREBLE (pile de tonalité du plugin NAM d'origine), OUTPUT (−40…+40 dB), A2 SIZE, POWER.
- Niveau de sortie : brut, normalisé (−18 dB) ou calibré ; calibration d'entrée en dBu (métadonnées de la capture).
- Anti-repliement : off / 2x / 4x, suréchantillonnage à phase minimale **sans latence** (TONE3000).
- Latence : 0 échantillon quand la fréquence du projet est celle de la capture (48 kHz) ; sinon rééchantillonnage, latence annoncée à l'hôte.
- Changement de capture, de taille ou d'anti-repliement sans clic (fondu de 30 ms), chargement en arrière-plan.
- Bibliothèque : dossier analysé en arrière-plan, recherche, filtres, favoris, récents, précédente / suivante, glisser-déposer d'un `.nam`.
- **Dossier de captures par défaut**, mémorisé pour toutes les instances et tous les projets : la première capture chargée définit ce dossier ; on le change ensuite par l'engrenage → Capture folder (« Choose default capture folder… » ou « Use this capture's folder as default ») ou par FOLDER… dans la bibliothèque. Le dernier dossier ouvert est mémorisé lui aussi.
- Vu-mètres IN / OUT à 48 LEDs (échelle −50…0 dBFS), mémoire de crête qui s'efface au bout de 2 s, reflet des LEDs dans le verre, voyants d'écrêtage. Couleur des LEDs et des 3 LEDs de saturation réglables (engrenage → Meter LED colour / Saturation LED colour), avec fondu sur les 4 LEDs intermédiaires.
- Presets `.ampire` avec la capture intégrée (petit bouton en haut à droite → Presets) ; le projet du DAW garde aussi la capture.
- MIDI Learn sur tous les potards et sur précédente / suivante (aucun CC assigné par défaut).
- Tailles de fenêtre 60 / 75 / 100 %.
- Interface « anti-gel v2 » : rendu logiciel, aucun calcul lourd dans `paint()`, caches reconstruits par le timer, chien de garde de redessin.

## Installation

Télécharger l'artefact du dernier build (onglet **Actions**) :

- **Windows** : copier `Ampire.vst3` dans `C:\Program Files\Common Files\VST3\`.
- **macOS** : copier `Ampire.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` et `Ampire.component` dans `~/Library/Audio/Plug-Ins/Components/`. Le plugin est signé ad hoc : au premier lancement, autoriser dans Réglages Système → Confidentialité et sécurité si besoin.

Réglages et bibliothèque : dossier `Nine Hands DSP/Ampire` des données d'application de l'utilisateur.

## Build

```
cmake -B build
cmake --build build --config Release --target Ampire_VST3
```

JUCE 8.0.15 et NeuralAmpModelerCore v0.6.0 (avec Eigen et AudioDSPTools) sont téléchargés par CMake.
Hors ligne : `-DJUCE_LOCAL_DIR=<juce>` et `-DFETCHCONTENT_SOURCE_DIR_NAMCORE=<clone NAM Core>`.

`tools/` (Linux, option `-DAMPIRE_TOOLS=ON`) : captures de l'interface, rendu audio hors ligne et banc CPU ; `tools/design_assets.py` tire les images de l'interface (`assets/`) de la maquette `assets/design/ampire_design.png`.

## Crédits et licences

- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) — Steven Atkinson, MIT (pile de tonalité reprise du plugin NAM, MIT).
- Suréchantillonneur et mesure de latence (`thirdparty/tone3000`) — TONE3000, MIT, voir `thirdparty/tone3000/LICENSE`.
- [JUCE](https://juce.com) 8.0.15.
- Police Inter — SIL Open Font License.

© NINE HANDS | DSP
