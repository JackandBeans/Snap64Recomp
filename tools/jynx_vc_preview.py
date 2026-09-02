#!/usr/bin/env python3
"""Offline check and preview for the opt-in Virtual Console Jynx recolour.

The recolour lives in lib/rt64/src/render/rt64_snap_recolor.h. It is not a
texture operation: Jynx's face and hands carry no texture at all. They are
drawn with the RDP's primitive colour multiplied by the lit shade
(gsDPSetCombineLERP(PRIMITIVE, 0, SHADE, 0, ...) with gsSPTexture(G_OFF)),
and the cartridge's primitive colours for them are #050505 (face) and
#505870 (hands). The renderer swaps those two colours for the purple of
Nintendo's official artwork on the recorded draw call; the shade, and so the
lighting gradient, is the game's own.

This script does three things, all offline, none of which needs the game:

  1. Parses Jynx's display lists from the decomp (assets/cave/jynx) and
     lists every untextured PRIMITIVE*SHADE section with its primitive colour
     and vertex bounding box, then checks that the primitive colours the
     header claims to recognise are exactly the ones those sections use.
  2. Re-implements the header's match rule in Python (a texture-off draw
     whose combiner is that exact LERP and whose primitive colour is one of
     the two cartridge values) and applies it to every section, reporting
     which are recoloured.
  3. Renders the recoloured sections before and after, Gouraud-shaded from
     the model's own vertex normals, to PNGs -- so the result can be looked
     at without a Cave recording, which the port does not have.

The render is a preview, not the game: it uses a single white directional
light with a dim ambient (the N64 lighting model, generic colours), an
orthographic front view of each part in its own bone space, and no fog. The
Cave's real lights, the pose and the fog are the game's. What the preview
does show truthfully is the colour arithmetic: PRIMITIVE times SHADE, before
and after the swap.

Usage (Windows, numpy + Pillow):
  python tools/jynx_vc_preview.py --assets <dir with *.gfx.inc.c and the two
      *.vtx.inc.c files> --out <dir>
The default --assets is the WSL decomp path used during development.
"""
import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_HEADER = os.path.join(HERE, "..", "lib", "rt64", "src", "render", "rt64_snap_recolor.h")

VTX_RE = re.compile(
    r"\{\{\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*\d+,\s*\{\s*-?\d+,\s*-?\d+\s*\},"
    r"\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}\}\}")
PRIM_RE = re.compile(r"gsDPSetPrimColor\(0, 0, (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)\)")
VLOAD_RE = re.compile(r"gsSPVertex\(&(\w+)\[(\d+)\], (\d+), (\d+)\)")
TRI_RE = re.compile(r"gsSP([12])Triangles?\(([^)]*)\)")
COMB_RE = re.compile(r"gsDPSetCombine(LERP|Mode)\(([^)]*)\)")
TEX_RE = re.compile(r"gsSPTexture\([^)]*(G_ON|G_OFF)\)")
RGB8_RE = re.compile(r"constexpr\s+RGB8\s+(\w+)\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")

# The combiner Jynx's untextured parts use, as the gsDPSetCombineLERP
# arguments appear in the decomp. Cycle 0: (PRIMITIVE - 0) * SHADE + 0 for
# colour, (0 - 0) * 0 + PRIMITIVE for alpha. Cycle 1 passes COMBINED through.
JYNX_LERP = "PRIMITIVE,0,SHADE,0,0,0,0,PRIMITIVE,0,0,0,COMBINED,0,0,0,COMBINED"


def load_header_constants(path):
    with open(path) as f:
        text = f.read()
    found = {m.group(1): tuple(int(m.group(i)) for i in (2, 3, 4)) for m in RGB8_RE.finditer(text)}
    for needed in ("CartridgeFace", "CartridgeHands", "ArtworkPurple"):
        if needed not in found:
            sys.exit(f"{path}: no 'constexpr RGB8 {needed}{{r, g, b}}' definition found")
    return found


def load_vtx(path):
    with open(path) as f:
        rows = [tuple(int(v) for v in m.groups()) for m in VTX_RE.finditer(f.read())]
    if not rows:
        sys.exit(f"{path}: no vertices parsed")
    pos = np.array([r[0:3] for r in rows], dtype=np.float64)
    # With lighting on, the colour bytes are the normal as signed bytes.
    nrm = np.array([[(v - 256) if v > 127 else v for v in r[3:6]] for r in rows], dtype=np.float64)
    return pos, nrm


def parse_sections(assets):
    """Every untextured PRIMITIVE*SHADE run of triangles in Jynx's DLs.

    Returns a list of dicts: name, prim (r, g, b, a) or None when the DL
    inherits whatever the previous DL left in the RDP, tris as a list of
    (array, index) triples, and unknown (slots loaded by an earlier DL)."""
    sections = []
    for path in sorted(glob.glob(os.path.join(assets, "*.gfx.inc.c"))):
        name = os.path.basename(path).replace(".gfx.inc.c", "")
        prim = None
        combiner = None
        texon = True
        buf = {}
        cur = None
        with open(path) as f:
            for line in f:
                m = PRIM_RE.search(line)
                if m:
                    prim = tuple(int(x, 16) for x in m.groups())
                    cur = None
                    continue
                m = COMB_RE.search(line)
                if m:
                    combiner = m.group(2).replace(" ", "") if m.group(1) == "LERP" else "MODE:" + m.group(2)
                    cur = None
                    continue
                m = TEX_RE.search(line)
                if m:
                    texon = (m.group(1) == "G_ON")
                    cur = None
                    continue
                m = VLOAD_RE.search(line)
                if m:
                    arr, base, count, dst = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
                    for i in range(count):
                        buf[dst + i] = (arr, base + i)
                    continue
                m = TRI_RE.search(line)
                if not m:
                    continue
                nums = [int(x) for x in m.group(2).split(",")]
                tris = [nums[0:3]] + ([nums[4:7]] if m.group(1) == "2" else [])
                untextured = (not texon) and (combiner == JYNX_LERP)
                if not untextured:
                    cur = None
                    continue
                if cur is None:
                    cur = {"name": name, "prim": prim, "tris": [], "unknown": set()}
                    sections.append(cur)
                for t in tris:
                    if all(i in buf for i in t):
                        cur["tris"].append(tuple(buf[i] for i in t))
                    else:
                        cur["unknown"].update(i for i in t if i not in buf)
    return sections


def matches(prim, texture_on, combiner, consts):
    """The header's rule, restated: texture off, Jynx's exact LERP, and the
    primitive colour equal to one of the two cartridge values with full alpha."""
    if texture_on or combiner != JYNX_LERP or prim is None:
        return False
    return prim[3] == 255 and prim[0:3] in (consts["CartridgeFace"], consts["CartridgeHands"])


def shade(nrm, light_dir, light_col, ambient):
    """N64 vertex lighting for one directional light: ambient + light * max(0, n.l), clamped."""
    n = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-6)
    ndl = np.clip(n @ light_dir, 0.0, None)[:, None]
    return np.clip(ambient[None, :] + light_col[None, :] * ndl, 0.0, 1.0)


def render_section(section, vtx, prim_rgb, size=256, light_dir=(0.35, 0.45, 0.82), light_col=(1.0, 1.0, 1.0), ambient=(38 / 255.0,) * 3):
    """Orthographic front view (+z toward the viewer) of one section in its own bone space."""
    arr_name = section["tris"][0][0][0]
    pos, nrm = vtx[arr_name]
    idx = np.array([[i for (_, i) in tri] for tri in section["tris"]], dtype=np.int64)
    used = np.unique(idx)
    lo = pos[used].min(axis=0)
    hi = pos[used].max(axis=0)
    span = max(hi[0] - lo[0], hi[1] - lo[1], 1.0)
    margin = 0.08
    scale = size * (1.0 - 2 * margin) / span
    cx = (lo[0] + hi[0]) * 0.5
    cy = (lo[1] + hi[1]) * 0.5
    sx = (pos[:, 0] - cx) * scale + size * 0.5
    sy = size * 0.5 - (pos[:, 1] - cy) * scale
    sz = pos[:, 2]
    ld = np.array(light_dir, dtype=np.float64)
    ld /= np.linalg.norm(ld)
    sh = shade(nrm, ld, np.array(light_col), np.array(ambient))
    prim = np.array(prim_rgb, dtype=np.float64) / 255.0
    img = np.zeros((size, size, 3), dtype=np.float64)
    zbuf = np.full((size, size), -np.inf)
    covered = np.zeros((size, size), dtype=bool)
    ys, xs = np.mgrid[0:size, 0:size]
    pxc = xs + 0.5
    pyc = ys + 0.5
    for tri in idx:
        a, b, c = tri
        x0, y0, x1, y1, x2, y2 = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
        det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if abs(det) < 1e-9:
            continue
        w0 = ((x1 - pxc) * (y2 - pyc) - (x2 - pxc) * (y1 - pyc)) / det
        w1 = ((x2 - pxc) * (y0 - pyc) - (x0 - pxc) * (y2 - pyc)) / det
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * sz[a] + w1 * sz[b] + w2 * sz[c]
        front = inside & (z > zbuf)
        s = (w0[..., None] * sh[a] + w1[..., None] * sh[b] + w2[..., None] * sh[c])
        # The combiner: PRIMITIVE * SHADE, per channel.
        col = prim[None, None, :] * s
        img[front] = col[front]
        zbuf[front] = z[front]
        covered |= front
    out = np.full((size, size, 3), 0.45)  # neutral grey ground so a near-black part stays visible
    out[covered] = img[covered]
    return (np.clip(out, 0, 1) * 255.0 + 0.5).astype(np.uint8), int(covered.sum())


def ramp(prim_rgb, width=256, height=24):
    """PRIMITIVE * SHADE for shade 0..255 left to right."""
    s = np.linspace(0.0, 1.0, width)[None, :, None]
    prim = np.array(prim_rgb, dtype=np.float64)[None, None, :] / 255.0
    row = np.repeat(prim * s, height, axis=0)
    return (row * 255.0 + 0.5).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", default=os.path.expanduser("~/pokemonsnap/assets/cave/jynx"),
                    help="directory holding Jynx's *.gfx.inc.c, model.vtx.inc.c and hd_model.vtx.inc.c")
    ap.add_argument("--header", default=DEFAULT_HEADER, help="rt64_snap_recolor.h to read the colours from")
    ap.add_argument("--out", required=True, help="directory for the PNGs")
    args = ap.parse_args()

    if not os.path.isdir(args.assets):
        sys.exit(f"--assets: {args.assets} is not a directory")
    os.makedirs(args.out, exist_ok=True)

    consts = load_header_constants(args.header)
    print("header colours:", {k: "#%02X%02X%02X" % v for k, v in consts.items()})

    vtx = {}
    for arr, fname in (("jynx_vtx", "model.vtx.inc.c"), ("jynx_hd_vtx", "hd_model.vtx.inc.c")):
        path = os.path.join(args.assets, fname)
        if not os.path.isfile(path):
            sys.exit(f"missing {path}")
        vtx[arr] = load_vtx(path)
        print(f"{arr}: {len(vtx[arr][0])} vertices")

    sections = parse_sections(args.assets)
    if not sections:
        sys.exit("no untextured PRIMITIVE*SHADE sections found; wrong --assets?")

    # 1. The table, and the claim check: every explicit primitive colour in
    #    those sections is either one the header recolours or one it leaves.
    print()
    print(f"{'display list':24s} {'prim':9s} {'recolour':8s} tris  bounding box (bone space)")
    explicit = set()
    recoloured = []
    for s in sections:
        prim = s["prim"]
        hit = matches(prim, False, JYNX_LERP, consts)
        if prim is not None:
            explicit.add(prim[0:3])
        if not s["tris"]:
            print(f"{s['name']:24s} {'#%02X%02X%02X' % prim[0:3] if prim else 'inherited':9s} {'-':8s}    0  (every slot loaded by an earlier DL: {sorted(s['unknown'])})")
            continue
        arr_name = s["tris"][0][0][0]
        pos = vtx[arr_name][0]
        ids = sorted({i for tri in s["tris"] for (_, i) in tri})
        p = pos[ids]
        lo, hi = p.min(axis=0), p.max(axis=0)
        print(f"{s['name']:24s} {'#%02X%02X%02X' % prim[0:3] if prim else 'inherited':9s} {'yes' if hit else 'no':8s} {len(s['tris']):4d}  "
              f"x[{int(lo[0]):5d},{int(hi[0]):5d}] y[{int(lo[1]):5d},{int(hi[1]):5d}] z[{int(lo[2]):5d},{int(hi[2]):5d}]  {arr_name}[{ids[0]}..{ids[-1]}]")
        if hit:
            recoloured.append(s)

    print()
    for key in ("CartridgeFace", "CartridgeHands"):
        if consts[key] not in explicit:
            sys.exit(f"FAIL: the header's {key} #%02X%02X%02X is not a primitive colour Jynx's untextured sections use" % consts[key])
        print(f"ok: {key} #%02X%02X%02X is used by Jynx's untextured sections" % consts[key])
    if not recoloured:
        sys.exit("FAIL: nothing matched the recolour rule")
    print(f"ok: {len(recoloured)} sections match the rule and would be recoloured; every other section is left alone")

    # 2. The rule must be blind to texture-on draws and to the other combiner.
    assert not matches(consts["CartridgeFace"] + (255,), True, JYNX_LERP, consts), "texture-on draw must not match"
    assert not matches(consts["CartridgeFace"] + (255,), False, "MODE:G_CC_MODULATEIDECALA,G_CC_PASS2", consts), "textured combiner must not match"
    assert not matches((0xCC, 0x38, 0x38, 255), False, JYNX_LERP, consts), "the red parts must not match"
    print("ok: rule ignores textured draws, the modulate combiner, and the red untextured parts")

    # 3. Pictures.
    purple = consts["ArtworkPurple"]
    tiles = []
    for s in recoloured:
        before, covered = render_section(s, vtx, s["prim"][0:3])
        after, _ = render_section(s, vtx, purple)
        if covered == 0:
            sys.exit(f"FAIL: {s['name']} rendered no pixels")
        Image.fromarray(before).save(os.path.join(args.out, f"{s['name']}_before.png"))
        Image.fromarray(after).save(os.path.join(args.out, f"{s['name']}_after.png"))
        tiles.append((s["name"], "#%02X%02X%02X" % s["prim"][0:3], before, after))

    size = tiles[0][2].shape[0]
    label_h = 18
    sheet = Image.new("RGB", (size * 2 + 12, len(tiles) * (size + label_h) + 4 * 30 + 12), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    y = 0
    for name, primtxt, before, after in tiles:
        draw.text((4, y + 2), f"{name}  prim {primtxt} -> #%02X%02X%02X   before | after" % purple, fill=(230, 230, 230))
        y += label_h
        sheet.paste(Image.fromarray(before), (0, y))
        sheet.paste(Image.fromarray(after), (size + 12, y))
        y += size
    for label, prim in (("face before", consts["CartridgeFace"]), ("face after", purple),
                        ("hands before", consts["CartridgeHands"]), ("hands after", purple)):
        strip = ramp(prim, width=size * 2 + 12, height=22)
        sheet.paste(Image.fromarray(strip), (0, y + 8))
        draw.text((4, y + 8 + 4), f"{label}: PRIMITIVE x SHADE, shade 0..255", fill=(200, 200, 200))
        y += 30
    out_path = os.path.join(args.out, "jynx_vc_preview.png")
    sheet.save(out_path)
    print(f"wrote {out_path} and {2 * len(tiles)} tiles")


if __name__ == "__main__":
    main()
