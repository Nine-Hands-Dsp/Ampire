# Ampire - facade v005 (repere 3600 x 1440 de Ampire_Design_Mockup_v005.svg) : images fixes du plugin
#   python3 tools/design_v005.py  ->  assets/bg.png (fond 2560 x 1024 : boitier, bevel, ecran et fenetres vides, boutons et
#   icones, centres fixes et ombres des potards, trous des diodes, logos), knob_crown.png / knob_reflect.png /
#   knob_indicator.png (potard : couronne mobile, reflet fixe, repere mobile), switch_up.png / switch_down.png,
#   diode_on.png / diode_off.png ; et assets/design/v005/textes_points_logos.svg, icone_*.svg (vectoriel).
# Le plugin dessine par-dessus tous les textes de la facade, les points, les LEDs, les potards, inverseurs et diodes.
# lumiere : en haut a gauche, 45 degres ; ombres portees vers le bas a droite
import os, re, subprocess, math
import numpy as np
from PIL import Image
import scipy.ndimage as nd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DES = os.path.join(ROOT, "assets", "design"); V5 = os.path.join(DES, "v005")
ASSETS = os.path.join(ROOT, "assets")
HERE = V5
OUT = V5; os.makedirs(OUT + "/icones", exist_ok=True)
W, H = 3600, 1440
F = {
    "tex": DES + "/plate_texture.png", "diode": V5 + "/diode_rouge.png",
    "kcenter": V5 + "/knob_center_fixed.png", "kcrown": V5 + "/knob_crown_rotating.png", "krefl": V5 + "/knob_reflect_static.png",
    "kind": V5 + "/knob_indicator.png", "swoff": DES + "/toggle_off_src.png", "swon": DES + "/toggle_on_src.png",
    "logoA": V5 + "/Ampire_LogoPP_v005_White.svg", "logoN": V5 + "/NineHands_LogoPP_v003_FullWhite.svg",
}
f32 = np.float32
yy, xx = np.mgrid[0:H, 0:W].astype(f32)

# ---------------------------------------------------------------- lumiere
Ld = np.array([-1.0, -1.0, math.sqrt(2.0)]); Ld /= np.linalg.norm(Ld)          # azimut haut-gauche, elevation 45 deg
Hv = Ld + np.array([0, 0, 1.0]); Hv /= np.linalg.norm(Hv)
def shade(nx, ny, nz, amb=0.35, kd=0.65, ks=0.0, p=30.0):
    nl = np.clip(nx * Ld[0] + ny * Ld[1] + nz * Ld[2], 0, 1)
    f = (amb + kd * nl) / (amb + kd * Ld[2])
    sp = ks * np.clip(np.clip(nx * Hv[0] + ny * Hv[1] + nz * Hv[2], 0, 1) ** p - Hv[2] ** p, 0, 1) if ks else 0   # speculaire relatif a la face plane
    return f, sp

def rr_sdf(x0, y0, w, h, r, X=xx, Y=yy):
    cx, cy = x0 + w / 2, y0 + h / 2
    qx = np.abs(X - cx) - (w / 2 - r); qy = np.abs(Y - cy) - (h / 2 - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
def circ_sdf(cx, cy, r, X=xx, Y=yy): return np.hypot(X - cx, Y - cy) - r
def grad(d):
    gy, gx = np.gradient(d); n = np.maximum(np.hypot(gx, gy), 1e-6); return gx / n, gy / n
def over(dst, rgb, a):                        # composition "par-dessus" (rgb non premultiplie)
    a = a[..., None]; dst[..., :3] = dst[..., :3] * (1 - a) + rgb * a
def blur(a, s): return nd.gaussian_filter(a, s)
def shift(a, dx, dy): return nd.shift(a, (dy, dx), order=1, mode="constant", cval=0.0)
def load(p): return np.asarray(Image.open(p).convert("RGBA"), dtype=f32) / 255.0
def save(a, p):
    Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(p)
def svg_raster(svg_text, w, h):
    import tempfile; tmp = os.path.join(tempfile.gettempdir(), "ampire_v005_tmp.svg"); open(tmp, "w").write(svg_text)
    subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h), tmp, "-o", tmp + ".png"], check=True)
    return load(tmp + ".png")

# ---------------------------------------------------------------- 1. boitier : alu brosse noir satine + bevel rond 24 px
tex = np.asarray(Image.open(F["tex"]).convert("RGB"), dtype=f32)[1000:1000 + H, 200:200 + W] / 255.0
lightgrad = 1.10 - 0.16 * (xx / W) * 0.6 - 0.16 * (yy / H) * 0.4                 # eclairage d'ensemble haut-gauche
base = tex * lightgrad[..., None]
sd_out = rr_sdf(0, 0, W, H, 43.54)
din = -sd_out                                                                   # distance vers l'interieur
BEV = 24.0
u = np.clip(din / BEV, 1e-3, 1)
slope = np.where(din < BEV, (1 - u) / np.sqrt(np.maximum(1 - (1 - u) ** 2, 1e-4)), 0)
slope = np.minimum(slope, 6.0)
gx, gy = grad(din)
nx, ny, nz = -slope * gx, -slope * gy, np.ones_like(slope); nn = np.sqrt(nx * nx + ny * ny + nz * nz)
fb, spb = shade(nx / nn, ny / nn, nz / nn, amb=0.10, kd=0.90, ks=1.0, p=9)
img = np.zeros((H, W, 4), f32)
img[..., :3] = base * fb[..., None] + spb[..., None] * np.array([0.85, 0.86, 0.88]) * (0.30 + 1.6 * base.mean(2, keepdims=True))
# fine arete de lumiere en sommet du bevel et filet sombre a sa base
img[..., :3] *= (1 - 0.12 * np.exp(-((din - BEV) / 1.5) ** 2))[..., None]
img[..., :3] += (0.10 * np.exp(-((din - 2.0) / 1.2) ** 2) * np.clip(-(gx + gy) * 0.7071, 0, 1))[..., None]   # arete exterieure eclairee
img[..., 3] = np.clip(din + 0.5, 0, 1)

# ---------------------------------------------------------------- 2. renfoncements : ecran (A5A7AA) et fenetres des vu-metres (58595B)
def recess(x0, y0, w, h, r, glass_fn, wall=9.0, depth=1.6, shadow=14.0):
    pad = 40
    X0, Y0, X1, Y1 = int(x0 - pad), int(y0 - pad), int(x0 + w + pad), int(y0 + h + pad)
    X, Y = xx[Y0:Y1, X0:X1], yy[Y0:Y1, X0:X1]
    d = -rr_sdf(x0, y0, w, h, r, X, Y)                                          # >0 dedans
    uu = np.clip(d / wall, 0, 1)
    s = np.where((d > 0) & (d < wall), depth * np.sin(np.pi * uu) * 1.2, 0)
    gx_, gy_ = grad(d)
    nx_, ny_ = s * gx_, s * gy_                                                 # paroi concave : normale vers l'interieur
    nn_ = np.sqrt(nx_ ** 2 + ny_ ** 2 + 1)
    f_, sp_ = shade(nx_ / nn_, ny_ / nn_, 1 / nn_, ks=0.35, p=14)
    sub = img[Y0:Y1, X0:X1]
    wallm = np.clip(d + 0.5, 0, 1) * (1 - np.clip(d - wall + 0.5, 0, 1))
    lit = sub[..., :3] * f_[..., None] * 0.80 + sp_[..., None] * 0.5
    sub[..., :3] = sub[..., :3] * (1 - wallm[..., None]) + lit * wallm[..., None]
    # vitre / plastique
    gm = np.clip(d - wall + 0.5, 0, 1)
    gl = glass_fn(X, Y, d - wall)
    # ombre portee du rebord haut-gauche sur la vitre (et occlusion le long des bords)
    outside = (d < wall).astype(f32)
    sh = np.clip(blur(shift(outside, shadow * 0.6, shadow * 0.6), shadow * 0.55), 0, 1)
    ao = np.clip(blur(outside, 3.0), 0, 1)
    gl = gl * (1 - 0.80 * sh[..., None]) * (1 - 0.45 * ao[..., None])
    # rebord bas-droite eclaire : fin liseré clair sur la vitre
    rim = np.exp(-((d - wall) / 1.4) ** 2) * np.clip((gx_ * -0.7071 + gy_ * -0.7071), 0, 1)
    gl = gl + rim[..., None] * 0.18
    sub[..., :3] = sub[..., :3] * (1 - gm[..., None]) + gl * gm[..., None]
    return (X0, Y0, X1, Y1)

def glass(base_rgb, sheen=0.05, line=0.22):
    def fn(X, Y, dd):
        x0, y0 = X.min(), Y.min(); w = X.max() - x0; h = Y.max() - y0
        t = (Y - y0) / h
        g = np.empty(X.shape + (3,), f32)
        col = np.array(base_rgb, f32) / 255.0
        g[:] = col * (1.15 - 0.30 * t)[..., None]
        # reflet diagonal doux (plastique brillant) venant du haut-gauche
        k = ((X - x0) * 0.55 + (Y - y0) * 1.0) / (h * 1.1 + w * 0.55)
        band = np.exp(-((k - 0.18) / 0.10) ** 2) * sheen + np.exp(-((k - 0.42) / 0.05) ** 2) * sheen * 0.45
        g += band[..., None]
        # filet de lumiere sous le bord haut (comme sur rendu_v7)
        ln = np.exp(-((dd - 7.0) / 1.3) ** 2) * line * np.clip(1 - np.abs((X - (x0 + w / 2)) / (w / 2)) ** 6, 0, 1)
        g += ln[..., None] * np.clip((Y - y0) < h * 0.5, 0, 1)[..., None]
        n = np.random.default_rng(3).normal(0, 0.004, X.shape)
        return np.clip(g + n[..., None], 0, 1)
    return fn

recess(58, 212, 3022, 325, 20.13, glass((7, 8, 10), sheen=0.045, line=0.20))
for x0 in (58, 3388):
    recess(x0, 571, 154, 777, 19.25, glass((10, 10, 12), sheen=0.035, line=0.14), wall=8.0, shadow=11)

# vu-metres (FFB000) : 48 LEDs eteintes derriere la vitre ; echelle de la maquette
def meter_pos(db):
    D = [-50, -40, -30, -20, -12, -6, 0]; P = [0, 0.125, 0.248, 0.41, 0.605, 0.79, 1.0]
    return float(np.interp(db, D, P))
MX = {"in": 92, "out": 3458}; MY0, MH, MW, NLED = 671, 609, 50, 48
pitch = MH / NLED
def led_cells(x0, lit_db=None, peak_db=None, colour=(1.0, 0.50, 0.08), sat=(1.0, 0.18, 0.05)):
    layer = np.zeros((H, W, 4), f32)
    for i in range(NLED):
        yb = MY0 + MH - i * pitch; yt = yb - pitch + 3.0
        X0, X1 = x0 + 2, x0 + MW - 2
        sl = (slice(int(yt) - 2, int(yb) + 3), slice(X0 - 2, X1 + 3))
        d = -rr_sdf(X0, yt, X1 - X0, yb - yt, 1.5, xx[sl], yy[sl])
        a = np.clip(d + 0.5, 0, 1)
        f = i / (NLED - 1)
        # fondu : 4 LEDs vers la couleur de saturation, 3 dernieres en saturation
        tsat = np.clip((i - (NLED - 8)) / 4.0, 0, 1)
        c = np.array(colour) * (1 - tsat) + np.array(sat) * tsat
        db_i = np.interp(f, [0, 0.125, 0.248, 0.41, 0.605, 0.79, 1.0], [-50, -40, -30, -20, -12, -6, 0])
        on = (lit_db is not None and db_i <= lit_db) or (peak_db is not None and i == int(round(meter_pos(peak_db) * (NLED - 1))))
        if on:
            col = c * (0.75 + 0.25 * np.clip(d / 3, 0, 1))[..., None]
        else:
            col = c * 0.09 + 0.012
            col = np.broadcast_to(col, d.shape + (3,)) * (0.8 + 0.2 * np.clip(d / 3, 0, 1))[..., None]
        layer[sl][..., :3] = layer[sl][..., :3] * (1 - a[..., None]) + col * a[..., None]
        layer[sl][..., 3] = np.maximum(layer[sl][..., 3], a)
    return layer
# (LEDs eteintes : dessinees par le plugin, a la couleur choisie)

# ---------------------------------------------------------------- 3. boutons poussoirs plastique (45FF00)
BTN = [(3114, "bibliotheque"), (3268, "precedent"), (3422, "suivant")]
BY, BS, BR = 312.5, 120.0, 13.05
for bx, _ in BTN:
    pad = 40
    X0, Y0, X1, Y1 = int(bx - pad), int(BY - pad), int(bx + BS + pad), int(BY + BS + pad)
    X, Y = xx[Y0:Y1, X0:X1], yy[Y0:Y1, X0:X1]
    sub = img[Y0:Y1, X0:X1]
    # trou dans la facade (jeu de 5 px) : paroi + fond sombre
    dh = -rr_sdf(bx - 5, BY - 5, BS + 10, BS + 10, BR + 5, X, Y)
    hole = np.clip(dh + 0.5, 0, 1)
    sub[..., :3] = sub[..., :3] * (1 - hole[..., None]) + np.array([0.012, 0.012, 0.014]) * hole[..., None]
    # capuchon : bevel rond convexe de 14 px, face superieure legerement bombee
    dc = -rr_sdf(bx, BY, BS, BS, BR, X, Y)
    capm = np.clip(dc + 0.5, 0, 1)
    sh = blur(shift(capm, 7, 7), 6.0)                                               # ombre du capuchon dans le trou
    sub[..., :3] *= (1 - 0.85 * sh * hole)[..., None]
    bw = 14.0
    uu = np.clip(dc / bw, 1e-3, 1)
    s = np.where(dc < bw, (1 - uu) / np.sqrt(np.maximum(1 - (1 - uu) ** 2, 1e-4)) * 0.55, 0)
    s = np.minimum(s, 4.0) + 0.06 * (1 - np.clip(dc / (BS / 2), 0, 1))           # legere bosse au centre
    gx_, gy_ = grad(dc)
    nx_, ny_ = -s * gx_, -s * gy_; nn_ = np.sqrt(nx_ ** 2 + ny_ ** 2 + 1)
    f_, sp_ = shade(nx_ / nn_, ny_ / nn_, 1 / nn_, amb=0.45, kd=0.55, ks=0.22, p=10)
    t = (Y - BY) / BS
    plast = np.array([0.20, 0.204, 0.215]) * (1.08 - 0.16 * t)[..., None]
    grain = np.random.default_rng(int(bx)).normal(0, 0.006, X.shape)
    capc = plast * f_[..., None] + sp_[..., None] * 0.55 + grain[..., None]
    # face superieure : reflet mat tres doux (plastique satine type fm1)
    top = np.clip((dc - bw) / 3, 0, 1)
    capc += (top * 0.025 * np.exp(-((t - 0.15) / 0.35) ** 2))[..., None]
    sub[..., :3] = sub[..., :3] * (1 - capm[..., None]) + np.clip(capc, 0, 1) * capm[..., None]

# ---------------------------------------------------------------- 4. icones SVG des boutons (couleur modifiable : currentColor)
ICON = {
 "bibliotheque": '<g fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" stroke-linejoin="round">'
                 '<line x1="34" y1="42" x2="86" y2="42"/><line x1="34" y1="58" x2="64" y2="58"/><line x1="34" y1="74" x2="56" y2="74"/>'
                 '<circle cx="76" cy="74" r="10"/><line x1="83.5" y1="81.5" x2="91" y2="89"/></g>',
 "precedent": '<path d="M78 37 L78 83 L40 60 Z" fill="none" stroke="currentColor" stroke-width="5" stroke-linejoin="round"/>',
 "suivant": '<path d="M42 37 L42 83 L80 60 Z" fill="none" stroke="currentColor" stroke-width="5" stroke-linejoin="round"/>',
}
for n, g in ICON.items():
    open(OUT + "/icones/icone_%s.svg" % n, "w").write(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 120" width="120" height="120" color="#E6E7E8">%s</svg>\n' % g)

# ---------------------------------------------------------------- 5. textes, points, graduations, logos (vectoriel)
SILK = "#F7F7F5"
KX = [600, 1000, 1400, 1800, 2200, 2600]; KY = 950.0; KR = 140.0
NAMES = ["INPUT", "BASS", "MIDDLE", "TREBLE", "OUTPUT", "A2 SIZE"]
SCALES = [("-20", "+20"), ("0", "10"), ("0", "10"), ("0", "10"), ("-40", "+40"), ("LITE", "FULL")]
DOT_R, DOT_RAD = 162.0, 5.5
FS = 44.5            # corps Inter = hauteur de capitale du Myriad Pro 48 px de la maquette
def T(x, y, s, size=FS, anchor="middle", weight=500, ls=0.02):
    return ('<text x="%.2f" y="%.2f" font-family="Inter" font-weight="%d" font-size="%.1f" letter-spacing="%.2fem" '
            'text-anchor="%s">%s</text>' % (x, y, weight, size, ls, anchor, s))
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3600 1440" width="3600" height="1440">',
       '<g id="textes" fill="%s">' % SILK]
BASE = 703.52
for x, n in zip(KX, NAMES): svg.append(T(x, BASE, n))
svg.append(T(2954, BASE, "STACK")); svg.append(T(3174, BASE, "POWER"))
for x in (2954, 3174):
    svg.append(T(x, 900.37, "ON", size=36, ls=0.0)); svg.append(T(x, 1182.78, "OFF", size=36, ls=0.0))
for x, (lo, hi) in zip(KX, SCALES):
    a = math.radians(120)
    svg.append(T(x - DOT_R * math.sin(a), 1088, lo.replace('-', '\u2212'), size=36, weight=500, ls=0.0))
    svg.append(T(x + DOT_R * math.sin(a), 1088, hi.replace('-', '\u2212'), size=36, weight=500, ls=0.0))
svg.append(T(135, 640, "IN", size=40)); svg.append(T(3465, 640, "OUT", size=40))
svg.append('</g><g id="points" fill="%s">' % SILK)
for x in KX:
    for i in range(9):
        a = math.radians(-120 + 30 * i)
        svg.append('<circle cx="%.2f" cy="%.2f" r="%.1f"/>' % (x + DOT_R * math.sin(a), KY - DOT_R * math.cos(a), DOT_RAD))
svg.append('</g><g id="graduations" fill="%s" stroke="none">' % SILK)
TICKS = [0, -6, -12, -20, -30, -40, -50]
for db in TICKS:
    y = MY0 + MH - meter_pos(db) * MH
    lab = "0" if db == 0 else "\u2212%d" % -db
    svg.append('<rect x="145" y="%.2f" width="8" height="2.6"/>' % (y - 1.3))
    svg.append(T(161, y + 8.2, lab, size=23, anchor="start", weight=500, ls=0.0))
    svg.append('<rect x="3447" y="%.2f" width="8" height="2.6"/>' % (y - 1.3))
    svg.append(T(3439, y + 8.2, lab, size=23, anchor="end", weight=500, ls=0.0))
svg.append('</g>')
def logo_paths(p):
    s = open(p).read(); s = s[s.index("<g", s.index("</style>")):s.rindex("</svg>")]
    s = re.sub(r'class="st0"', 'style="display:none"', s)
    return re.sub(r'class="st\d"', '', s)
svg.append('<g id="logo_ampire" fill="#FFFFFF" transform="translate(241.73 1230.44) scale(0.16)">%s</g>' % logo_paths(F["logoA"]))
svg.append('<g id="logo_nine_hands" fill="#FFFFFF" transform="translate(3059.2 28.40) scale(0.1792)">%s</g>' % logo_paths(F["logoN"]))
svg.append('</svg>')
svg_txt = "\n".join(svg)
open(OUT + "/textes_points_logos.svg", "w").write(svg_txt)
# dans le fond : seulement les logos (textes et points : dessines par le plugin, meme rendu partout)
txt = svg_raster(svg_txt[:svg_txt.index('<g id="textes"')] + svg_txt[svg_txt.index('<g id="logo_ampire"'):], W, H)
# serigraphie : tres legere ombre (encre posee sur l'alu) puis encre
ta = txt[..., 3]
ts = blur(shift(ta, 1.6, 1.6), 1.4) * 0.55
img[..., :3] *= (1 - ts)[..., None]
over(img, txt[..., :3], ta * 0.97)

# icones posees sur les boutons
icons_svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 3600 1440" width="3600" height="1440" color="#E6E7E8">']
for bx, n in BTN: icons_svg.append('<g transform="translate(%g %g)">%s</g>' % (bx, BY, ICON[n]))
icons_svg.append('</svg>')
ic = svg_raster("".join(icons_svg), W, H)
img[..., :3] *= (1 - blur(shift(ic[..., 3], 1.2, 1.2), 1.0) * 0.5)[..., None]
over(img, ic[..., :3], ic[..., 3] * 0.92)

# ---------------------------------------------------------------- 6. potards (FF00FF) : r 140 -> calques 800 px mis a 280 px
KS = 280
kc, kcr, kre, kin = (Image.open(F[n]).convert("RGBA") for n in ("kcenter", "kcrown", "krefl", "kind"))
def knob_layer(im, angle=0.0):
    if angle: im = im.rotate(-angle, resample=Image.BICUBIC, center=(400, 400))
    return np.asarray(im.resize((KS, KS), Image.LANCZOS), dtype=f32) / 255.0
L_center, L_refl = knob_layer(kc), knob_layer(kre)
# images du plugin : 256 px pour les 280 unites du potard
for n, im in (("knob_crown", kcr), ("knob_reflect", kre), ("knob_indicator", kin)):
    im.resize((256, 256), Image.LANCZOS).save(os.path.join(ASSETS, n + ".png"))
# ombres portees + occlusion des potards sur la facade
kmask = np.zeros((H, W), f32)
for x in KX: kmask = np.maximum(kmask, np.clip(KR + 0.5 - np.hypot(xx - x, yy - KY), 0, 1))
dropk = blur(shift(kmask, 13, 13), 13) * 0.80
aok = blur(shift(kmask, 3, 3), 4.5) * 0.85
img[..., :3] *= ((1 - dropk) * (1 - aok))[..., None]
def paste(dst, layer, cx, cy):
    h_, w_ = layer.shape[:2]; x0, y0 = int(round(cx - w_ / 2)), int(round(cy - h_ / 2))
    sub = dst[y0:y0 + h_, x0:x0 + w_]
    a = layer[..., 3:4]; sub[..., :3] = sub[..., :3] * (1 - a) + layer[..., :3] * a
    sub[..., 3:4] = np.maximum(sub[..., 3:4], a)
for x in KX: paste(img, L_center, x, KY)

# ---------------------------------------------------------------- 7. diodes (0013FF) : r 34, logees dans un trou avec liseré
dio = Image.open(F["diode"]).convert("RGBA").crop((28, 28, 122, 122)).resize((68, 68), Image.LANCZOS)
dio = np.asarray(dio, dtype=f32) / 255.0
DIO = [(2954, 783.43), (3174, 783.43)]
for cx, cy in DIO:
    sl = (slice(int(cy - 60), int(cy + 60)), slice(int(cx - 60), int(cx + 60)))
    X, Y = xx[sl], yy[sl]; sub = img[sl]
    d = -circ_sdf(cx, cy, 38.5, X, Y)                     # trou
    holem = np.clip(d + 0.5, 0, 1)
    gx_, gy_ = grad(d); uu = np.clip(d / 5, 0, 1)
    s = np.where(d < 5, np.sin(np.pi * uu) * 1.4, 0)
    nx_, ny_ = s * gx_, s * gy_; nn_ = np.sqrt(nx_ ** 2 + ny_ ** 2 + 1)
    f_, sp_ = shade(nx_ / nn_, ny_ / nn_, 1 / nn_, ks=0.5, p=12)
    bez = np.array([0.06, 0.06, 0.065]) * f_[..., None] + sp_[..., None] * 0.6
    sub[..., :3] = sub[..., :3] * (1 - holem[..., None]) + bez * holem[..., None]
    outside = (d < 4.5).astype(f32)
    sub[..., :3] *= (1 - 0.6 * blur(shift(outside, 3, 3), 2.5) * np.clip(d - 3, 0, 1))[..., None]
def diode_sprite(on):
    S = 280; c = S / 2
    sp = np.zeros((S, S, 4), f32)
    lens = dio.copy()
    if not on:
        lum = lens[..., :3].mean(2, keepdims=True)
        lens[..., :3] = np.clip(lens[..., :3] * 0.16 + lum * np.array([0.10, 0.02, 0.02]), 0, 1)
        ys, xs = np.mgrid[0:68, 0:68]
        hl = np.exp(-(((xs - 24) / 9.0) ** 2 + ((ys - 21) / 6.0) ** 2)) * 0.35        # reflet de la lumiere ambiante
        lens[..., :3] = np.clip(lens[..., :3] + hl[..., None], 0, 1)
    else:
        ys, xs = np.mgrid[0:S, 0:S]; rr = np.hypot(xs - c, ys - c)
        glow = (np.exp(-((rr - 30) / 24.0) ** 2) * 0.55 + np.exp(-((rr - 30) / 50.0) ** 2) * 0.18) * (rr > 30) * np.clip((138 - rr) / 30, 0, 1)
        sp[..., :3] = np.array([1.0, 0.16, 0.06]); sp[..., 3] = np.clip(glow, 0, 1)
    x0 = int(c - 34)
    sub = sp[x0:x0 + 68, x0:x0 + 68]; a = lens[..., 3:4]
    sub[..., :3] = sub[..., :3] * (1 - a) + lens[..., :3] * a; sub[..., 3:4] = a + sub[..., 3:4] * (1 - a)
    return sp
D_ON, D_OFF = diode_sprite(True), diode_sprite(False)
save(D_ON, ASSETS + "/diode_on.png"); save(D_OFF, ASSETS + "/diode_off.png")      # 280 x 280 unites

# ---------------------------------------------------------------- 8. inverseurs (FF0000) : rondelle r 665 px (photo) -> r 60
DESIGN_SRC = {"toggle_on_src.png": F["swon"], "toggle_off_src.png": F["swoff"]}
# deux photos (levier haut / bas) calees sur la rondelle : rondelle, ecrou et bague fixes, seul le levier change
def toggle_canvas(src, washer_c):
    t = Image.open(DESIGN_SRC[src]).convert("RGBA")
    sc = 60.0 / 665.0                    # 25 % plus petit
    t = t.resize((int(t.size[0] * sc), int(t.size[1] * sc)), Image.LANCZOS)
    cx, cy = washer_c[0] * sc, washer_c[1] * sc
    S_ = 252                      # carre de 170 unites de maquette autour de l axe (levier entier)
    canvas = Image.new("RGBA", (S_, S_), (0, 0, 0, 0))
    canvas.alpha_composite(t, (int(round(S_ / 2 - cx)), int(round(S_ / 2 - cy))))
    return np.asarray(canvas, dtype=np.float32) / 255.0, sc
# deux photos (levier haut / bas) calees sur la rondelle. Pour que la rondelle et l'ecrou restent fixes, tout ce qui est
# hors de la bande verticale du levier vient de la meme photo (levier haut) ; seule la bande du levier change.
up, scT = toggle_canvas("toggle_on_src.png", (1925.7, 1115.8))      # centre de la rondelle (cercle ajuste) + recalage image a image
dn, _ = toggle_canvas("toggle_off_src.png", (1927.0, 1104.6))
St = up.shape[0]; c0 = St / 2.0; Rw = 673.0 * scT
INNER_R = 0.60                                       # rayon (en fraction de la rondelle) en dessous duquel l'interieur vient de la photo levier bas
dn0 = dn.copy()
dn = np.stack([nd.shift(dn[..., i], (-1.25, -0.5), order=1) for i in range(4)], -1)
dn_top = np.stack([nd.shift(dn0[..., i], (0.5, -0.25), order=1) for i in range(4)], -1)   # pan superieur : recalage propre (perspective)   # recalage fin sur les bagues basses de l ecrou (mesure)
tyy, txx = np.mgrid[0:St, 0:St].astype(np.float32)
tr = np.hypot(txx - c0, tyy - c0)
bandw = np.clip((275 * scT - np.abs(txx - c0)) / (30 * scT), 0, 1)                   # bande du levier
upper = tyy < c0 + 2
# position basse = photo levier haut (rondelle, ecrou, ombre fixes) dont on retire le levier haut :
down = up.copy()
# 1) au-dessus de la rondelle : le levier haut disparait
out_up = upper & (tr > Rw + 2.5) & (bandw > 0)
down[out_up, 3] *= (1 - bandw[out_up])
# 2) couronne de la rondelle sous le levier haut : refaite en tournant autour de l'axe (interpolation en angle)
ring = upper & (tr >= 0.80 * Rw) & (tr <= Rw + 2.5) & (bandw > 0)
NRt, NTt = int(Rw + 5), 1440
rr_t = np.linspace(0, Rw + 5, NRt)[:, None]; tt_t = np.linspace(-np.pi, np.pi, NTt, endpoint=False)[None, :]
pxt = c0 + rr_t * np.sin(tt_t); pyt = c0 - rr_t * np.cos(tt_t)
polt = np.stack([nd.map_coordinates(up[..., i], [pyt, pxt], order=1) for i in range(4)], -1)
pmt = nd.map_coordinates(nd.binary_dilation(ring, iterations=2).astype(np.float32), [pyt, pxt], order=1) > 0.01
tixt = np.arange(NTt)
for r in range(NRt):
    mm = pmt[r]
    if not mm.any() or mm.all(): continue
    gg = ~mm; gxx = np.concatenate([tixt[gg] - NTt, tixt[gg], tixt[gg] + NTt])
    for i in range(4): polt[r, mm, i] = np.interp(tixt[mm], gxx, np.tile(polt[r, gg, i], 3))
ys_, xs_ = np.nonzero(ring)
rq = tr[ys_, xs_] / (Rw + 5) * (NRt - 1); tq = (np.arctan2(xs_ - c0, -(ys_ - c0)) + np.pi) / (2 * np.pi) * NTt
for i in range(4):
    v_ = nd.map_coordinates(np.concatenate([polt[..., i], polt[:, :1, i]], 1), [rq, tq], order=1, mode="nearest")
    down[ys_, xs_, i] = v_
# 3) ecrou et canon (au centre) et tout le levier bas : photo levier bas, calee sur l'ecrou
# seul le levier bas (et le canon central qui s'incline) vient de la photo levier bas : il est detecte comme ce qui differe
# de la photo levier haut dans la moitie basse ; l'ecrou, la rondelle et leurs reflets restent ceux de la photo levier haut
diff = np.abs(dn[..., :3] - up[..., :3]).max(2) * np.maximum(dn[..., 3], up[..., 3])
lever = nd.binary_opening(diff > 0.10, iterations=1) & (bandw > 0) & ~upper
lab_, n_ = nd.label(lever)
if n_:
    sizes = nd.sum(lever, lab_, range(1, n_ + 1)); lever = lab_ == (1 + int(np.argmax(sizes)))
lever = nd.binary_fill_holes(nd.binary_closing(lever, iterations=3))
lever |= ~upper & (tr > Rw + 2.5) & (bandw > 0)                      # levier hors de la rondelle
take = np.clip(nd.gaussian_filter(nd.binary_dilation(lever, iterations=2).astype(np.float32), 1.0), 0, 1)
take = np.maximum(take, np.clip((0.30 * Rw - tr) / (0.06 * Rw), 0, 1))  # canon et soufflet au centre
down = down * (1 - take[..., None]) + dn * take[..., None]
# moitie haute de l'ecrou, cachee par le levier haut :
#  - couronne exterieure (pan de l'hexagone, bagues larges) : ecrou de la photo levier haut, refait en tournant autour de
#    l'axe (interpolation en angle a rayon constant) -> raccord parfait avec le reste de l'ecrou fixe
#  - interieur (bague fine + soufflet) : photo levier bas, ou cette zone est visible telle quelle
hw = 195 * scT
NRh, NTh = int(0.85 * Rw) + 2, 720
rrh = np.linspace(0, 0.85 * Rw, NRh)[:, None]; tth = np.linspace(-np.pi, np.pi, NTh, endpoint=False)[None, :]
pxh = c0 + rrh * np.sin(tth); pyh = c0 - rrh * np.cos(tth)
polh = np.stack([nd.map_coordinates(up[..., i], [pyh, pxh], order=1) for i in range(4)], -1)
pmh = nd.map_coordinates(nd.binary_dilation(upper & (np.abs(txx - c0) < hw), iterations=1).astype(np.float32), [pyh, pxh], order=1) > 0.01
tixh = np.arange(NTh)
for r in range(NRh):
    mm = pmh[r]
    if not mm.any() or mm.all(): continue
    gg = ~mm; gxx = np.concatenate([tixh[gg] - NTh, tixh[gg], tixh[gg] + NTh])
    for i in range(4): polh[r, mm, i] = np.interp(tixh[mm], gxx, np.tile(polh[r, gg, i], 3))
rot = up.copy()
outer = upper & (np.abs(txx - c0) < hw) & (tr < 0.81 * Rw)
# pan superieur de l'hexagone : photo levier bas (ou il est visible), raccordee sans couture a l'ecrou de la photo levier
# haut : on lui ajoute, ligne par ligne, l'ecart up - dn mesure juste a gauche et a droite du levier (collage sans raccord)
xl_, xr_ = int(np.floor(c0 - hw)) - 1, int(np.ceil(c0 + hw)) + 1
for y in np.nonzero(outer.any(1))[0]:
    xs_h = np.nonzero(outer[y])[0]
    dl = (up[y, xl_ - 2:xl_ + 1] - dn_top[y, xl_ - 2:xl_ + 1]).mean(0)
    dr = (up[y, xr_:xr_ + 3] - dn_top[y, xr_:xr_ + 3]).mean(0)
    t_ = (xs_h - xl_) / (xr_ - xl_)
    rot[y, xs_h] = dn_top[y, xs_h] + dl[None] * (1 - t_[:, None]) + dr[None] * t_[:, None]
rot[..., :3] = np.clip(rot[..., :3], 0, 1); rot[..., 3] = 1.0
RI = INNER_R * Rw
inner_w = np.clip((RI - tr) / (0.04 * Rw), 0, 1)                    # interieur : photo levier bas
w_out = nd.gaussian_filter(outer.astype(np.float32), 0.7) * (1 - inner_w)
w_in = inner_w                                                      # disque interieur entier : bague fine et soufflet coherents
down = down * (1 - w_out[..., None]) + rot * w_out[..., None]
down = down * (1 - w_in[..., None]) + dn * w_in[..., None]
# bague grise interieure : elle doit rester EXACTEMENT la meme dans les deux positions (piece fixe). L'interieur de l'ecrou
# en position basse est donc reconstruit ainsi :
#  1) bague et ecrou : photo levier haut ; la partie haute cachee par le levier haut est refaite en tournant autour de
#     l'axe (interpolation en angle a rayon constant : la bague est ronde)
#  2) trou du soufflet : photo levier bas, mise a l'echelle radiale pour que son bord (318) tombe sur celui de la photo
#     levier haut (264)
#  3) levier bas : photo levier bas, pose par-dessus (masque du levier mesure : metal clair dans l'axe, moitie basse)
RIN = 470 * scT                                                   # rayon de la zone interieure refaite (dans l'ecrou)
ang = np.arctan2(txx - c0, -(tyy - c0))
NRb, NTb = int(RIN) + 3, 1440
rrb = np.linspace(0, RIN + 2, NRb)[:, None]; ttb = np.linspace(-np.pi, np.pi, NTb, endpoint=False)[None, :]
pxb = c0 + rrb * np.sin(ttb); pyb = c0 - rrb * np.cos(ttb)
polb = np.stack([nd.map_coordinates(up[..., i], [pyb, pxb], order=1) for i in range(4)], -1)
hid = (upper & (np.abs(txx - c0) < 205 * scT)).astype(np.float32)          # levier haut sur la photo levier haut
pmb = nd.map_coordinates(nd.binary_dilation(hid > 0, iterations=1).astype(np.float32), [pyb, pxb], order=1) > 0.01
tixb = np.arange(NTb)
for r in range(NRb):
    mm = pmb[r]
    if not mm.any() or mm.all(): continue
    gg = ~mm; gxx = np.concatenate([tixb[gg] - NTb, tixb[gg], tixb[gg] + NTb])
    for i in range(4): polb[r, mm, i] = np.interp(tixb[mm], gxx, np.tile(polb[r, gg, i], 3))
ringimg = up.copy()
zin = tr < RIN
ysb, xsb = np.nonzero(zin)
rqb = tr[ysb, xsb] / (RIN + 2) * (NRb - 1); tqb = (ang[ysb, xsb] + np.pi) / (2 * np.pi) * NTb
for i in range(4):
    ringimg[ysb, xsb, i] = nd.map_coordinates(np.concatenate([polb[..., i], polb[:, :1, i]], 1), [rqb, tqb], order=1, mode="nearest")
# 2) trou du soufflet, photo levier bas mise a l'echelle (bord 318 -> 264)
RH_U, RH_D = 262.0 * scT, 314.0 * scT
rd_ = tr * (RH_D / RH_U)
holeimg = np.stack([nd.map_coordinates(dn[..., i], [c0 - rd_ * np.cos(ang), c0 + rd_ * np.sin(ang)], order=1) for i in range(4)], -1)
wh_ = np.clip((RH_U - 40 * scT - tr) / (30 * scT), 0, 1)[..., None]   # bord du trou = celui de la photo levier haut (bague identique)
inner = ringimg * (1 - wh_) + holeimg * wh_
# 3) levier bas : secteur du bas (autour du levier) pris tel quel sur la photo levier bas (levier entier, sans couture ni
#    deformation) ; sur les cotes et en haut, bague et ecrou de la photo levier haut -> bague identique la ou elle se voit
dang = np.abs(np.angle(np.exp(1j * (ang - np.pi))))                 # ecart angulaire avec le bas
sector = np.clip((np.radians(34) - dang) / np.radians(8), 0, 1) * (tyy > c0 - 1)
# pres du pivot le levier depasse le secteur : bande verticale de la largeur du levier (17 px de demi-largeur a la
# rondelle, mesure) pour que ses bords dores restent entiers jusqu'au soufflet
band_l = np.clip((19.5 - np.abs(txx - c0)) / 1.5, 0, 1) * np.clip((tyy - (c0 - 6)) / 2.0, 0, 1)
wl_ = np.maximum(sector, band_l)[..., None]
inner = inner * (1 - wl_) + dn * wl_
wi = np.clip((RIN - tr) / (8 * scT), 0, 1)[..., None]
down = down * (1 - wi) + inner * wi
SW_UP, SW_DN = up, down          # 252 x 252, centre de la rondelle au centre
def sw_shadow(a_):
    # ombre portee 45 deg : rondelle/ecrou fixes, levier qui suit
    al = a_[..., 3]
    sh = shift(blur(al, 5.0), 8, 8) * 0.80 + shift(blur(al, 1.8), 2, 2) * 0.55
    sh = np.clip(sh, 0, 1)
    tot = al + sh * (1 - al)
    rgb = a_[..., :3] * al[..., None] / np.maximum(tot[..., None], 1e-4)
    return np.dstack([rgb, tot])
SWON, SWOFF = sw_shadow(SW_UP), sw_shadow(SW_DN)
save(SWON, ASSETS + "/switch_up.png"); save(SWOFF, ASSETS + "/switch_down.png")    # 252 x 252 unites
SW = [(2954, 1029.71), (3174, 1029.71)]

# ---------------------------------------------------------------- fond du plugin
bg = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").resize((2560, 1024), Image.LANCZOS)
bg.save(os.path.join(ASSETS, "bg.png"), optimize=True)
print("ok")
