"""Make the Android adaptive launcher icon from the port's own icon.

The original art (src/snap64.icns, its 512 px image) is left as it is. Its
purple is fitted as a smooth top-to-bottom gradient; the art is lifted off
that purple (color to alpha against the fitted background, so the art over
the gradient composites back to the original within a level or two); the
burst's rays, which the original's frame cuts, fade out instead of ending in
a straight edge; the art is placed at 66 dp, inside the adaptive icon's
safe circle, on the 108 dp canvas. The monochrome layer (Android 13 themed
icons) is the "Snap64" lettering. Needs numpy and Pillow (with WebP).

usage: python make_android_icon.py <path to src/snap64.icns> <output res dir>
"""
import sys, os, io, base64, hashlib, struct
import numpy as np
from PIL import Image

ICNS_SHA256 = "5ecd6efc157f0f02cc1c68d24d77c90dc88023ab12ffa44d3571b970880faa02"
S_DP = 66.0
DENS = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
X0, X1 = 15, 497   # inside the original's frame (14 px at 512) and its 1 px bevel
MONO_MASK = "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"


def read_ic09(path):
    d = open(path, "rb").read()
    if hashlib.sha256(d).hexdigest() != ICNS_SHA256:
        sys.exit("snap64.icns is not the file this was made for; stopping")
    off = 8
    while off < len(d):
        t = d[off:off+4]; n = struct.unpack(">I", d[off+4:off+8])[0]
        if t == b"ic09":
            return Image.open(io.BytesIO(d[off+8:off+n])).convert("RGB")
        off += n
    sys.exit("no 512 px image in snap64.icns")


def fit_background(src):
    rows = np.arange(16, 496)
    L = np.array([np.median(src[y, 16:27], 0) for y in rows])
    sd = np.array([src[y, 16:27].std(0).max() for y in rows])
    good = sd < 1.0/255
    A = np.vstack([rows[good]**2, rows[good], np.ones(good.sum())]).T
    return np.array([np.linalg.lstsq(A, L[good, c], rcond=None)[0] for c in range(3)])


def bg_rows(coef, y):
    y = np.asarray(y, float)
    return np.stack([coef[c][0]*y*y + coef[c][1]*y + coef[c][2] for c in range(3)], -1)


def lift_art(src, coef):
    H, W, _ = src.shape
    b = np.broadcast_to(bg_rows(coef, np.arange(H))[:, None, :], (H, W, 3))
    p = src; eps = 1e-6
    a_up = np.where(p > b, (p-b)/np.maximum(1-b, eps), 0)
    a_dn = np.where(p < b, (b-p)/np.maximum(b, eps), 0)
    a = np.clip(np.maximum(a_up, a_dn).max(-1), 0, 1)
    a = np.where(a < 2.5/255, 0, a)
    f = np.clip(np.where(a[..., None] > 0, b + (p-b)/np.maximum(a[..., None], eps), 0), 0, 1)
    inner = np.zeros((H, W)); inner[X0:X1+1, X0:X1+1] = 1
    a = a*inner
    # quantize as the 8-bit master would be, then fade the rays at the frame's cut
    f = np.round(f*255)/255; a = np.round(a*255)/255
    hsv = np.asarray(Image.fromarray((f*255).astype(np.uint8)).convert("HSV")).astype(float)
    hue = hsv[..., 0]*360/255; sat = hsv[..., 1]/255; val = hsv[..., 2]/255
    yellow = (hue > 30) & (hue < 75) & (sat > 0.35) & (val > 0.55)
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.minimum.reduce([xx-X0, X1-xx, yy-X0, X1-yy]).astype(float)
    r = np.clip(d/28.0, 0, 1); r = r*r*(3-2*r)
    a = np.where(yellow, a*r, a)
    return np.dstack([f, a])


def place(rgba, k):
    canvas = int(round(108*k)); ss = 4
    big = canvas*ss; art = int(round(S_DP*k*ss))
    pm = rgba.copy(); pm[..., :3] *= pm[..., 3:4]
    out = np.zeros((big, big, 4)); o = (big-art)//2
    for c in range(4):
        ch = Image.fromarray(pm[..., c].astype(np.float32), "F").resize((art, art), Image.LANCZOS)
        out[o:o+art, o:o+art, c] = np.asarray(ch)
    out = np.clip(out, 0, 1).reshape(canvas, ss, canvas, ss, 4).mean((1, 3))
    a = out[..., 3:4]
    rgb = np.where(a > 1e-6, out[..., :3]/np.maximum(a, 1e-6), 0)
    return Image.fromarray((np.clip(np.dstack([rgb, a[..., 0]]), 0, 1)*255+0.5).astype(np.uint8), "RGBA")


def background_xml(coef):
    def at(c):
        y0 = (c-(108-S_DP)/2)*512/S_DP-0.5
        y = min(max(y0, 14), 498)
        col = bg_rows(coef, [y])[0]
        for lim in (14, 498):
            if (lim == 14 and y0 < 14) or (lim == 498 and y0 > 498):
                col = col + np.array([2*coef[i][0]*lim+coef[i][1] for i in range(3)])*(y0-lim)
        return np.clip(col, 0, 1)
    items = []
    for c in range(0, 109, 12):
        r, g, b = (at(c)*255+0.5).astype(int)
        items.append('                <item android:offset="%.4f" android:color="#FF%02X%02X%02X" />' % (c/108.0, r, g, b))
    return "\n".join([
        '<?xml version="1.0" encoding="utf-8"?>',
        "<!-- The port icon's own purple, top to bottom, carried to the edges of the",
        "     108 dp adaptive icon canvas (the art itself is ic_launcher_foreground). -->",
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
        '    xmlns:aapt="http://schemas.android.com/aapt"',
        '    android:width="108dp"', '    android:height="108dp"',
        '    android:viewportWidth="108"', '    android:viewportHeight="108">',
        '    <path android:pathData="M0,0h108v108h-108z">',
        '        <aapt:attr name="android:fillColor">',
        "            <gradient", '                android:type="linear"',
        '                android:startX="54"', '                android:startY="0"',
        '                android:endX="54"', '                android:endY="108">']
        + items + ["            </gradient>", "        </aapt:attr>", "    </path>", "</vector>", ""])


ADAPTIVE = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />
</adaptive-icon>
"""


def main():
    icns, res = sys.argv[1], sys.argv[2]
    src = np.asarray(read_ic09(icns)).astype(np.float64)/255.0
    coef = fit_background(src)
    art = lift_art(src, coef)
    mask = np.asarray(Image.open(io.BytesIO(base64.b64decode(MONO_MASK))).convert("L")).astype(np.float64)/255.0
    mono = np.dstack([np.ones((512, 512, 3)), mask])
    report = []
    for name, k in DENS.items():
        d = os.path.join(res, "mipmap-"+name); os.makedirs(d, exist_ok=True)
        for layer, rgba in (("foreground", art), ("monochrome", mono)):
            im = place(rgba, k)
            p = os.path.join(d, "ic_launcher_%s.webp" % layer)
            im.save(p, "WEBP", lossless=True, quality=100, method=6)
            back = np.asarray(Image.open(p).convert("RGBA"))
            report.append("%s %s %dx%d pixels %s" % (name, layer, im.size[0], im.size[1], hashlib.sha256(back.tobytes()).hexdigest()[:16]))
    os.makedirs(os.path.join(res, "drawable"), exist_ok=True)
    open(os.path.join(res, "drawable", "ic_launcher_background.xml"), "w", newline="\n").write(background_xml(coef))
    os.makedirs(os.path.join(res, "mipmap-anydpi-v26"), exist_ok=True)
    for n in ("ic_launcher.xml", "ic_launcher_round.xml"):
        open(os.path.join(res, "mipmap-anydpi-v26", n), "w", newline="\n").write(ADAPTIVE)
    print("\n".join(report))


if __name__ == "__main__":
    main()
