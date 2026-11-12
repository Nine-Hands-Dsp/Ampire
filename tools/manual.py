# AMPIRE (NINE HANDS | DSP) - notice PDF en francais, illustree avec le rendu reel du plugin.
# Usage : python3 tools/manual.py <dossier des captures> <fichier PDF de sortie>
#   captures attendues (AmpireTool render ... 2) : main.png (capture A2 chargee, POWER on, STACK off), lib.png (bibliotheque
#   ouverte), stackon.png (STACK on), poweroff.png (POWER off), ladder.png (conteneur a plusieurs sous-modeles),
#   menu.png (AmpireTool menu), t3k.png (onglet TONE3000, donnees fictives : render ... t), t3kconnect.png (ecran de connexion : render ... s).
import os, sys, subprocess, shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS, OUT = sys.argv[1], sys.argv[2]
VERSION = "1.1.6"
TMP = os.path.dirname(os.path.abspath(OUT))
MA = os.path.join(ROOT, "tools", "manual_assets")
for n, f in (("Pop", os.path.join(ROOT, "assets", "Poppins-Regular.ttf")), ("PopM", os.path.join(ROOT, "assets", "Poppins-Medium.ttf")),
             ("PopB", os.path.join(ROOT, "assets", "Poppins-Bold.ttf")), ("Title", os.path.join(MA, "fonts", "BarlowCondensed-ExtraBold.ttf"))):
    pdfmetrics.registerFont(TTFont(n, f))

ACC = colors.HexColor("#E8701A"); ACC_L = colors.HexColor("#FFB066")      # orange des LEDs des vu-metres
DARK = colors.HexColor("#1B1A19"); GREY = colors.HexColor("#6A6662"); LIGHT = colors.HexColor("#F6EEE6")
LOGO_PNG = os.path.join(TMP, "nine_hands_logo_blanc.png")
subprocess.run(["rsvg-convert", "-w", "1200", os.path.join(MA, "nine_hands_logo_blanc.svg"), "-o", LOGO_PNG], check=True)

# ---------------------------------------------------------------- images (repere de la facade : 3600 x 1440)
def shot(n):
    im = Image.open(os.path.join(SHOTS, n)).convert("RGB")
    # poignee de redimensionnement de la fenetre (coin bas-droit) : remplacee par le bord voisin du boitier
    g = int(im.width * 34 / 1494)
    im.paste(im.crop((im.width - 2 * g - 8, im.height - g, im.width - g - 8, im.height)), (im.width - g, im.height - g))
    im.paste(im.crop((im.width - g, im.height - 2 * g - 8, im.width, im.height - g - 8)), (im.width - g, im.height - 2 * g))
    return im
main = shot("main.png")
k = main.width / 3600.0
def crop_d(im, box): return im.crop(tuple(int(round(v * k)) for v in box))
def save(im, n): p = os.path.join(TMP, n); im.save(p); return p

# capture annotee
KX = [600, 1000, 1400, 1800, 2200, 2600]
marks = [(1, 1500, 300), (2, 2950, 270), (3, 3174, 268), (4, 3328, 268), (5, 3482, 268), (6, 135, 1392), (7, 3465, 1392)] + \
        [(8 + i, x, 1200) for i, x in enumerate(KX)] + [(14, 2954, 1300), (15, 3174, 1300)]
ann = main.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(ROOT, "assets", "Poppins-Bold.ttf"), 24)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 19
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(232, 112, 26), width=4)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(27, 26, 25), anchor="mm")
annotated = save(ann, "ampire_annotated.png")

# bibliotheque (dossier d'exemple aux noms neutres) et onglet TONE3000
lib = shot("lib.png")
lib_img = save(lib, "ampire_library.png")
t3k_img = save(shot("t3k.png"), "ampire_t3k.png")
t3kc_img = save(shot("t3kconnect.png"), "ampire_t3k_connect.png")

gap = 14
def strip(parts, name):
    s = Image.new("RGB", (sum(p.width for p in parts) + gap * (len(parts) - 1), max(p.height for p in parts)), (255, 255, 255))
    x = 0
    for p in parts: s.paste(p, (x, 0)); x += p.width + gap
    return save(s, name)
box_sw = (2830, 640, 3300, 1215)
sw_img = strip([crop_d(shot("poweroff.png"), box_sw), crop_d(main, box_sw), crop_d(shot("stackon.png"), box_sw)], "ampire_switches.png")
box_a2 = (2395, 650, 2850, 1110)
a2_img = strip([crop_d(main, box_a2), crop_d(shot("ladder.png"), box_a2)], "ampire_a2size.png")
lcd_img = save(crop_d(main, (40, 195, 3100, 555)), "ampire_screen.png")
box_m = (40, 555, 300, 1365)
meter_img = strip([crop_d(main, box_m), crop_d(main, (3300, 555, 3560, 1365))], "ampire_meters.png")
menu_img = save(Image.open(os.path.join(SHOTS, "menu.png")).convert("RGB"), "ampire_menu.png")

# couverture : facade detouree (rectangle arrondi du boitier), reflet au sol, halo orange tres doux
cut = main.convert("RGBA")
W_, H_ = cut.size; rr = 43.54 * k
yy, xx = np.mgrid[0:H_, 0:W_].astype(np.float64)
qx = np.abs(xx + 0.5 - W_ / 2) - (W_ / 2 - rr); qy = np.abs(yy + 0.5 - H_ / 2) - (H_ / 2 - rr)
sd = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rr
a = np.asarray(cut).copy(); a[..., 3] = (np.clip(0.5 - sd, 0, 1) * 255).astype(np.uint8)
# coin bas-droit : la poignee de redimensionnement de la fenetre n'est pas sur la facade
cut = Image.fromarray(a, "RGBA")
cw, ch = cut.size
pad = int(cw * 0.10)
canvas = Image.new("RGBA", (cw + 2 * pad, pad + int(ch * 1.30)), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.58)) ** 2 + ((yy - gcy) / (ch * 0.70)) ** 2)) * 30
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
canvas = Image.alpha_composite(canvas, Image.fromarray(np.dstack([glow * 0.95, glow * 0.45, glow * 0.12, np.full_like(glow, 255)]).astype(np.uint8), "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM); ra = np.array(refl).astype(np.float64)
ra[..., 3] *= (np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22)[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
COVER_PAD, COVER_W, COVER_H = pad, cw, ch
cover = save(canvas.convert("RGB"), "ampire_cover_cutout.png")
os.makedirs(MA, exist_ok=True)
for p in (annotated, lib_img, sw_img, a2_img, lcd_img, meter_img, menu_img, cover): shutil.copy(p, os.path.join(MA, os.path.basename(p)))

def img(path, width):
    im = Image.open(path); return RLImage(path, width=width, height=width * im.height / im.width)

# ---------------------------------------------------------------- styles
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "tip": ParagraphStyle("tip", fontName="Pop", fontSize=9.0, leading=12.4, textColor=DARK, spaceAfter=3),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY, spaceAfter=6),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items, s="p"): return [P("&bull;&nbsp;&nbsp;" + i, s) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#E3D7CB")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

W = A4[0] - 36 * mm
DL = "https://github.com/Nine-Hands-Dsp/Ampire/releases/latest/download/"

# ---------------------------------------------------------------- contenu (conforme au code livre)
story = [NextPageTemplate("p"), Spacer(1, 1), PageBreak()]
story += [P("1. Présentation", "h1"),
    P("<b>AMPIRE</b> est un plugin NINE HANDS | DSP qui charge des captures <b>Neural Amp Modeler</b> (fichiers <b>.nam</b>) : amplis, préamplis, pédales ou chaînes complètes, pour guitare comme pour basse. Il lit les captures <b>A1</b> (WaveNet et LSTM) et les nouvelles captures <b>A2</b>, avec le chemin de calcul rapide A2 de NeuralAmpModelerCore 0.6."),
    P("Le son est celui du moteur NAM d'origine, sans rien ajouter : mesuré sur de vraies captures A1 et A2, l'écart entre Ampire et NeuralAmpModelerCore est inférieur à −110 dB (hors gain de normalisation). Autour du modèle, Ampire ajoute le confort d'un vrai ampli : potards INPUT / OUTPUT, pile de tonalité BASS / MIDDLE / TREBLE (celle du plugin NAM d'origine), choix de la taille des captures A2, anti-repliement sans latence, bibliothèque de captures avec recherche et favoris, téléchargement direct des tones TONE3000, presets, vu-mètres à LEDs et MIDI Learn."),
    P("Dans REAPER, l'inverseur <b>STACK</b> affiche l'<b>Impulse</b> de la même piste (le chargeur d'IR NINE HANDS | DSP) collé sous la façade d'Ampire : ampli et baffle se règlent dans une seule fenêtre."),
    P("2. Installation", "h1")]
for h, x in [("Téléchargement", "Dernière version : <b>Windows</b> <font color='#E8701A'>" + DL + "Ampire-VST3-Windows.zip</font> ; <b>macOS</b> <font color='#E8701A'>" + DL + "Ampire-VST3-macOS.zip</font> (boutons de téléchargement en tête du README du dépôt GitHub Nine-Hands-Dsp/Ampire)."),
             ("Windows", "Décompressez l'archive et copiez le dossier <b>Ampire.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>."),
             ("macOS", "Copiez <b>Ampire.vst3</b> dans <b>~/Bibliothèque/Audio/Plug-Ins/VST3</b> et, pour Logic ou GarageBand, <b>Ampire.component</b> (Audio Unit) dans <b>~/Bibliothèque/Audio/Plug-Ins/Components</b>. Le plugin est signé ad hoc : si macOS le bloque, autorisez-le dans Réglages Système &gt; Confidentialité et sécurité, ou tapez <b>xattr -cr ~/Library/Audio/Plug-Ins/VST3/Ampire.vst3</b> dans le Terminal."),
             ("REAPER", "Options &gt; Préférences &gt; Plug-ins &gt; VST &gt; <b>Re-scan</b>. Le plugin apparaît sous le nom <b>Ampire (NINE HANDS | DSP)</b>."),
             ("Réglages", "Les réglages communs à toutes les instances (dossier de captures, favoris, captures récentes, couleurs, presets) sont rangés dans le dossier <b>Nine Hands DSP/Ampire</b> des données d'application de l'utilisateur.")]:
    story += [P(h, "h2"), P(x)]
story.append(PageBreak())

story += [P("3. Vue d'ensemble de l'interface", "h1"), img(annotated, W), Spacer(1, 3 * mm),
    table([["#", "Élément", "Rôle"],
        ["1", "Écran", "Capture chargée : nom, matériel / type / son, puis format (A1 / A2, taille), fréquence, anti-repliement, calibration, ESR. Clic = bibliothèque ; déposez un .nam dessus pour le charger."],
        ["2", "Zone d'état", "CPU, fréquence du projet, anti-repliement et latence ; affiche aussi les messages (réglage modifié, MIDI Learn…)."],
        ["3", "Bibliothèque", "Ouvre la bibliothèque de captures et l'onglet TONE3000."],
        ["4 / 5", "Précédente / suivante", "Capture précédente / suivante du dossier (ou de la liste filtrée de la bibliothèque)."],
        ["6 / 7", "Vu-mètres IN / OUT", "Niveau d'entrée (après INPUT) et de sortie, 48 LEDs, mémoire de crête, écrêtage."],
        ["8", "INPUT", "Gain d'entrée, −20 à +20 dB."],
        ["9 – 11", "BASS / MIDDLE / TREBLE", "Pile de tonalité, 0 à 10 (5 = neutre)."],
        ["12", "OUTPUT", "Volume de sortie, −40 à +40 dB."],
        ["13", "A2 SIZE", "Taille des captures A2 « slimmables » (LITE / FULL), ou GAIN STEP sur les conteneurs à plusieurs sous-modèles."],
        ["14", "STACK", "Mode stack avec Impulse (REAPER), diode allumée quand il est actif."],
        ["15", "POWER", "Ampire en marche / bypass, diode allumée en marche."]], [12 * mm, 38 * mm, W - 50 * mm]),
    P("Clic droit n'importe où sur l'interface : menu des réglages (chapitre 9).", "small"),
    PageBreak()]

story += [P("4. Captures, bibliothèque et TONE3000", "h1"),
    P("L'écran", "h2"), img(lcd_img, W * 0.9), Spacer(1, 2 * mm),
    P("La première ligne donne le titre de la capture (ou le nom du fichier), la deuxième le matériel, le type (ampli, pédale, chaîne complète…) et le son (clean, crunch, high gain…) lus dans le fichier, la troisième le format : <b>A2 FULL / LITE</b> ou <b>STEP n/N</b>, <b>A1</b> et sa variante, la fréquence de la capture, l'anti-repliement (<b>AA 2x</b>), le niveau de calibration (<b>cal … dBu</b>) et l'erreur d'entraînement (<b>ESR</b>) quand le fichier les fournit. <b>project copy</b> signale une capture relue depuis le projet (fichier d'origine introuvable). À droite : charge CPU, fréquence du projet, anti-repliement, et <b>LATENCY 0 (none added)</b> quand le projet est à la fréquence de la capture."),
    P("Charger une capture", "h2"),
    *bullets(["<b>Bibliothèque</b> : clic sur l'écran ou sur le bouton bibliothèque.",
              "<b>Flèches</b> : capture précédente / suivante, dans l'ordre de la bibliothèque (filtres et recherche compris).",
              "<b>Glisser-déposer</b> d'un fichier .nam sur l'interface.",
              "Le chargement se fait en arrière-plan, sans couper le son ; le passage d'une capture à l'autre (comme un changement de taille ou d'anti-repliement) se fait par un fondu de 30 ms, sans clic. Pendant le chargement, l'écran affiche <b>LOADING...</b>",
              "La capture est enregistrée <b>dans le projet</b> (et dans les presets) : le projet se rouvre avec le bon son même si le fichier a été déplacé."]),
    P("La bibliothèque", "h2"), img(lib_img, W * 0.82), P("La bibliothèque : dossiers, sous-dossiers et captures.", "small"),
    P("Elle s'ouvre dans une fenêtre en verre fumé au-dessus d'Ampire (clic sur l'écran ou sur le bouton bibliothèque). L'onglet <b>Library</b> montre vos captures, l'onglet <b>TONE3000</b> le catalogue en ligne (plus bas)."),
    *bullets(["<b>Trois colonnes</b> : à gauche <b>Favourites</b>, <b>Recent</b> puis les dossiers de votre dossier de captures ; au milieu les sous-dossiers du dossier choisi ; à droite ses captures. Chaque colonne défile à la molette ; le chemin s'affiche au-dessus.",
              "<b>Recherche</b> (en haut) sur le nom, le matériel, le fabricant, le son, le dossier… dans toute la bibliothèque, plusieurs mots possibles.",
              "<b>Dossier</b> (icône à côté de la recherche) : choisir le dossier de vos captures (sous-dossiers compris, analysé en arrière-plan), charger un fichier isolé, ouvrir le dossier. <b>Rescan</b> relit le dossier.",
              "<b>Un clic</b> sur une capture la charge tout de suite (écoute rapide) et laisse la bibliothèque ouverte ; <b>double-clic</b> ou <b>Entrée</b> la charge et ferme la bibliothèque ; les flèches haut / bas du clavier passent d'une capture à l'autre en l'écoutant ; <b>Échap</b> ferme.",
              "<b>Nom trop long</b> : laissez la souris 800 ms sur un dossier ou une capture dont le nom est coupé (…) : la ligne s'allonge et montre le nom en entier.",
              "<b>Clic droit sur un dossier</b> : afficher dans l'Explorateur / le Finder, <b>Rename folder...</b>, <b>Delete folder...</b> ; <b>sur une capture</b> : charger, favori, afficher, <b>Delete capture...</b>. Chaque suppression demande une confirmation et envoie le dossier ou le fichier dans la Corbeille, d'où il se restaure : rien n'est effacé définitivement.",
              "<b>Étoile</b> (à gauche de la ligne) : ajoute ou retire des favoris. Pastille <b>A2 / A1</b> et type (Amp, Pedal, Full rig) quand le fichier l'indique. ",
              "<b>Dossier par défaut</b> : mémorisé pour toutes les instances et tous les projets. La première capture chargée le définit ; on le change par l'icône dossier ou par clic droit &gt; Capture folder."]),
    P("TONE3000", "h2"),
    P("Ampire est partenaire de <b>TONE3000</b> (tone3000.com), le catalogue communautaire de captures NAM et d'IR. L'onglet <b>TONE3000</b> de la bibliothèque télécharge les tones directement dans votre dossier de captures."),
    img(t3kc_img, W * 0.82), P("Avant la première connexion.", "small"),
    *bullets(["<b>CONNECT WITH TONE3000</b> ouvre votre navigateur sur la page de connexion de TONE3000 (e-mail et code à 6 chiffres) ; revenez ensuite dans Ampire. Ampire ne voit jamais votre mot de passe : il garde une autorisation qu'il renouvelle seul, la connexion se fait donc une seule fois. <b>Sign out</b> (sous votre nom) la supprime.",
              "Il faut avoir choisi un dossier de captures dans l'onglet Library : c'est là que les tones sont rangés."]),
    img(t3k_img, W * 0.82), P("Les tones favoris sur TONE3000 (exemple).", "small"),
    *bullets(["<b>Favorites</b> (vos favoris sur TONE3000), <b>Synced</b> (les tones déjà téléchargés par Ampire), <b>Created</b> (vos propres tones), <b>Trending</b> et <b>Latest</b> (les 10 tones du moment et les 10 plus récents). Les cartes défilent vers le bas à la molette et la suite se charge toute seule.",
              "Chaque carte montre l'image du tone, son titre, son type, son format (A2, A1 ou IR) et son auteur. Le bouton suit l'état du tone : <b>SYNC</b> le télécharge, <b>DOWNLOADING n / N</b> pendant le téléchargement, <b>SYNCED · LOAD</b> une fois téléchargé (un clic passe dans l'onglet Library sur le dossier du tone et charge sa capture A2), <b>UPDATE AVAILABLE</b> quand l'auteur a publié une nouvelle version. Clic droit : télécharger à nouveau, afficher dans le dossier, ouvrir la page TONE3000.",
              "<b>Nom du dossier</b> : au clic sur SYNC, une petite fenêtre propose le titre du tone comme nom de dossier ; modifiez-le si vous voulez, puis Entrée ou SYNC. La case « Ask for the folder name each time I sync » permet de ne plus être interrogé (réglage repris par clic droit sur une carte). Un dossier déjà synchronisé se renomme par clic droit &gt; <b>Rename folder...</b> : le tone reste « Synced » et ses mises à jour vont dans le dossier renommé. On peut aussi renommer ou déplacer le dossier dans l'Explorateur / le Finder : Ampire le retrouve grâce à son metadata.json.",
              "<b>Re-sync</b> (à gauche de Versions) : <b>Scan my capture folder</b> relit votre dossier et retrouve tous les tones déjà synchronisés, par Ampire ou par NAMMAN, même renommés ou déplacés ; <b>Add a folder...</b> range dans votre dossier de captures les tones synchronisés ailleurs (par exemple le dossier de NAMMAN), sans rien retélécharger ni écraser ; <b>Update all synced tones</b> télécharge d'un coup toutes les mises à jour disponibles.",
              "<b>Browse TONE3000</b> ouvre le catalogue complet de TONE3000 dans le navigateur : le tone choisi est téléchargé et apparaît dans <b>Synced</b>.",
              "<b>Versions</b> (à droite) : <b>Best</b> (par défaut) ne garde qu'une version de chaque capture, A2 sinon A1 ; ou <b>A2 only</b>, <b>A1 only</b>, <b>Custom only</b>, <b>All versions</b>. Les IR sont toujours gardées.",
              "<b>Rangement</b> (le même que NAMMAN) : <b>&lt;dossier&gt;/&lt;catégorie&gt;/&lt;nom du tone&gt;/</b> avec un fichier <b>metadata.json</b> ; catégories <b>Amp_and_Cab</b>, <b>Amps</b>, <b>Pedals</b>, <b>Cabinets_IRs</b> (les IR, pour Impulse), <b>Spaces</b>, <b>Outboard</b>, <b>Experimental</b>. À une mise à jour, les anciennes versions sont déplacées dans <b>_Archive</b> : rien n'est jamais effacé.",
              "Respectez la licence de chaque tone (affichée sur sa page TONE3000). L'accès gratuit de TONE3000 est réservé à un usage non commercial."]),
    ]

story += [P("5. Les potards", "h1"),
    P("Glisser vers le haut / le bas (ou la droite / la gauche) pour tourner, <b>Maj</b> + glisser pour un réglage fin, molette, <b>double-clic</b> = valeur par défaut. La valeur s'affiche sous le titre de chaque potard. Ampire n'assigne aucun CC MIDI d'office : chaque potard se pilote par MIDI Learn (chapitre 10).")]
for h, x in [("INPUT", "Gain d'entrée avant la capture, de <b>−20 à +20 dB</b> (0 par défaut). C'est lui qui fait plus ou moins « saturer » l'ampli modélisé, comme le volume de la guitare. Le vu-mètre IN montre le niveau après INPUT."),
             ("BASS / MIDDLE / TREBLE", "Pile de tonalité du plugin NAM d'origine, après la capture, de <b>0 à 10</b> ; <b>5 = neutre</b> (aucune correction). Clic droit &gt; <b>Tone stack</b> la met hors circuit."),
             ("OUTPUT", "Volume de sortie, de <b>−40 à +40 dB</b> (0 par défaut), après la tonalité. Le niveau de départ dépend du mode choisi dans clic droit &gt; Output level (chapitre 9)."),
             ("A2 SIZE", "Les captures A2 « slimmables » contiennent plusieurs tailles du même modèle : <b>FULL</b> = la meilleure qualité, <b>LITE</b> = moins de CPU. Sur un conteneur à plusieurs sous-modèles (par exemple une échelle de gains), le potard devient <b>GAIN STEP</b> et choisit le sous-modèle (<b>n / N</b>, de LOW à HIGH). Le passage se fait par fondu. Le potard est grisé quand la capture n'a qu'une taille (A1, LSTM, A2 simple).")]:
    story += [P(h, "h2"), P(x)]
story += [KeepTogether([img(a2_img, W * 0.62), P("A2 SIZE sur une capture A2 slimmable (FULL), puis GAIN STEP sur un conteneur à plusieurs sous-modèles.", "small")])]

story += [P("6. STACK et POWER", "h1"), img(sw_img, W * 0.82),
    P("POWER en bas (bypass), POWER en haut (en marche), puis STACK en haut (Impulse affiché sous Ampire).", "small"),
    P("POWER", "h2"),
    P("Levier en haut = Ampire en marche, la diode POWER s'allume ; levier en bas = <b>bypass</b> : le signal passe sans traitement, en fondu, et la diode s'éteint."),
    P("STACK (REAPER)", "h2"),
    P("Chargez <b>Impulse</b> sur la même piste qu'Ampire (après lui dans la chaîne), puis levez l'inverseur STACK : la fenêtre d'Ampire s'agrandit et affiche l'interface d'Impulse collée sous sa façade, à la même largeur. Ce que vous touchez dans cette partie agit sur l'Impulse de la piste, qui reste un effet indépendant avec sa propre fenêtre. La diode STACK s'allume quand le mode est actif. Hors de REAPER, ou sans Impulse sur la piste, l'écran l'indique (<b>IMPULSE STACK   REAPER only</b> / <b>load Impulse on this track</b>). L'état STACK est enregistré avec le projet."),
    P("7. Vu-mètres", "h1")]
mt = Table([[[P("<b>IN</b> (à gauche) mesure l'entrée après INPUT, <b>OUT</b> (à droite) la sortie du plugin, en crête, sur une échelle gravée de −50 à 0 dBFS."),
              *bullets(["48 LEDs, avec un reflet de chaque LED allumée dans le verre de la fenêtre.",
                        "<b>Mémoire de crête</b> : la LED la plus haute reste allumée un moment, puis s'efface en fondu.",
                        "Les 4 LEDs avant les 3 dernières passent progressivement à la couleur de saturation.",
                        "<b>Écrêtage</b> : quand le signal atteint 0 dBFS, les <b>3 LEDs du haut</b> passent en rouge vif.",
                        "Couleur des LEDs et des LEDs de saturation au choix (clic droit &gt; Colours &amp; glow)."])],
             img(meter_img, 40 * mm)]], colWidths=[W - 46 * mm, 46 * mm])
mt.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
story += [mt, PageBreak()]

story += [P("8. Niveaux : normalisation, calibration", "h1"),
    P("<b>Output level</b> (clic droit) règle le niveau de sortie de la capture avant OUTPUT :"),
    *bullets(["<b>Raw</b> : la capture telle qu'elle a été enregistrée.",
              "<b>Normalized</b> (par défaut) : toutes les captures au même volume moyen (−18 dB, la règle de NAM et TONE3000), pour passer d'une capture à l'autre sans saut de volume. Une capture clean enregistrée bas est remontée d'autant : avec un jeu très dynamique, les crêtes peuvent dépasser 0 dBFS ; baissez alors OUTPUT ou passez en Raw.",
              "<b>Calibrated</b> : niveaux en dBu indiqués dans la capture, rapportés au niveau de votre interface."]),
    P("<b>Input calibration</b> : quand la capture indique le niveau d'entrée utilisé lors de son enregistrement (en dBu), <b>Calibrate input to each capture</b> ajuste le gain d'entrée pour que l'ampli modélisé reçoive le même niveau que l'ampli réel. Indiquez pour cela le niveau de votre interface : <b>Interface: 0 dBFS = +… dBu</b> (0, 6, 9, 12, 15, 18, 20 ou 24 dBu, voir la fiche technique de votre interface ; 12 dBu par défaut). Désactivé par défaut."),
    P("9. Menu du clic droit", "h1")]
mn = Table([[img(menu_img, 66 * mm),
    [table([["Menu", "Contenu"],
        ["Presets", "Init (empty), liste de vos presets, Save preset as..., Save, Delete, Open presets folder (chapitre 11)."],
        ["Anti-aliasing", "Off (le moins de CPU), <b>2x</b> (recommandé, par défaut), 4x (le meilleur, pour le high gain). Suréchantillonnage à phase minimale, sans latence."],
        ["Output level", "Raw, Normalized, Calibrated (chapitre 8)."],
        ["Input calibration", "Calibration d'entrée et niveau de l'interface (chapitre 8)."],
        ["Input channel", "Left (par défaut), Right ou L + R (somme) pour une piste stéréo."],
        ["Tone stack", "Met en circuit / hors circuit BASS / MIDDLE / TREBLE."],
        ["Colours &amp; glow", "Couleur des diodes POWER / STACK et de leur halo (Off, Low, Medium, High) ; couleur des LEDs des vu-mètres et des 3 LEDs de saturation."],
        ["Capture folder", "Dossier par défaut, Choose default capture folder..., Use this capture's folder as default, Rescan library."],
        ["Low cut / High cut", "Coupe-bas (Off, 40 à 150 Hz) et coupe-haut (Off, 12 à 5 kHz) après la capture."],
        ["MIDI", "MIDI Learn de chaque commande (chapitre 10)."],
        ["Plugin size", "Taille de la fenêtre (chapitre 12)."]], [26 * mm, W - 70 * mm - 26 * mm])]]],
    colWidths=[70 * mm, W - 70 * mm])
mn.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
story += [mn, P("Les menus s'ouvrent là où vous cliquez.", "small"), PageBreak()]

story += [P("10. MIDI", "h1"),
    P("Ampire écoute tous les canaux MIDI. <b>Aucun CC n'est assigné d'office</b> : vous choisissez chaque assignation avec le MIDI Learn, le plugin ne réagit donc jamais à un contrôleur que vous n'avez pas assigné vous-même. Dans REAPER, la piste doit recevoir le MIDI du contrôleur (armement ou monitoring d'entrée, entrée MIDI réglée sur votre appareil)."),
    table([["Commande", "Type"],
           ["Input, Bass, Middle, Treble, Output, A2 size", "Continu (0-127 = toute la course)"],
           ["Power", "À accrochage (64 ou plus = en marche), ou momentané si « Footswitch momentary » est coché (chaque appui bascule)"],
           ["Previous capture / Next capture", "Déclencheur : capture précédente / suivante"],
           ["Preset previous / Preset next", "Déclencheur : preset précédent / suivant"]], [62 * mm, W - 62 * mm]),
    Spacer(1, 2 * mm),
    *bullets(["<b>MIDI Learn</b> : clic droit &gt; MIDI &gt; <b>Learn: &lt;commande&gt;</b>, puis bougez la pédale ou le potard, ou appuyez sur le bouton MIDI. La commande clignote tant qu'elle attend ; l'écran confirme l'assignation. <b>Cancel MIDI learn</b> annule l'attente.",
              "<b>Clear all MIDI assignments</b> retire toutes les assignations. Les assignations sont enregistrées avec le projet."]),
    P("11. Presets", "h1"),
    P("Un preset Ampire (fichier <b>.ampire</b>) contient tous les réglages <b>et la capture elle-même</b> : il se recharge à l'identique sur une autre machine. Ampire n'a pas de presets d'usine : la liste du menu est celle de vos presets."),
    *bullets(["<b>Init (empty)</b> : remet tous les réglages par défaut et décharge la capture.",
              "<b>Save preset as...</b> : enregistre sous un nouveau nom ; <b>Save « nom »</b> met à jour le preset courant (une * après le nom signale des changements non enregistrés) ; <b>Delete « nom »</b> le supprime.",
              "<b>Open presets folder</b> : ouvre le dossier <b>Presets</b> (dans Nine Hands DSP/Ampire) pour sauvegarder ou partager vos fichiers.",
              "Les presets précédent / suivant peuvent être pilotés en MIDI (chapitre 10)."]),
    P("12. Fenêtre", "h1"),
    P("Clic droit &gt; <b>Plugin size</b> : <b>60 %</b> (taille par défaut), 75 % ou 100 %. <b>Default size (new windows)</b> choisit la taille d'ouverture des nouvelles fenêtres. Le coin en bas à droite redimensionne la fenêtre librement, sans jamais descendre sous la taille 60 %. L'interface est dessinée pour rester fluide sous Windows comme sous macOS (anti-gel v2)."),
    PageBreak()]

story += [P("13. Conseils d'utilisation", "h1"),
    *bullets(["Réglez d'abord le niveau d'entrée : sur une capture clean, faites monter le vu-mètre IN vers −12 / −6 dBFS en jouant fort, puis réglez le grain avec INPUT, comme le volume de la guitare.",
              "Une capture d'ampli seul (sans baffle) demande une IR de baffle après Ampire : chargez Impulse juste après lui et levez STACK pour régler les deux dans la même fenêtre. Les captures « Full rigs » contiennent déjà le baffle.",
              "Laissez l'anti-repliement sur 2x ; passez en 4x pour du high gain très saturé, en Off pour économiser le CPU sur un projet chargé.",
              "Gardez le projet à la fréquence de la capture (48 kHz en général) : la latence reste à 0. Sinon Ampire rééchantillonne et annonce sa latence à REAPER.",
              "En Normalized, passer d'une capture à l'autre se fait au même volume : idéal pour comparer. Si les crêtes approchent 0 dBFS (LEDs rouges du vu-mètre OUT), baissez OUTPUT.",
              "Guitare 7 cordes ou accordage grave : un Low cut à 40 ou 60 Hz resserre le grave sur les captures high gain.",
              "Une capture A2 slimmable en LITE réduit nettement la charge CPU, pratique pour plusieurs instances."], "tip"),
    KeepTogether([P("14. Caractéristiques techniques", "h1"),
        table([["Format", "VST3 (Windows, macOS), Audio Unit (macOS), 64 bits ; macOS universel (Apple Silicon et Intel)"],
               ["Captures", ".nam : A1 (WaveNet, LSTM), A2 (chemin rapide NeuralAmpModelerCore 0.6), A2 slimmables et conteneurs à plusieurs sous-modèles"],
               ["Commandes", "INPUT −20…+20 dB ; BASS / MIDDLE / TREBLE 0…10 ; OUTPUT −40…+40 dB ; A2 SIZE ; STACK ; POWER"],
               ["Entrée / sortie", "Mono traité (canal gauche, droit ou somme), sortie sur les deux canaux"],
               ["Anti-repliement", "Off / 2x / 4x, phase minimale, sans latence"],
               ["Latence", "0 échantillon à la fréquence de la capture ; sinon rééchantillonnage, latence annoncée au DAW"],
               ["Niveaux", "Raw, Normalized (−18 dB), Calibrated ; calibration d'entrée 0 à +24 dBu"],
               ["Filtres", "Low cut Off / 40-150 Hz, High cut Off / 12-5 kHz"],
               ["Vu-mètres", "IN / OUT, 48 LEDs, −50 à 0 dBFS, mémoire de crête, écrêtage"],
               ["MIDI", "Tous les canaux, MIDI Learn sur 11 commandes, aucun CC assigné par défaut"],
               ["Fenêtre", "60 % (défaut) / 75 % / 100 %, jamais sous 60 %"],
               ["Crédits", "NeuralAmpModelerCore (Steven Atkinson, MIT), suréchantillonneur TONE3000 (MIT), JUCE, polices Inter et Poppins (SIL OFL)"]],
              [30 * mm, W - 30 * mm], header=False)])]

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "Ampire (NINE HANDS | DSP) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

# ---- Couverture « minimale » (gabarit fixe commun a toutes les notices), mesures depuis le HAUT de la page sauf logo / bandeau
PEDAL_H, PEDAL_MAXW, PEDAL_TOP = 82 * mm, 115 * mm, 84 * mm
LANG_TOP = 18 * mm
NAME_TOP, NAME_PT = 194 * mm, 30
SUB_TOP, SUB_PT = 208 * mm, 10
LOGO_W, LOGO_BOTTOM = 38 * mm, 22 * mm
BAND_H = 3 * mm
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, 0, A4[0], BAND_H, stroke=0, fill=1)
    def line(text, top, font, size, cs, col):
        w = pdfmetrics.stringWidth(text, font, size) + cs * (len(text) - 1)
        t = c.beginText((A4[0] - w) / 2, A4[1] - top - size * 0.8); t.setFont(font, size); t.setCharSpace(cs)
        t.setFillColor(col); t.textOut(text); c.drawText(t)
    line("FRANÇAIS", LANG_TOP, "PopM", 8.5, 0, ACC_L)
    iw, ih = Image.open(cover).size
    kk = min(PEDAL_H / COVER_H, PEDAL_MAXW / COVER_W)
    dw, dh = iw * kk, ih * kk
    y0 = A4[1] - (PEDAL_TOP + (PEDAL_H - COVER_H * kk) / 2) + COVER_PAD * kk
    c.drawImage(cover, (A4[0] - dw) / 2, y0 - dh, dw, dh)
    line("AMPIRE", NAME_TOP - NAME_PT * 0.02, "Title", NAME_PT, NAME_PT * 0.5, colors.white)
    line("Chargeur de captures NAM", SUB_TOP, "Pop", SUB_PT, 1.5, colors.HexColor("#BDC4B8"))
    lw, lh = Image.open(LOGO_PNG).size
    c.drawImage(LOGO_PNG, (A4[0] - LOGO_W) / 2, LOGO_BOTTOM, LOGO_W, LOGO_W * lh / lw, mask="auto")
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="AMPIRE - Notice v" + VERSION, author="NINE HANDS | DSP")
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
doc.build(story)
print(OUT)
