# AMPIRE - images de l'interface tirees de la maquette de l'utilisateur (assets/design/ampire_design.png, 3600 x 1500).
#   python3 tools/design_assets.py  ->  assets/bg.png, assets/led_on.png, assets/toggle_up.png, assets/toggle_down.png
# Le fond garde tout ce qui est fixe (plaque, potards, graduations, libelles, vis, fenetres, boutons) ; on en retire ce
# qui bouge et que le plugin dessine : repere et valeur des potards, libelles du potard A2 SIZE, textes de l'ecran,
# barres et voyants des vu-metres, voyant POWER, ancien inverseur (remplace par les images d'inverseur fournies).
# Logos officiels (assets/logos, SVG) en blanc : NINE HANDS | DSP en haut a droite, AMPIRE en bas a gauche.
import os, io, subprocess
import numpy as np
from PIL import Image
from scipy import ndimage as nd
HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")
DESIGN = os.path.join(ASSETS, "design")
SRC = os.path.join(DESIGN, "ampire_design.png")
im = np.array(Image.open(SRC).convert("RGB")).astype(np.float32) / 255
H, W, _ = im.shape; k = W / 1999.0
L = im.mean(2)
sat = (im.max(2) - im.min(2)) / np.maximum(im.max(2), 1e-3)
white = (L > 0.42) & (sat < 0.25)
out = im.copy()

def box(x0, y0, x1, y1):
    m = np.zeros((H, W), bool); m[int(y0*k):int(np.ceil(y1*k)), int(x0*k):int(np.ceil(x1*k))] = True; return m

def row_fill(img, mask, detail=True):
    """remplit chaque zone masquee avec le metal pris juste au-dessus ou au-dessous (meme colonnes : le brossage
    est horizontal), puis recale le niveau ligne par ligne sur les pixels voisins a gauche et a droite"""
    res = img.copy()
    big = nd.binary_dilation(mask, iterations=6)
    lab, n = nd.label(nd.binary_dilation(mask, iterations=3))
    for i, sl in enumerate(nd.find_objects(lab), 1):
        m = (lab[sl] == i) & mask[sl]
        y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
        h = y1 - y0
        src = None
        for off in (h + 4, -(h + 4), 2 * h + 8, -(2 * h + 8)):
            a_, b_ = y0 + off, y1 + off
            if a_ < 0 or b_ > H: continue
            if not big[a_:b_, x0:x1].any(): src = img[a_:b_, x0:x1]; break
        if src is None: src = img[y0:y1, x0:x1]
        patch = src.copy()
        for r in range(h):
            y = y0 + r
            lpx = [x for x in range(x0 - 1, max(x0 - 40, 0), -1) if not big[y, x]][:8]
            rpx = [x for x in range(x1, min(x1 + 40, W)) if not big[y, x]][:8]
            ref = img[y, lpx + rpx].mean(0) if (lpx or rpx) else patch[r].mean(0)
            sref = np.concatenate([src[r, :min(8, x1 - x0)], src[r, -min(8, x1 - x0):]]).mean(0)
            patch[r] = patch[r] - sref + ref
        if not detail: patch = nd.uniform_filter1d(patch, 25, axis=1)
        res[y0:y1, x0:x1][m] = patch[m]
    return res

def donor_fill(img, mask, dy0, dy1, side=30, skip=0):
    """remplit les pixels masques : niveau interpole ligne par ligne entre les voisins propres a gauche et a droite
    (cherches au-dela de 'skip' points), plus le grain du brossage pris sur des lignes propres (dy0..dy1) memes colonnes"""
    res = img.copy()
    big = nd.binary_dilation(mask, iterations=4)
    lab, n = nd.label(nd.binary_dilation(mask, iterations=2))
    for i, sl in enumerate(nd.find_objects(lab), 1):
        y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
        donor = img[int(dy0 * k):int(dy1 * k), x0:x1]
        grain = donor - nd.uniform_filter1d(donor, 41, axis=1)
        sk = int(skip * k)
        for y in range(y0, y1):
            m = (lab[y, x0:x1] == i) & mask[y, x0:x1]
            if not m.any(): continue
            lpx = [x for x in range(x0 - 1 - sk, max(x0 - int(side * k) - sk, 0), -1) if not big[y, x]][:12]
            rpx = [x for x in range(x1 + sk, min(x1 + int(side * k) + sk, W)) if not big[y, x]][:12]
            if not lpx and not rpx: continue
            lv = img[y, lpx].mean(0) if lpx else img[y, rpx].mean(0)
            rv = img[y, rpx].mean(0) if rpx else lv
            t = np.linspace(0, 1, x1 - x0)[:, None]
            row = res[y, x0:x1]
            row[m] = (lv * (1 - t) + rv * t + grain[(y - y0) % grain.shape[0]])[m]
    return res

def text_mask(x0, y0, x1, y1, th=0.05):
    reg = box(x0, y0, x1, y1)
    th_ = (L - nd.grey_opening(L, size=(17, 17))) > th
    return nd.binary_dilation(th_ & reg, iterations=2) & reg

# ---- 1. textes de l'ecran : le verre est lisse -> remplissage par la lumiere locale du verre (convolution normalisee)
lcd = text_mask(160, 128, 1680, 294, 0.03) & ~box(1207, 135, 1217, 285)
lcd = nd.binary_dilation(lcd, iterations=int(2.5 * k))
glassm = box(156, 126, 1684, 296) & ~lcd & ~box(1207, 135, 1217, 285)
for c in range(3):
    num = nd.gaussian_filter(out[..., c] * glassm, 12 * k); den = nd.gaussian_filter(glassm.astype(np.float32), 12 * k)
    fill = num / np.maximum(den, 1e-4)
    out[..., c] = np.where(lcd, fill, out[..., c])

# ---- 2. valeurs au-dessus des potards, titre et libelles du potard A2 SIZE (zones entieres, metal de la plaque)
m = np.zeros((H, W), bool)
for cx in (333, 547, 760, 973, 1187, 1400): m |= box(cx - 26, 389, cx + 26, 416)
# toutes les inscriptions de la facade sont redessinees par le plugin (meme rendu pour toutes) : on les retire du fond
for x0_, x1_ in ((288, 386), (507, 590), (700, 822), (913, 1032), (1120, 1252), (1336, 1466), (1658, 1774)):
    m |= box(x0_, 339, x1_, 367)               # INPUT ... OUTPUT, A2 SIZE, POWER
for x0_, x1_ in ((244, 292), (375, 428), (473, 497), (597, 632), (686, 710), (811, 846), (899, 923), (1021, 1056),
                 (1096, 1145), (1223, 1277), (1312, 1366), (1438, 1502)):
    m |= box(x0_, 616, x1_, 645)               # graduations -20 / +20, 0 / 10, -40 / +40, A2
m |= box(1694, 412, 1741, 439)                 # « ON »
m |= box(1692, 598, 1750, 624)                 # « OFF »
out = donor_fill(out, m, 652, 684)

# ---- 3. repere des potards (et son sillon) retire : le chapeau en metal tourne (rayures concentriques, reflets qui ne
#         varient que doucement avec l'angle) est refait en coordonnees polaires, par interpolation en angle a rayon constant
KN = [(331.5, 531), (547.2, 531), (760.7, 531), (974.5, 531), (1187.7, 531), (1402.4, 531)]
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
for cx, cy in KN:
    mask = nd.binary_dilation(box(cx - 13, 461, cx + 13, 510), iterations=2)
    NR, NT = int(68 * k), 1440
    rr_ = np.linspace(0, 68 * k, NR)[:, None]; tt = np.linspace(-np.pi, np.pi, NT, endpoint=False)[None, :]
    px_ = cx * k + rr_ * np.sin(tt); py_ = cy * k - rr_ * np.cos(tt)          # theta = 0 en haut
    pol = np.stack([nd.map_coordinates(out[..., c], [py_, px_], order=1) for c in range(3)], -1)
    pm = nd.map_coordinates(mask.astype(np.float32), [py_, px_], order=1) > 0.01
    tix = np.arange(NT)
    for r in range(NR):
        m = pm[r]
        if not m.any() or m.all(): continue
        good = ~m
        # interpolation circulaire en angle
        gx = np.concatenate([tix[good] - NT, tix[good], tix[good] + NT])
        for c in range(3):
            gv = np.tile(pol[r, good, c], 3)
            pol[r, m, c] = np.interp(tix[m], gx, gv)
    # retour en cartesien pour les pixels du masque
    ys_, xs_ = np.nonzero(mask)
    dx_, dy_ = xs_ - cx * k, ys_ - cy * k
    r_ = np.hypot(dx_, dy_) / (68 * k) * (NR - 1)
    t_ = (np.arctan2(dx_, -dy_) + np.pi) / (2 * np.pi) * NT
    inside = r_ < NR - 1
    for c in range(3):
        v = nd.map_coordinates(np.concatenate([pol[..., c], pol[:, :1, c]], 1), [r_, t_], order=1, mode="nearest")
        out[ys_[inside], xs_[inside], c] = v[inside]

# ---- 4. vu-metres : barres eteintes (verre sombre), debord orange retire, voyants d'ecretage eteints
for x0, x1, win in ((66, 87, (46, 306, 137, 745)), (1916, 1937, (1861, 306, 1952, 745))):
    wm = box(*win)
    glass = np.median(out[box(x0 - 14, 400, x0 - 6, 700) & wm].reshape(-1, 3), axis=0) if True else 0
    bar = box(x0 - 3, 384, x1 + 3, 718)
    out[bar] = glass
    orange = wm & (sat > 0.25) & ~white
    g = np.minimum(L, 0.10)
    soft = nd.gaussian_filter(orange.astype(np.float32), 1.0) * wm
    neutral = np.stack([g, g, g * 1.03], -1)
    out = out * (1 - soft[..., None]) + neutral * soft[..., None]
for cx, cy in ((76.1, 369.7), (1926.4, 369.5)):
    m = (np.hypot(xx - cx * k, yy - cy * k) < 8.5 * k)
    out[m] = out[m] * np.array([0.30, 0.22, 0.22])

# ---- 5. voyant POWER eteint (le sprite allume est decoupe dans la maquette)
LED = (1618.4, 522.5)
r = np.hypot(xx - LED[0] * k, yy - LED[1] * k) / k
# halo : la lumiere orange s'ajoute surtout au rouge ; le bleu garde le metal d'origine -> gris neutre a partir du bleu
zone = (r < 95) & (r >= 15)
neutral = np.stack([out[..., 2] * 0.985, out[..., 2] * 0.99, out[..., 2] * 1.02], -1)
w = np.clip((95 - r) / 10, 0, 1) * zone
glowy = np.clip((out[..., 0] - out[..., 2]) / 0.04, 0, 1)
w = w * glowy
out = out * (1 - w[..., None]) + neutral * w[..., None]
# lentille eteinte
dx = (xx / k - LED[0]) / 16.0; dy = (yy / k - LED[1]) / 16.0
rr = np.hypot(dx, dy)
dome = np.clip((1 - rr) * 16 * 0.6, 0, 1)
shade = np.clip(1 - rr ** 2, 0, 1) ** 0.5
lens = np.stack([0.060 + 0.05 * shade, 0.035 + 0.025 * shade, 0.022 + 0.012 * shade], -1)
spec = np.exp(-(((dx + 0.35) ** 2 + (dy + 0.40) ** 2) / 0.02)) * 0.55
lens = lens + spec[..., None] * np.array([1.0, 0.92, 0.85])
rim = np.exp(-((rr - 1.0) / 0.07) ** 2) * 0.10
out = out * (1 - dome[..., None]) + lens * dome[..., None] + rim[..., None] * np.array([0.9, 0.85, 0.8])

# ---- 6. ancien inverseur retire (remplace par les sprites fournis) : metal pris plus bas, memes colonnes,
#         niveau recale ligne par ligne sur la plaque a 10-18 points du trou (hors ombre de l'inverseur)
TG = (1721.0, 519.0)
rt = np.hypot(xx / k - TG[0], (yy / k - TG[1]) * 1.04)
rs = np.hypot(xx / k - TG[0] - 6, yy / k - TG[1] - 8)
tm = (rt < 58) | (rs < 66) | box(1694, 438, 1754, 522)
tm &= ~box(1690, 405, 1760, 438) & ~box(1680, 596, 1770, 625)
far = nd.binary_dilation(tm, iterations=int(10 * k))
farther = nd.binary_dilation(tm, iterations=int(18 * k))
refz = farther & ~far & ~box(1690, 405, 1760, 438) & ~box(1680, 596, 1770, 625) & (np.hypot(xx / k - 1618.4, yy / k - 522.5) > 24) & box(1560, 380, 1850, 660)
x0, x1 = int(1640 * k), int(1810 * k)
donor = out[int(645 * k):int(735 * k), x0:x1]
dlow = nd.uniform_filter1d(donor, 61, axis=1)
grain = donor - dlow
ys_ = np.nonzero(far.any(1))[0]
lowrow = np.zeros((H, x1 - x0, 3), np.float32)
for y in ys_:
    xs = np.nonzero(refz[y, x0:x1])[0]
    if len(xs) < 2: continue
    lowrow[y] = np.stack([np.interp(np.arange(x1 - x0), xs, out[y, x0 + xs, c]) for c in range(3)], -1)
lowrow = nd.gaussian_filter(lowrow, (6 * k, 6 * k, 0))
for y in ys_:
    m = far[y, x0:x1] & ~refz[y, x0:x1]
    m &= ~box(1690, 405, 1760, 438)[y, x0:x1] & ~box(1680, 596, 1770, 625)[y, x0:x1]
    val = lowrow[y] + grain[(y - ys_[0]) % grain.shape[0]]
    w = np.clip(nd.distance_transform_edt(~tm)[y, x0:x1] / (10 * k), 0, 1)
    row = out[y, x0:x1]
    row[m] = (val * (1 - w[:, None]) + row * w[:, None])[m]
    out[y, x0:x1] = row

# ---- 6b. voyant POWER descendu sur l'axe des potards (y 531) : on deplace le voyant (bague + lentille + ombre)
LED_DY = 531.0 - 522.5
pr_ = 19.5
yy2, xx2 = np.mgrid[0:H, 0:W].astype(np.float32)
r_old = np.hypot(xx2 / k - 1618.4, yy2 / k - 522.5)
bx = (int((1618.4 - pr_ - 10) * k), int((522.5 - pr_ - 10) * k), int((1618.4 + pr_ + 10) * k), int((522.5 + pr_ + 10) * k))
patch = out[bx[1]:bx[3], bx[0]:bx[2]].copy()
pal = np.clip((pr_ - r_old[bx[1]:bx[3], bx[0]:bx[2]]) / 3.0, 0, 1)
hole = r_old < pr_ + 9
out = donor_fill(out, hole, 652, 684)
dy = int(round(LED_DY * k))
reg = out[bx[1] + dy:bx[3] + dy, bx[0]:bx[2]]
ann = (r_old[bx[1]:bx[3], bx[0]:bx[2]] > 16.5) & (pal > 0)
for c_ in range(3):                                   # couronne de plaque autour de la bague : niveau de la nouvelle place
    patch[..., c_] = np.where(ann, patch[..., c_] * reg[..., c_][ann].mean() / max(patch[..., c_][ann].mean(), 1e-4), patch[..., c_])
out[bx[1] + dy:bx[3] + dy, bx[0]:bx[2]] = reg * (1 - pal[..., None]) + patch * pal[..., None]

# ---- 6c. texture de la facade remplacee par assets/design/plate_texture.png ; les anciennes sources de lumiere de la
#          plaque (reflets, degrades) sont retirees, seules les ombres de contact des commandes sont gardees
from PIL import ImageDraw
alpha0 = np.asarray(Image.open(SRC))[..., 3] if Image.open(SRC).mode == "RGBA" else np.full((H, W), 255, np.uint8)
case = nd.binary_fill_holes(alpha0 > 250)
inner = nd.binary_erosion(case, iterations=int(15 * k))
fgi = Image.new("L", (W, H), 0); d_ = ImageDraw.Draw(fgi)
def RR(x0_, y0_, x1_, y1_, r_=0): d_.rounded_rectangle([x0_ * k, y0_ * k, x1_ * k, y1_ * k], radius=r_ * k, fill=255)
def CC(cx_, cy_, r_): d_.ellipse([(cx_ - r_) * k, (cy_ - r_) * k, (cx_ + r_) * k, (cy_ + r_) * k], fill=255)
RR(140, 110, 1695, 305, 14)
for xb in (1707, 1791, 1874): RR(xb, 165, xb + 76, 241, 8)
CC(1913, 75, 39)
RR(46, 306, 137, 745, 12); RR(1861, 306, 1952, 745, 12)
for cxk in (336, 549, 761, 974, 1187, 1401): CC(cxk, 535, 76)
CC(1618.4, 531.0, 17)
RR(1546, 340, 1556, 715, 3)
fgm = np.asarray(fgi) > 0
panel = inner & ~fgm
Lc = out.mean(2)
T = np.asarray(Image.open(os.path.join(DESIGN, "plate_texture.png")).convert("RGB").resize((W, W), Image.LANCZOS), dtype=np.float32)[:H] / 255.0
so_ = (out.max(2) - out.min(2)) / np.maximum(out.max(2), 1e-3)
tophat = Lc - nd.grey_opening(Lc, size=(31, 31))
zone = box(140, 325, 1850, 665)
txt = nd.binary_dilation(panel & zone & (tophat > 0.18) & (Lc > 0.35) & (so_ < 0.2), iterations=3) & panel
base_ = panel & ~txt
def nblur(v, m, s_):
    return nd.gaussian_filter(v * m, s_) / np.maximum(nd.gaussian_filter(m.astype(np.float32), s_), 1e-4)
Ls = nblur(Lc, base_, 6.0)
Mloc = nblur(Lc, base_, 60.0)
S = np.clip(Ls / np.maximum(Mloc, 1e-3), 0.3, 1.0)          # ombres de contact seulement (aucun ancien reflet)
newp = np.clip(T * S[..., None], 0, 1)
oldp = np.stack([nblur(out[..., c], base_, 3.0) for c in range(3)], -1)
ink = np.array([0.86, 0.86, 0.85], np.float32)
a_ = np.clip((Lc - oldp.mean(2)) / np.maximum(ink.mean() - oldp.mean(2), 1e-3), 0, 1) * txt
inkcol = np.clip(np.where(a_[..., None] > 0.02, (out - (1 - a_[..., None]) * oldp) / np.maximum(a_[..., None], 0.02), ink), 0, 1)
newpix = a_[..., None] * inkcol + (1 - a_[..., None]) * newp
# bande de raccord contre les commandes et le biseau : pixels d'origine (aretes, ombres) remis au niveau de la nouvelle texture
Mg = nblur(Lc, base_, 90.0)
Tl = nd.gaussian_filter(T.mean(2), 6.0)
gain_ = np.clip(Tl / np.maximum(Mg, 1e-3), 0.3, 3.0)
dist = nd.distance_transform_edt(panel)
wb = np.clip((16.0 - dist) / 10.0, 0, 1)
bandpix = np.clip(out * gain_[..., None], 0, 1)
newpix = np.where(txt[..., None], newpix, wb[..., None] * bandpix + (1 - wb[..., None]) * newpix)
out = np.where(panel[..., None], newpix, out)
# marges hors masque qui montrent encore l'ancienne plaque (meme niveau, sans couleur) : remises au niveau de la texture
btn = np.zeros((H, W), bool)
for xb in (1707, 1791, 1874): btn |= box(xb + 4, 169, xb + 72, 237)
dpan = nd.distance_transform_edt(~panel)
Z = case & ~panel & (dpan <= 40) & ~btn
pz = np.exp(-((Lc - Mg) / 0.07) ** 2) * np.clip((0.25 - so_) / 0.15, 0, 1) * np.clip((40 - dpan) / 15, 0, 1)
pz = nd.gaussian_filter(pz, 2.0) * Z
out = out * (1 - pz[..., None]) + np.clip(out * gain_[..., None], 0, 1) * pz[..., None]

# ---- 7. logos officiels en blanc
def svg_alpha(name, h_px):
    png = subprocess.run(["rsvg-convert", "-w", "6000", os.path.join(ASSETS, "logos", name)], check=True, capture_output=True).stdout
    a_ = Image.open(io.BytesIO(png)).convert("RGBA").getchannel("A")
    a_ = a_.crop(a_.point(lambda v: 255 if v > 4 else 0).getbbox())
    w_ = int(round(a_.size[0] * h_px / a_.size[1]))
    return np.asarray(a_.resize((w_, int(round(h_px))), Image.LANCZOS), dtype=np.float32) / 255.0
def put_white(m, x0_, y0_, alpha=0.95):
    h_, w_ = m.shape
    reg = out[y0_:y0_ + h_, x0_:x0_ + w_]
    a_ = (m * alpha)[..., None]
    out[y0_:y0_ + h_, x0_:x0_ + w_] = reg * (1 - a_) + np.array([0.97, 0.97, 0.96]) * a_
nh = svg_alpha("NineHands_Logo_White.svg", 30 * 0.65 * k)          # NINE HANDS | DSP : 35 % plus petit qu'a l'origine
put_white(nh, int(1830 * k) - nh.shape[1], int((73 - 30 * 0.65 / 2) * k))
amp = svg_alpha("Ampire_Logo_v003_White.svg", 62 * k)
put_white(amp, int(168 * k), int(688 * k))

# ---- sorties : 3,368 pixels par point logique (fenetre 760 x 317 points)
PX = 2560 / 1999.0                     # pixels de sortie par unite de maquette
def to_img(a_):
    return Image.fromarray((np.clip(a_, 0, 1) * 255 + 0.5).astype(np.uint8))
to_img(out).resize((2560, 1068), Image.LANCZOS).save(os.path.join(ASSETS, "bg.png"), optimize=True)

# voyant allume : decoupe dans la maquette, bord adouci (pose sur le fond, voyant eteint)
LEDB = (1570, 474, 1667, 571)
lit = im[int(LEDB[1] * k):int(LEDB[3] * k), int(LEDB[0] * k):int(LEDB[2] * k)]
hh_, ww_ = lit.shape[:2]
yy_, xx_ = np.mgrid[0:hh_, 0:ww_]
rr_ = np.hypot(xx_ / k + LEDB[0] - 1618.4, yy_ / k + LEDB[1] - 522.5)
# seulement la lumiere (lentille + halo), sans la plaque de la maquette : alpha = exces de lumiere sur la plaque autour
plate_c = np.median(lit[(rr_ > 43) & (rr_ < 48)], axis=0)
excess = np.clip((lit.max(2) - plate_c.max()) / (1.0 - plate_c.max()), 0, 1)
al_ = np.clip(excess * 1.15, 0, 1) * np.clip((47 - rr_) / 6, 0, 1)
al_ = np.where(rr_ < 18, al_, nd.gaussian_filter(al_, 2.5 * k))      # halo lisse (sans le grain de la plaque)
rgb_ = np.clip((lit - (1 - al_[..., None]) * plate_c) / np.maximum(al_[..., None], 1e-3), 0, 1)
rgba = np.dstack([rgb_, al_])
sz = (int(round((LEDB[2] - LEDB[0]) * PX)), int(round((LEDB[3] - LEDB[1]) * PX)))
Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").resize(sz, Image.LANCZOS).save(os.path.join(ASSETS, "led_on.png"))

# inverseur : images fournies (fond transparent), rondelle (rayon 665 px) mise au rayon 46 de la maquette, ombre portee
def toggle(src, washer_c, out_name):
    t = Image.open(os.path.join(DESIGN, src)).convert("RGBA")
    sc = 40.5 * PX / 665.0                    # 25 % plus petit
    t = t.resize((int(t.size[0] * sc), int(t.size[1] * sc)), Image.LANCZOS)
    cx, cy = washer_c[0] * sc, washer_c[1] * sc
    S_ = int(round(170 * PX))                      # carre de 170 unites de maquette autour de l axe (levier entier)
    canvas = Image.new("RGBA", (S_, S_), (0, 0, 0, 0))
    canvas.alpha_composite(t, (int(round(S_ / 2 - cx)), int(round(S_ / 2 - cy))))
    a_ = np.asarray(canvas, dtype=np.float32) / 255.0
    sh = nd.shift(nd.gaussian_filter(a_[..., 3], 4.5 * PX), (4.5 * PX, 2.5 * PX), order=1) * 0.75
    al = a_[..., 3] + sh * (1 - a_[..., 3])
    rgb = a_[..., :3] * a_[..., 3:4] / np.maximum(al[..., None], 1e-4)
    Image.fromarray((np.dstack([rgb, al]) * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(ASSETS, out_name))
toggle("toggle_on_src.png", (1925.0, 1104.0), "toggle_up.png")
toggle("toggle_off_src.png", (1921.5, 1069.0), "toggle_down.png")

# visibilite du reflet de chaque LED des vu-metres (profil releve sur la maquette, irregularite fixe) -> PluginEditor.cpp
pr = np.load(os.path.join(DESIGN, "refl_profile.npy"))
env = nd.gaussian_filter1d(pr[1], 10 * k); env = env / env.max()
rng = np.random.default_rng(19)
vis = []
for i in range(48):
    yc = 714.0 - (i + 0.5) * (714.0 - 390.0) / 48
    vis.append(float(np.interp(yc, pr[0], env)) * (0.12 + 0.88 * rng.random() ** 1.6))
print("kMeterVis[48] = { " + ", ".join("%.3ff" % v for v in vis) + " };")
print("ok")
