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
m |= box(1336, 339, 1466, 367)                 # « A2 SIZE »
m |= box(1312, 617, 1366, 643)                 # « -40 »
m |= box(1438, 617, 1502, 643)                 # « FULL »
out = donor_fill(out, m, 652, 684)

# ---- 3. repere des potards : remplace par le metal du chapeau tourne autour de l'axe
KN = [(331.5, 531), (547.2, 531), (760.7, 531), (974.5, 531), (1187.7, 531), (1402.4, 531)]
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
for cx, cy in KN:
    bar = box(cx - 11, 464, cx + 11, 508) & (L > 0.32)
    bar = nd.binary_dilation(bar, iterations=8) & box(cx - 18, 460, cx + 18, 514)
    X, Y = (xx - cx * k), (yy - cy * k)
    acc = np.zeros_like(out); 
    for ang in (24.0, -24.0, 32.0, -32.0):
        a = np.radians(ang); c, s = np.cos(a), np.sin(a)
        sx = cx * k + X * c - Y * s; sy = cy * k + X * s + Y * c
        for ch in range(3):
            acc[..., ch] += nd.map_coordinates(out[..., ch], [sy, sx], order=1)
    acc /= 4
    # recalage du niveau par couronne : comparer a l'original juste a cote du repere (meme rayon)
    rad = np.hypot(X, Y) / k
    near = box(cx - 30, 460, cx + 30, 516) & ~nd.binary_dilation(bar, iterations=3)
    for r0 in range(0, 70, 3):
        ring_in = bar & (rad >= r0) & (rad < r0 + 3)
        ring_nb = near & (rad >= r0) & (rad < r0 + 3)
        if ring_in.sum() < 3 or ring_nb.sum() < 3: continue
        gain = out[ring_nb].mean(0) / np.maximum(acc[ring_nb].mean(0), 1e-3)
        acc[ring_in] *= np.clip(gain, 0.5, 2.0)
    soft = nd.gaussian_filter(bar.astype(np.float32), 1.2); soft = np.maximum(soft, bar)
    out = out * (1 - soft[..., None]) + acc * soft[..., None]

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
rr_ = np.hypot(xx_ / k + LEDB[0] - 1618.4, yy_ / k + LEDB[1] - 522.5)   # decoupe a l'ancienne place, posee 8,5 plus bas
al_ = np.clip((46 - rr_) / 6, 0, 1)
rgba = np.dstack([lit, al_])
sz = (int(round((LEDB[2] - LEDB[0]) * PX)), int(round((LEDB[3] - LEDB[1]) * PX)))
Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), "RGBA").resize(sz, Image.LANCZOS).save(os.path.join(ASSETS, "led_on.png"))

# inverseur : images fournies (fond transparent), rondelle (rayon 665 px) mise au rayon 46 de la maquette, ombre portee
def toggle(src, washer_c, out_name):
    t = Image.open(os.path.join(DESIGN, src)).convert("RGBA")
    sc = 54.0 * PX / 665.0
    t = t.resize((int(t.size[0] * sc), int(t.size[1] * sc)), Image.LANCZOS)
    cx, cy = washer_c[0] * sc, washer_c[1] * sc
    S_ = int(round(130 * PX))                      # carre de 130 unites de maquette autour de l'axe
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
