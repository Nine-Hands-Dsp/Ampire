#!/usr/bin/env python3
"""
AMPIRE - rendu photo-realiste des visuels (fond, potard, inverseur).
Rendu hors plugin par champs de hauteurs : normales, eclairage (une source en haut a gauche,
lumiere d'appoint, reflets anisotropes du metal usine), ombres douces par lancer de rayons
sur le champ de hauteurs, occlusion ambiante. Images 2x (ecrans HiDPI), sur-echantillonnees 3x.

    python3 tools/render_art.py        -> assets/bg.png, assets/knob.png, assets/toggle_up.png, assets/toggle_down.png

Repere logique : 760 x 392 points (taille « 60 % »), images a 2 pixels par point.
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage as ndi

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")
S = 2                       # pixels par point
W, H = 760, 318
L = np.array([-0.42, -0.52, 0.74]); L /= np.linalg.norm(L)     # vers la lumiere (x droite, y bas, z vers l'oeil)
V = np.array([0.0, 0.0, 1.0])
HV = (L + V) / np.linalg.norm(L + V)
FONT_SEMI = os.path.join(ASSETS, "Inter-SemiBold.otf")
FONT_MED = os.path.join(ASSETS, "Inter-Medium.otf")

# ---- geometrie (points) : identique a PluginEditor.cpp
PLATE = (48, 130, 664, 176)
KNOB_Y, KNOB_R = 220, 26
KNOB_X = [102, 186, 270, 354, 438, 522]
DIVIDER_X = 584
LED = (616, 220)
TOGGLE = (664, 220)
LCD = (12, 48, 634, 72)
LCD_SPLIT = 448                      # filet entre infos de la capture et etat (CPU / latence)
PRESET = (16, 12, 220, 26)
METER_WIN_IN = (12, 130, 28, 176)    # vu-metres verticaux : IN a gauche, OUT a droite de la plaque
METER_WIN_OUT = (720, 130, 28, 176)
METER_IN = (16, 162, 7, 136)       # barres plasma (facon RTW 1019), graduations cote facade
METER_OUT = (737, 162, 7, 136)
NAMES = ["INPUT", "BASS", "MIDDLE", "TREBLE", "OUTPUT", "A2 SIZE"]
SCALES = [("-20", "0", "+20"), ("0", "5", "10"), ("0", "5", "10"), ("0", "5", "10"), ("-40", "0", "+40"), ("LITE", None, "FULL")]



LOGO_SVG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "logos", "Ampire_Logo_v003_White.svg")
LOGO_X, LOGO_H = 74, 24          # logo de facade : bord gauche et hauteur (points logiques)


def ampire_logo(h_px):
    """Rend le SVG officiel a la hauteur demandee, recadre sur son contenu (fond transparent)."""
    import subprocess, tempfile, io
    big = subprocess.run(["rsvg-convert", "-w", "6000", LOGO_SVG], check=True, capture_output=True).stdout
    im = Image.open(io.BytesIO(big)).convert("RGBA")
    im = im.crop(im.getchannel("A").point(lambda v: 255 if v > 4 else 0).getbbox())
    w = int(round(im.size[0] * h_px / im.size[1]))
    return im.resize((w, int(round(h_px))), Image.LANCZOS)


def normals(h):
    gy, gx = np.gradient(h)
    n = np.dstack([-gx, -gy, np.ones_like(h)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)


def soft_shadow(h, samples=10, spread=0.10, steps=48, step=1.0, seed=1):
    """fraction de lumiere recue (1 = eclaire) par lancer de rayons sur le champ de hauteurs"""
    rng = np.random.default_rng(seed)
    hh, ww = h.shape
    yy, xx = np.mgrid[0:hh, 0:ww].astype(np.float32)
    lit = np.zeros_like(h, dtype=np.float32)
    for _ in range(samples):
        d = L + rng.normal(0, spread, 3)
        d[2] = abs(d[2]) + 0.05
        d /= np.linalg.norm(d)
        occ = np.zeros_like(h, dtype=bool)
        for k in range(1, steps + 1):
            t = k * step
            sx = np.clip(xx + d[0] * t, 0, ww - 1)
            sy = np.clip(yy + d[1] * t, 0, hh - 1)
            sh = ndi.map_coordinates(h, [sy, sx], order=1, mode="nearest")
            occ |= sh > h + d[2] * t + 0.05
        lit += ~occ
    return lit / samples


def ambient_occlusion(h, radius):
    blurred = ndi.gaussian_filter(h, radius)
    return np.clip(1.0 - np.maximum(blurred - h, 0) / (radius * 0.9), 0.35, 1.0)


def downsample(img, f):
    hh, ww = img.shape[:2]
    return img.reshape(hh // f, f, ww // f, f, *img.shape[2:]).mean(axis=(1, 3))


def to_rgba(rgb, a):
    out = np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)])
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


# =============================================================================== potard
def render_knob(R=KNOB_R, pad=24, ss=3):
    size = int((R + pad) * 2 * S)
    n = size * ss
    px = S * ss                                      # pixels de rendu par point
    c = n / 2.0
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    dx, dy = (xx - c) / px, (yy - c) / px            # en points
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    Rc, Rb = 0.70 * R, 0.76 * R                      # chapeau, chanfrein
    H_top, H_cap = 9.0, 8.6                         # hauteurs (points)
    h = np.zeros_like(r)
    cap = r <= Rc
    h[cap] = H_cap + 0.35 * (1 - (r[cap] / Rc) ** 2)            # chapeau legerement bombe
    ch = (r > Rc) & (r <= Rb)
    h[ch] = H_cap - (r[ch] - Rc) / (Rb - Rc) * 0.9              # chanfrein poli
    sk = (r > Rb) & (r <= R)
    u = (r[sk] - Rb) / (R - Rb)
    knurl = 0.22 * np.abs(np.sin(th[sk] * 30))                 # moletage (60 stries)
    h[sk] = (H_cap - 0.9) - u ** 1.6 * 2.0 + knurl * (1 - u * 0.3)
    edge = (r > R) & (r <= R + 0.8)
    h[edge] = np.maximum(0, (H_cap - 2.9) * (1 - (r[edge] - R) / 0.8))
    inside = r <= R + 0.8
    h *= px                                           # hauteurs en pixels de rendu
    nrm = normals(h)
    ndl = np.clip((nrm * L).sum(2), 0, 1)
    ndh = np.clip((nrm * HV).sum(2), 0, 1)
    # metal : aluminium microbille sur la jupe, finition tournee (anisotrope) sur le chapeau
    base = np.full_like(r, 0.50)
    base[cap] = 0.58
    base[ch] = 0.70
    grain = ndi.gaussian_filter(np.random.default_rng(3).normal(0, 1, r.shape), 0.6 * ss)
    base += grain * 0.012
    # spirales de tournage : variation fine de luminosite selon le rayon
    base[cap] += 0.018 * np.sin(r[cap] * px * 2.2)
    tangent = np.dstack([-np.sin(th), np.cos(th), np.zeros_like(th)])     # direction des rayures (circulaire)
    tdh = (tangent * HV).sum(2)
    aniso = np.sqrt(np.clip(1 - tdh ** 2, 0, 1)) ** 120               # reflet en noeud papillon
    spec = np.where(cap, 0.55 * aniso + 0.20 * ndh ** 60, 0.0)
    spec = np.where(ch, 0.85 * ndh ** 30, spec)
    spec = np.where(sk, 0.30 * ndh ** 18, spec)
    sky = 0.5 + 0.5 * (-nrm[..., 1])                                     # environnement : ciel en haut
    lightm = 0.20 + 0.85 * ndl
    occl = ambient_occlusion(h, 1.2 * px)
    rgb = (base * (lightm * occl + 0.10 * sky))[..., None] * np.array([1.0, 1.0, 1.02])
    rgb += spec[..., None] * np.array([1.0, 0.99, 0.97])
    # bord sombre de la jupe (fresnel / contact)
    rim = np.clip((r - (R - 0.6)) / 0.6, 0, 1) * inside
    rgb *= (1 - 0.35 * rim)[..., None]
    alpha = np.clip((R + 0.8 - r) * px, 0, 1)
    # ombre portee sur la facade : rayons sur le champ de hauteurs + adoucissement
    shadow_h = np.where(inside, h, 0)
    lit = soft_shadow(downsample(shadow_h, ss), samples=12, spread=0.09, steps=70, step=1.0)
    lit = np.kron(lit, np.ones((ss, ss)))
    lit = ndi.gaussian_filter(lit, 2.0 * ss)
    contact = ndi.gaussian_filter((r <= R + 0.3).astype(float), 2.2 * px)
    sh_alpha = np.clip((1 - lit) * 0.62 + contact * 0.30, 0, 0.85) * (1 - alpha)
    out_rgb = rgb * alpha[..., None]
    out_a = alpha + sh_alpha
    out_rgb = np.where(out_a[..., None] > 0, out_rgb / np.maximum(out_a, 1e-6)[..., None], 0)
    img = to_rgba(downsample(out_rgb, ss), downsample(out_a, ss))
    return img


# =============================================================================== inverseur
def render_toggle(up=True, half=42, ss=3):
    size = int(half * 2 * S)
    n = size * ss
    px = S * ss
    c = n / 2.0
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    dx, dy = (xx - c) / px, (yy - c) / px
    r = np.hypot(dx, dy)
    h = np.zeros_like(r)
    mat = np.zeros_like(r, dtype=int)                 # 1 rondelle, 2 ecrou, 3 levier
    # rondelle
    m = r <= 11.5
    h[m] = 1.2; mat[m] = 1
    # ecrou hexagonal chanfreine
    ang = np.arctan2(dy, dx)
    hexr = 9.0 / np.cos(((ang + np.pi / 6) % (np.pi / 3)) - np.pi / 6)
    m = r <= hexr
    hn = 4.2 - np.clip(r - (hexr - 1.2), 0, None) * 1.6
    h[m] = np.maximum(h[m], hn[m]); mat[m] = 2
    # canon fileté
    m = r <= 5.0
    h[m] = 5.4 + 0.25 * np.cos(r[m] * 7); mat[m] = 2
    # levier : capsule effilee de (0,0) vers (0,-19) ou (0,+19), qui monte vers l'oeil
    sgn = -1 if up else 1
    tip = np.array([0, sgn * 19.0])
    t = np.clip((dy * tip[1]) / (tip @ tip), 0, 1)
    d = np.hypot(dx, dy - t * tip[1])
    rad = 2.9 - 0.7 * t
    zc = 6.5 + 6.0 * t
    m = d <= rad
    hb = zc + np.sqrt(np.clip(rad ** 2 - d ** 2, 0, None))
    sel = m & (hb > h)
    h[sel] = hb[sel]; mat[sel] = 3
    # bout arrondi
    db = np.hypot(dx, dy - tip[1])
    m = db <= 3.4
    hb = 6.5 + 6.0 + np.sqrt(np.clip(3.4 ** 2 - db ** 2, 0, None))
    sel = m & (hb > h)
    h[sel] = hb[sel]; mat[sel] = 3
    h *= px
    nrm = normals(h)
    ndl = np.clip((nrm * L).sum(2), 0, 1)
    ndh = np.clip((nrm * HV).sum(2), 0, 1)
    # chrome : reflet d'environnement (ciel clair en haut, sol sombre) + speculaire serre
    refl_y = -nrm[..., 1]
    env = np.interp(refl_y, [-1, -0.3, 0.1, 0.6, 1], [0.22, 0.30, 0.62, 0.88, 0.95])
    chrome = 0.18 + 0.75 * env * (0.4 + 0.6 * ndl) + 1.0 * ndh ** 80
    nickel = 0.22 + 0.55 * env * (0.5 + 0.5 * ndl) + 0.7 * ndh ** 40
    val = np.where(mat == 3, chrome, np.where(mat == 2, nickel, 0.20 + 0.50 * ndl + 0.4 * ndh ** 30))
    occl = ambient_occlusion(h, 1.0 * px)
    rgb = (val * occl)[..., None] * np.array([1.0, 1.0, 1.02])
    alpha = (mat > 0).astype(float)
    alpha = ndi.gaussian_filter(alpha, 0.5 * ss)
    lit = soft_shadow(downsample(h, ss), samples=12, spread=0.08, steps=70, step=1.0)
    lit = ndi.gaussian_filter(np.kron(lit, np.ones((ss, ss))), 2.0 * ss)
    sh_alpha = np.clip((1 - lit) * 0.6, 0, 0.8) * (1 - alpha)
    out_a = alpha + sh_alpha
    out_rgb = np.where(out_a[..., None] > 0, rgb * alpha[..., None] / np.maximum(out_a, 1e-6)[..., None], 0)
    return to_rgba(downsample(out_rgb, ss), downsample(out_a, ss))


# =============================================================================== fond
def rect_mask(shape, x, y, w, h, rad, scale=S, soft=1.0):
    hh, ww = shape
    yy, xx = np.mgrid[0:hh, 0:ww].astype(np.float64) / scale + 0.5 / scale
    cx, cy = x + w / 2, y + h / 2
    qx = np.abs(xx - cx) - (w / 2 - rad)
    qy = np.abs(yy - cy) - (h / 2 - rad)
    dist = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - rad
    return np.clip(0.5 - dist * scale / soft, 0, 1), dist


def render_background():
    hh, ww = H * S, W * S
    rng = np.random.default_rng(7)
    # ---- chassis : metal laque noir mat, grain fin
    grain = ndi.gaussian_filter(rng.normal(0, 1, (hh, ww)), 0.7)
    yy, xx = np.mgrid[0:hh, 0:ww].astype(np.float64)
    vign = 1 - 0.18 * (((xx / ww - 0.45) ** 2) * 1.2 + ((yy / hh - 0.35) ** 2) * 1.6)
    base = 0.085 * vign + grain * 0.004 + 0.012 * (1 - yy / hh)
    img = np.dstack([base, base * 1.01, base * 1.04])

    def composite(rgb, a):
        nonlocal img
        img = img * (1 - a[..., None]) + rgb * a[..., None]

    # ---- facade : aluminium anodise gris graphite, brossage horizontal
    px_, py_, pw_, ph_ = PLATE
    pm, pd = rect_mask((hh, ww), px_, py_, pw_, ph_, 6)
    shadow = ndi.gaussian_filter(np.roll(np.roll(pm, 6, 0), 3, 1), 7) * 0.75
    composite(np.zeros(3), shadow * (1 - pm))
    brush = ndi.gaussian_filter(rng.normal(0, 1, (hh, ww)), (0.45, 70))
    brush = brush / (np.abs(brush).max() + 1e-9)
    fine = ndi.gaussian_filter(rng.normal(0, 1, (hh, ww)), (0.35, 6)) * 0.35
    py0, ph0 = py_ * S, ph_ * S
    tv = np.clip((yy - py0) / ph0, 0, 1)
    sheen = 0.035 * np.exp(-((xx / ww - 0.30 - tv * 0.15) ** 2) / 0.05)        # large reflet diffus du brossage
    plate = 0.205 - 0.035 * tv + brush * 0.016 + fine * 0.010 + sheen
    # chanfrein : bords hauts/gauches clairs, bas/droits sombres
    bev = np.clip(-pd, 0, 2.0) / 2.0
    inner = 1 - bev
    gx = np.where(np.abs(xx / S - (px_ + pw_ / 2)) > pw_ / 2 - 3, np.sign(xx / S - (px_ + pw_ / 2)), 0)
    gy = np.where(np.abs(yy / S - (py_ + ph_ / 2)) > ph_ / 2 - 3, np.sign(yy / S - (py_ + ph_ / 2)), 0)
    edge_light = (-(gx * L[0] + gy * L[1])) * inner
    plate = plate + edge_light * 0.10
    composite(np.dstack([plate, plate * 1.012, plate * 1.03]), pm)

    # vis BTR (six pans creux) aux coins
    for (sx, sy) in [(px_ + 14, py_ + 14), (px_ + pw_ - 14, py_ + 14), (px_ + 14, py_ + ph_ - 14), (px_ + pw_ - 14, py_ + ph_ - 14)]:
        rr = np.hypot(xx / S - sx, yy / S - sy)
        m = np.clip((4.2 - rr) * S, 0, 1)
        nx_ = (xx / S - sx) / 4.2; ny_ = (yy / S - sy) / 4.2
        shade = 0.30 + 0.25 * np.clip(-(nx_ * L[0] + ny_ * L[1]) * 2, -1, 1)
        composite(np.dstack([shade] * 3) * np.array([1, 1, 1.02]), m * 0.95)
        ang = np.arctan2(yy / S - sy, xx / S - sx)
        hexr = 1.9 / np.cos(((ang) % (np.pi / 3)) - np.pi / 6)
        hm = np.clip((hexr - rr) * S, 0, 1)
        composite(np.full(3, 0.05), hm)

    # ---- fenetres d'affichage en creux (verre fume)
    for (x, y, w, h_) in [LCD, METER_WIN_IN, METER_WIN_OUT]:
        m, d = rect_mask((hh, ww), x, y, w, h_, 5)
        # lèvre exterieure eclairee en bas a droite, ombre interieure en haut a gauche
        ring, _ = rect_mask((hh, ww), x - 1.5, y - 1.5, w + 3, h_ + 3, 6.5)
        composite(np.full(3, 0.16), np.clip(ring - m, 0, 1) * np.clip((yy / S - y) / h_, 0, 1) * 0.8)
        glass = 0.030 + 0.012 * (1 - (yy / S - y) / h_)
        refl = 0.018 * np.clip(1 - np.abs((xx / S - x) - (yy / S - y) * 1.6 - w * 0.22) / min(90, w * 1.2), 0, 1)
        g = glass + refl + grain * 0.002
        inner_sh = ndi.gaussian_filter((1 - m), 5) * m
        g = g * (1 - 0.85 * np.clip(inner_sh * 2.2, 0, 1))
        composite(np.dstack([g, g * 1.02, g * 1.06]), m)

    # ---- champ du preset (creux)
    m, _ = rect_mask((hh, ww), *PRESET, 4)
    inner_sh = ndi.gaussian_filter((1 - m), 3) * m
    g = 0.045 - 0.04 * np.clip(inner_sh * 2, 0, 1)
    composite(np.full(3, 1.0) * g[..., None] if False else np.dstack([g, g, g * 1.05]), m)

    # ---- bagues : voyant
    lx, ly = LED
    rr = np.hypot(xx / S - lx, yy / S - ly)
    m = np.clip((7.5 - rr) * S, 0, 1)
    nx_ = (xx / S - lx) / 7.5; ny_ = (yy / S - ly) / 7.5
    shade = 0.32 + 0.28 * np.clip(-(nx_ * L[0] + ny_ * L[1]) * 2.5, -1, 1)
    composite(np.dstack([shade] * 3), m)
    composite(np.full(3, 0.02), np.clip((5.2 - rr) * S, 0, 1))

    out = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").convert("RGBA")

    # ---- serigraphie (encre blanche legerement diffuse)
    ink = Image.new("RGBA", out.size, (0, 0, 0, 0))
    dr = ImageDraw.Draw(ink)
    f_lab = ImageFont.truetype(FONT_SEMI, 11 * S)
    f_sc = ImageFont.truetype(FONT_MED, int(8.5 * S))
    f_small = ImageFont.truetype(FONT_SEMI, int(8 * S))

    def text(t, cx, cy, font, alpha=225, spacing=1.2):
        # interlettrage elargi : lettres une par une
        widths = [dr.textlength(ch, font=font) for ch in t]
        tw = sum(widths) + spacing * S * (len(t) - 1)
        x = cx * S - tw / 2
        for ch, wch in zip(t, widths):
            dr.text((x, cy * S), ch, font=font, fill=(236, 236, 232, alpha), anchor="lm")
            x += wch + spacing * S

    for i, cx in enumerate(KNOB_X):
        if i != 5:   # nom et extremites du potard de taille A2 : dessines par le plugin (dependent de la capture)
            text(NAMES[i], cx, PLATE[1] + 22, f_lab)
        lo, mid, hi = SCALES[i]
        for t in range(11):
            a = np.radians(-150 + 30 * t)
            r0 = KNOB_R + 7
            x0, y0 = cx + r0 * np.sin(a), KNOB_Y - r0 * np.cos(a)
            if t in (0, 5, 10):
                r1 = r0 + 4
                dr.line([(x0 * S, y0 * S), ((cx + r1 * np.sin(a)) * S, (KNOB_Y - r1 * np.cos(a)) * S)], fill=(236, 236, 232, 210), width=int(1.2 * S))
            else:
                rr_ = 0.9 * S
                dr.ellipse([x0 * S - rr_, y0 * S - rr_, x0 * S + rr_, y0 * S + rr_], fill=(236, 236, 232, 150))
        if i != 5:
            text(lo, cx - 26, KNOB_Y + 40, f_sc, 170, 0.2)
            text(hi, cx + 26, KNOB_Y + 40, f_sc, 170, 0.2)
        if mid:
            text(mid, cx, KNOB_Y - 48, f_sc, 170, 0.4)
    text("POWER", TOGGLE[0], PLATE[1] + 22, f_lab)
    text("ON", TOGGLE[0], KNOB_Y - 38, f_sc, 170, 0.6)
    text("OFF", TOGGLE[0], KNOB_Y + 38, f_sc, 170, 0.6)
    # filet separateur grave
    dr.line([(DIVIDER_X * S, (PLATE[1] + 16) * S), (DIVIDER_X * S, (PLATE[1] + PLATE[3] - 16) * S)], fill=(0, 0, 0, 150), width=S)
    dr.line([(DIVIDER_X * S + S, (PLATE[1] + 16) * S), (DIVIDER_X * S + S, (PLATE[1] + PLATE[3] - 16) * S)], fill=(255, 255, 255, 18), width=S)
    # marque sur la facade, en bas a gauche
    # logo officiel Ampire (SVG fourni, jamais redessine) serigraphie en blanc
    logo = ampire_logo(LOGO_H * S)
    lx, ly = int(LOGO_X * S), int((PLATE[1] + PLATE[3] - 16) * S - logo.size[1] / 2)
    la = np.array(logo).astype(np.float32)
    la[..., :3] = (236, 236, 232)
    la[..., 3] *= 215 / 255
    ink.alpha_composite(Image.fromarray(la.astype(np.uint8)), (lx, ly))
    text("NEURAL AMP MODELER  A1 / A2", PLATE[0] + 204, PLATE[1] + PLATE[3] - 16, f_small, 120, 1.2)
    # libelles des fenetres (sur le chassis)
    f_tiny = ImageFont.truetype(FONT_MED, int(6.0 * S))
    for win, bar, lab, side in ((METER_WIN_IN, METER_IN, "IN", 1), (METER_WIN_OUT, METER_OUT, "OUT", -1)):
        text(lab, win[0] + win[2] / 2, win[1] + 12, f_small, 160, 0.6)
        bx0, by0, bw, bh = bar
        edge = bx0 + bw + 2 if side > 0 else bx0 - 2           # bord de la barre cote graduations
        # echelle RTW non lineaire (meme table que rtwPos dans PluginEditor.cpp)
        def rtw(db):
            D = [-50, -40, -30, -20, -10, 0]; P = [0, 0.10, 0.22, 0.38, 0.64, 1.0]
            return float(np.interp(db, D, P))
        majors = [0, -3, -6, -9, -12, -15, -20, -25, -30, -40, -50]
        minors = [-1, -2, -4, -5, -7, -8, -10, -11, -13, -14, -16, -17, -18, -19, -35, -45]
        for db in majors + minors:
            y = by0 + bh * (1 - rtw(db))
            major = db in majors
            ln = 3.0 if major else 1.6
            colr = (255, 120, 90) if db > -6 or db == 0 else (236, 236, 232)
            dr.line([(edge * S, y * S), ((edge + side * ln) * S, y * S)], fill=colr + ((150 if major else 70),), width=max(1, int(0.6 * S)))
            if major and db not in (-3, -9, -15, -25):
                tx = edge + side * 4.2
                dr.text((tx * S, y * S), str(abs(db)), font=f_tiny, fill=colr + (150,), anchor="lm" if side > 0 else "rm")
    # filet vertical sur le verre de l'ecran (infos | etat)
    dr.line([(LCD_SPLIT * S, (LCD[1] + 12) * S), (LCD_SPLIT * S, (LCD[1] + LCD[3] - 12) * S)], fill=(236, 236, 232, 26), width=S)
    ink = ink.filter(ImageFilter.GaussianBlur(0.35 * S / 2))
    out = Image.alpha_composite(out, ink)
    return out.convert("RGB")


if __name__ == "__main__":
    os.makedirs(ASSETS, exist_ok=True)
    render_knob().save(os.path.join(ASSETS, "knob.png"))
    render_toggle(True).save(os.path.join(ASSETS, "toggle_up.png"))
    render_toggle(False).save(os.path.join(ASSETS, "toggle_down.png"))
    render_background().save(os.path.join(ASSETS, "bg.png"))
    print("ok")
