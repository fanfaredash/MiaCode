#!/usr/bin/env python3
"""Fabricate the Easy (id 1) and Utage (id 7) difficulty-card assets.

The trackstart prefab card family + LV digit atlas only ship the five standard
maimai colours (Basic..Re:Master == simai difficulty ids 2..6). MiaCode's own
palette (MainWindowShared.cpp difficultyColor) assigns Easy = #69A6FF (blue) and
Utage = #E29A46 (orange). This script derives the two missing colour variants by
a *masked* hue-remap of an existing card (only pixels whose hue is near the source
difficulty hue are shifted, so the constant navy achievement band, black jacket
slot and white footer are preserved), then re-renders the baked difficulty-name
text ("EASY" / "UTAGE").

Bases: Easy <- Expert (red, hue ~351, far from the navy band) ; the digit atlas
Easy <- UI_NUM_MLevel_03 (red). Utage <- Advanced (gold ~44) ; atlas <- _02 (gold).

Difficulty-name text: the prefab names are NOT a simple font+shadow — measured
on MASTER/EXPERT they are a 4-layer build (verified by pixel cross-section):
  fill (near-white) -> inner bright stroke -> a thin DARK outline ring at a small
  stand-off -> soft blurred drop shadow.
They are set in SEGA's FOT-NewRodin Pro UB (matched against the rasterised
glyphs: UB weight, ~0.87 horizontal condense, cap height 28), centred at x~140
and vertically centred in the name strip (y~391). NewRodin is a COMMERCIAL font
and is NOT bundled — point NEWRODIN_UB at a local copy to regenerate; only the
rasterised PNG ships.

Final EASY/UTAGE styling (tuned with the user): the dark outline ring is OMITTED
(DRAW_DARK_RING off) — just a brightened fill + a narrow inner bright stroke
(~4px) + an even, all-around soft drop shadow (zero offset). All layer widths and
the shadow are env-overridable (LVCARD_*) for further tuning.

Run from the repo root:  python tools/lvcard_gen/recolor_difficulty_cards.py
"""
import os
import numpy as np
import scipy.ndimage as ndi
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TST = os.path.join(ROOT, "src", "intro", "assets", "trackstart")
ATLAS_SRC = os.path.join(ROOT, "src", "intro", "assets", "lv_atlas")
# The baked difficulty-name text uses SEGA's FOT-NewRodin Pro UB — the same font
# family the prefab cards use (matched against the rasterised "MASTER" glyphs:
# UB weight, ~0.87 horizontal condense, cap height 28). It is a commercial font,
# so it is NOT bundled — only the rasterised pixels ship. Point NEWRODIN at a
# local copy (e.g. extracted under build/fonts_ref/) to regenerate. Falls back to
# Resource Han Rounded if absent (visibly off — see project note).
NEWRODIN = os.environ.get(
    "NEWRODIN_UB",
    os.path.join(ROOT, "build", "fonts_ref", "FOT-NewRodin Pro UB.otf"))
FONT_FALLBACK = os.path.join(ROOT, "src", "intro", "assets", "fonts", "ResourceHanRoundedCN-Heavy.ttf")
NAME_FONT = NEWRODIN if os.path.exists(NEWRODIN) else FONT_FALLBACK
PREVIEW = os.path.join(ROOT, "build", "lvcard_preview")
os.makedirs(PREVIEW, exist_ok=True)

# Baked difficulty-name geometry, measured from UI_TST_MBase_*.png (420x636):
# names are CENTER-aligned at x~140, baseline y~403, cap height ~28, fill a near
# white #FDEFFF, a ~2px darker-difficulty-colour outline, and a soft drop shadow.
NAME_CENTER_X = 140
NAME_CENTER_Y = 391    # vertical centre of the name strip (jacket bottom 362 .. navy top 421)
NAME_CAP_H = 28
NAME_CONDENSE = 0.87   # NewRodin UB natural width -> maimai's condensed width


# ---- vectorised HSV ----------------------------------------------------------
def rgb_to_hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = np.max(rgb, axis=-1)
    mn = np.min(rgb, axis=-1)
    d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-9
    rmax = (mx == r) & nz
    gmax = (mx == g) & nz & ~rmax
    bmax = (mx == b) & nz & ~rmax & ~gmax
    h[rmax] = (((g - b)[rmax] / d[rmax]) % 6)
    h[gmax] = (((b - r)[gmax] / d[gmax]) + 2)
    h[bmax] = (((r - g)[bmax] / d[bmax]) + 4)
    h = h / 6.0
    s = np.where(mx > 0, d / np.where(mx > 0, mx, 1), 0)
    return np.stack([h, s, mx], axis=-1)


def hsv_to_rgb(hsv):
    h = (hsv[..., 0] % 1.0) * 6.0
    s, v = hsv[..., 1], hsv[..., 2]
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p = v * (1 - s)
    q = v * (1 - f * s)
    t = v * (1 - (1 - f) * s)
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], axis=-1)


def remap_hue(img, target_h, src_center, tol=55.0, sat_thresh=0.10,
              sat_scale=1.0, val_scale=1.0):
    """Shift only pixels whose hue is within `tol` deg of `src_center` to
    `target_h` (degrees). Saturation/value preserved (optionally scaled)."""
    arr = np.asarray(img.convert("RGBA"), dtype=np.float32) / 255.0
    rgb, a = arr[..., :3], arr[..., 3:4]
    hsv = rgb_to_hsv(rgb)
    h = hsv[..., 0] * 360.0
    s, v = hsv[..., 1], hsv[..., 2]
    dh = np.abs(((h - src_center + 180) % 360) - 180)
    mask = (dh <= tol) & (s >= sat_thresh)
    h2 = h.copy(); h2[mask] = target_h
    s2 = s.copy(); s2[mask] = np.clip(s[mask] * sat_scale, 0, 1)
    v2 = v.copy(); v2[mask] = np.clip(v[mask] * val_scale, 0, 1)
    out = hsv_to_rgb(np.stack([h2 / 360.0, s2, v2], axis=-1))
    out = np.concatenate([out, a], axis=-1)
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


# ---- per-difficulty spec -----------------------------------------------------
# target hue derived from the MiaCode palette hex; outline/shadow/fill tuned to
# echo the prefab's white-tinted fill + medium colour outline + soft shadow.
# `name` carries the 4-layer baked-text palette measured from the prefab cards
# (fill -> inner bright stroke -> dark outline ring -> blurred drop shadow), the
# structure verified on MASTER/EXPERT. Tones are hue-consistent with each body.
SPECS = {
    "EASY": dict(
        base="EXP", atlas_base="UI_NUM_MLevel_03",
        target_h=215.6, src_center=351.0, tol=52.0,        # red -> blue
        text="EASY",
        name=dict(fill=(248, 251, 255), bright=(120, 180, 255),
                  dark=(28, 78, 180), shadow=(16, 44, 110)),
    ),
    "UTAGE": dict(
        base="ADV", atlas_base="UI_NUM_MLevel_02",
        target_h=32.3, src_center=44.0, tol=38.0,          # gold -> orange
        text="UTAGE",
        sat_scale=0.92,
        name=dict(fill=(255, 251, 243), bright=(255, 190, 110),
                  dark=(176, 86, 8), shadow=(64, 26, 0)),
    ),
}

# baked-name text band to clear before re-lettering. Starts just BELOW the black
# jacket slot (y362) so it never paints over the slot, and is wide enough to wipe
# the full footprint of the longest source label ("ADVANCED", x43..246) incl. its
# left/right glow + shadow. (x0, y0, x1, y1)
TEXT_BAND = (20, 363, 402, 418)
SS = 4   # supersample factor for crisp condensed text

# Concentric stroke radii (1x logical px, measured on MASTER/EXPERT):
# fill -> inner bright (0..WI) -> dark ring (WI..WD) -> outer bright glow (WD..WO).
W_INNER = float(os.environ.get("LVCARD_W_INNER", "4.0"))
W_DARK = 5.0
W_OUTER = float(os.environ.get("LVCARD_W_OUTER", "4.0"))
GLOW_BLUR = float(os.environ.get("LVCARD_GLOW_BLUR", "0.8"))   # 1x px feather of outer glow
SHADOW_OFFSET = tuple(int(v) for v in os.environ.get("LVCARD_SHADOW_OFF", "0,0").split(","))
SHADOW_BLUR = float(os.environ.get("LVCARD_SHADOW_BLUR", "2.4"))
SHADOW_ALPHA = float(os.environ.get("LVCARD_SHADOW_ALPHA", "0.62"))
# Set LVCARD_DARK_RING=1 to add the thin dark outline ring (off by final design).
DRAW_DARK_RING = os.environ.get("LVCARD_DARK_RING", "0") != "0"


def body_color(frame_img):
    arr = np.asarray(frame_img)
    return tuple(int(c) for c in arr[370, 30, :4])


def _size_for_cap(path, target):
    """Pixel size whose 'M' cap height == target (binary search)."""
    lo, hi = 8, 400
    for _ in range(34):
        mid = (lo + hi) / 2
        f = ImageFont.truetype(path, max(1, int(round(mid))))
        im = Image.new("L", (400, 500), 0)
        ImageDraw.Draw(im).text((10, 10), "M", font=f, fill=255)
        ys = np.where(np.asarray(im).any(1))[0]
        ch = (ys.max() - ys.min() + 1) if len(ys) else 0
        if ch < target:
            lo = mid
        else:
            hi = mid
    return max(1, int(round((lo + hi) / 2)))


def _glyph_mask(text):
    """Antialiased glyph coverage (0..1) for `text` in NewRodin UB, rendered at
    SS x cap height, tightly cropped."""
    font = ImageFont.truetype(NAME_FONT, _size_for_cap(NAME_FONT, NAME_CAP_H * SS))
    pad = 14 * SS
    layer = Image.new("L", (2000, NAME_CAP_H * SS + 2 * pad), 0)
    ImageDraw.Draw(layer).text((pad, pad), text, font=font, fill=255)
    a = np.asarray(layer).astype(np.float32) / 255.0
    ys = np.where(a.any(1))[0]
    xs = np.where(a.any(0))[0]
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def _over(dst, src):
    """Alpha-composite src over dst; both float (H,W,4), straight alpha."""
    sa = src[..., 3:4]
    da = dst[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = (src[..., :3] * sa + dst[..., :3] * da * (1 - sa)) / np.clip(oa, 1e-6, None)
    return np.concatenate([rgb, oa], axis=-1)


def _solid(shape, rgb, alpha):
    out = np.zeros(shape + (4,), np.float32)
    out[..., 0], out[..., 1], out[..., 2] = (c / 255.0 for c in rgb)
    out[..., 3] = alpha
    return out


def _render_name_layer(text, tones):
    """Build the 4-layer difficulty name as an RGBA float image at SS scale:
    outer bright glow + dark outline ring + inner bright + gradient fill.
    Returns (canvas, fill_top_ss, fill_bot_ss) so the caller can baseline-align."""
    g = _glyph_mask(text)                  # tight 0..1 coverage
    pad = int(np.ceil(W_OUTER * SS)) + 4
    mask = np.pad(g, pad)
    fill_top, fill_bot = pad, pad + g.shape[0]
    inside = mask > 0.5
    dist = ndi.distance_transform_edt(~inside)   # 0 inside, grows outside (SS px)

    def disk(R):
        d = np.clip((R * SS + 0.5) - dist, 0.0, 1.0)
        return np.maximum(d, mask)

    H, W = mask.shape
    canvas = np.zeros((H, W, 4), np.float32)
    # soft outer bright glow (blurred ring) for the halo the prefab shows
    glow = _solid((H, W), tones["bright"], disk(W_OUTER))
    glow[..., 3] = ndi.gaussian_filter(glow[..., 3], GLOW_BLUR * SS) * 0.9
    canvas = _over(canvas, glow)
    # outermost crisp layers first so inner layers overpaint -> dark ring remains
    canvas = _over(canvas, _solid((H, W), tones["bright"], disk(W_OUTER)))
    if DRAW_DARK_RING:
        canvas = _over(canvas, _solid((H, W), tones["dark"], disk(W_DARK)))
    canvas = _over(canvas, _solid((H, W), tones["bright"], disk(W_INNER)))
    # fill with a subtle vertical gradient (brighter top -> faint tint bottom)
    fill = _solid((H, W), tones["fill"], mask)
    grad = np.ones(H, np.float32)
    grad[fill_top:fill_bot] = np.linspace(1.0, 0.96, fill_bot - fill_top)
    fill[..., :3] *= grad[:, None, None]
    canvas = _over(canvas, fill)
    return canvas, fill_top, fill_bot


def _to_image(arr):
    return Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def draw_name(frame_img, spec):
    img = frame_img.copy()
    # Erase the old baked label by refilling the band row-by-row with the clean
    # body colour sampled from the left margin (x24..40 is body for every row:
    # left of even the widest label). Per-row sampling follows the body's vertical
    # gradient so no flat-fill seam shows.
    arr = np.asarray(img).copy()
    x0, y0, x1, y1 = TEXT_BAND
    for yy in range(y0, y1):
        col = np.median(arr[yy, 24:40, :3], axis=0)
        arr[yy, x0:x1, :3] = col.astype(np.uint8)
        arr[yy, x0:x1, 3] = 255
    img = Image.fromarray(arr, "RGBA")
    tones = spec["name"]

    layer, ftop_ss, fbot_ss = _render_name_layer(spec["text"], tones)
    word = _to_image(layer)
    # condense horizontally + downsample SS -> 1x in one resample
    fw = max(1, int(round(word.width * NAME_CONDENSE / SS)))
    fh = max(1, int(round(word.height / SS)))
    word = word.resize((fw, fh), Image.LANCZOS)

    # vertically centre the cap block on the name strip; centre horizontally
    x = NAME_CENTER_X - fw // 2
    fill_cy = (ftop_ss + fbot_ss) / 2 / SS
    y = int(round(NAME_CENTER_Y - fill_cy))

    # drop shadow: dark silhouette of the whole word, blurred + offset
    sil = np.zeros_like(layer)
    a = layer[..., 3]
    sil[..., 0], sil[..., 1], sil[..., 2] = (c / 255.0 for c in tones["shadow"])
    sil[..., 3] = a
    sh = _to_image(sil).resize((fw, fh), Image.LANCZOS)
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    shadow.alpha_composite(sh, (x + SHADOW_OFFSET[0], y + SHADOW_OFFSET[1]))
    shadow = shadow.filter(ImageFilter.GaussianBlur(SHADOW_BLUR))
    sa = np.asarray(shadow).astype(np.float32)
    sa[..., 3] *= SHADOW_ALPHA
    img = Image.alpha_composite(img, Image.fromarray(sa.astype(np.uint8), "RGBA"))

    img.alpha_composite(word, (x, y))
    return img


def gen(key):
    spec = SPECS[key]
    b = spec["base"]
    sc, th, tol = spec["src_center"], spec["target_h"], spec["tol"]
    ss = spec.get("sat_scale", 1.0)

    def rm(img):
        return remap_hue(img, th, sc, tol=tol, sat_scale=ss)

    # frame (recolour, then re-letter)
    frame = rm(Image.open(os.path.join(TST, f"UI_TST_MBase_{b}.png")))
    frame = draw_name(frame, spec)
    frame.save(os.path.join(TST, f"UI_TST_MBase_{key}.png"))
    # tab + pill (recolour only)
    rm(Image.open(os.path.join(TST, f"UI_TST_MBase_{b}_Tab.png"))).save(
        os.path.join(TST, f"UI_TST_MBase_{key}_Tab.png"))
    rm(Image.open(os.path.join(TST, f"UI_TST_MBase_LV_{b}.png"))).save(
        os.path.join(TST, f"UI_TST_MBase_LV_{key}.png"))
    # digit atlas (recolour; wide tol since the sheet is single-hue + white)
    atlas = remap_hue(Image.open(os.path.join(ATLAS_SRC, spec["atlas_base"] + ".png")),
                      th, sc, tol=80.0, sat_scale=ss)
    out_atlas = f"UI_NUM_MLevel_{key}.png"
    atlas.save(os.path.join(ATLAS_SRC, out_atlas))

    # preview composite: frame + tab + pill
    comp = Image.new("RGBA", (420, 732), (40, 40, 48, 255))
    comp.alpha_composite(frame, (0, 96))
    comp.alpha_composite(Image.open(os.path.join(TST, f"UI_TST_MBase_{key}_Tab.png")), (0, 60))
    comp.alpha_composite(Image.open(os.path.join(TST, f"UI_TST_MBase_LV_{key}.png")), (223, 96 + 307))
    comp.save(os.path.join(PREVIEW, f"card_{key}.png"))
    atlas.convert("RGBA").save(os.path.join(PREVIEW, f"atlas_{key}.png"))
    print("generated", key)


if __name__ == "__main__":
    for k in SPECS:
        gen(k)
    print("done ->", TST)
