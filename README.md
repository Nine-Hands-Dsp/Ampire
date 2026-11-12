# Ampire — NINE HANDS | DSP

Chargeur de captures **Neural Amp Modeler** (A1 et A2) pour guitare et basse, en VST3 (Windows, macOS) et AU (macOS).
Neural Amp Modeler capture loader (A1 and A2) for guitar and bass — VST3 (Windows, macOS) and AU (macOS).

<p align="center">
  <a href="https://github.com/Nine-Hands-Dsp/Ampire/releases/latest/download/Ampire-VST3-Windows.zip"><img alt="Télécharger pour Windows" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Nine-Hands-Dsp/Ampire/releases/latest/download/Ampire-VST3-macOS.zip"><img alt="Télécharger pour macOS" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-macOS-111111?style=for-the-badge&logo=apple&logoColor=white"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Ampire/releases/latest"><img alt="Dernière version" src="https://img.shields.io/github/v/release/Nine-Hands-Dsp/Ampire?label=derni%C3%A8re%20version&color=919496"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/Ampire/raw/main/docs/Ampire_Notice_FR_v1.1.9.pdf"><img alt="Notice (PDF)" src="https://img.shields.io/badge/%F0%9F%93%96%20Notice-PDF%20fran%C3%A7ais-E8701A?style=for-the-badge"></a>
</p>

## Fonctions

- Captures `.nam` : WaveNet A1, **A2** (chemin rapide A2 de NeuralAmpModelerCore 0.6), LSTM.
- **A2 SIZE** : choix de la taille des captures A2 « slimmables » (FULL = meilleure qualité, LITE = moins de CPU) ; sur les conteneurs à plusieurs sous-modèles, le potard devient **GAIN STEP** (n / N). Grisé si la capture n'en a pas.
- Potards : INPUT (−20…+20 dB), BASS / MIDDLE / TREBLE (pile de tonalité du plugin NAM d'origine), OUTPUT (−40…+40 dB), A2 SIZE ; valeur affichée sous chaque titre.
- Inverseurs **STACK** (dans REAPER : l'Impulse de la même piste s'affiche collé sous Ampire) et **POWER**, chacun avec sa diode.
- Niveau de sortie : brut, normalisé (−18 dB) ou calibré ; calibration d'entrée en dBu (métadonnées de la capture).
- Anti-repliement : off / 2x / 4x, suréchantillonnage à phase minimale **sans latence** (TONE3000).
- Latence : 0 échantillon quand la fréquence du projet est celle de la capture (48 kHz) ; sinon rééchantillonnage, latence annoncée à l'hôte.
- Changement de capture, de taille ou d'anti-repliement sans clic (fondu de 30 ms), chargement en arrière-plan.
- Bibliothèque (fenêtre en verre fumé, style des menus d'Octopus) : recherche, trois colonnes dossiers / sous-dossiers / captures avec défilement, favoris, récents, nom complet au survol, agrandissement au défilement (déborde sous Ampire, reste ouvert jusqu'à la croix), barres de défilement à attraper à la souris, renommage et suppression (vers la Corbeille, avec confirmation) par clic droit, précédente / suivante, glisser-déposer d'un `.nam`.
- **TONE3000** : onglet de la bibliothèque relié au catalogue tone3000.com (API officielle, connexion par le navigateur) : Favorites, Synced, Created, Trending, Latest, Browse TONE3000 ; téléchargement des tones dans le dossier de captures, rangement compatible NAMMAN (`<catégorie>/<tone>/` + `metadata.json`, anciennes versions dans `_Archive`), choix des versions (Best, A2, A1, Custom, toutes) ; nom du dossier choisi au téléchargement et renommage par clic droit ; bouton Re-sync (relire le dossier, importer des tones déjà synchronisés ailleurs, tout mettre à jour). Accès gratuit TONE3000 : usage non commercial.
- **Dossier de captures par défaut**, mémorisé pour toutes les instances et tous les projets : la première capture chargée définit ce dossier ; on le change ensuite par le menu du clic droit → Capture folder (« Choose default capture folder… » ou « Use this capture's folder as default ») ou par l'icône dossier de la bibliothèque. Le dernier dossier ouvert est mémorisé lui aussi.
- Vu-mètres IN / OUT à 48 LEDs (échelle −50…0 dBFS), mémoire de crête qui s'efface au bout de 2 s, reflet des LEDs dans le verre ; à l'écrêtage, les 3 LEDs du haut passent en rouge vif. Couleur des LEDs et des 3 LEDs de saturation réglables (clic droit → Colours & glow), avec fondu sur les 4 LEDs intermédiaires.
- Presets `.ampire` avec la capture intégrée (clic droit → Presets) ; le projet du DAW garde aussi la capture.
- MIDI Learn sur tous les potards et sur précédente / suivante (aucun CC assigné par défaut).
- Tailles de fenêtre 60 / 75 / 100 %.
- Diodes POWER / STACK : couleur et glow réglables (clic droit → Colours & glow).
- Tous les réglages (presets, couleurs, anti-repliement, niveaux, MIDI, taille) : menu du clic droit, n'importe où sur l'interface, au même design que les menus d'Octopus.
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

`tools/` (Linux, option `-DAMPIRE_TOOLS=ON`) : captures de l'interface, rendu audio hors ligne et banc CPU ; `tools/manual.py` fabrique la notice (`docs/`) à partir des captures de l'interface (`AmpireTool render` / `menu`). `tools/design_v005.py` calcule les images de l'interface (`assets/`) d'après la maquette `assets/design/v005/Ampire_Design_Mockup_v005.svg` (textes et icônes vectoriels dans le même dossier).

## Crédits et licences

- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) — Steven Atkinson, MIT (pile de tonalité reprise du plugin NAM, MIT).
- Suréchantillonneur et mesure de latence (`thirdparty/tone3000`) — TONE3000, MIT, voir `thirdparty/tone3000/LICENSE`.
- [JUCE](https://juce.com) 8.0.15.
- Polices Inter (interface) et Poppins (menus) — SIL Open Font License.

© NINE HANDS | DSP
