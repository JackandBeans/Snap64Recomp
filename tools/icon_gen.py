# The executable's icon, src/snap64.ico, composed from the port's logo,
# docs/logo.png (the author's own art, nothing of the game's).
#
# 256, 128, 64, 48 and 32 px: the whole logo (burst, Snap64, the Recomp
# filmstrip) on a square tile filling the whole icon, of the blue-violet the
# logo itself uses as the outline behind "Snap64" (its median, sampled from
# the logo: 90, 73, 136), shaded a little lighter at the top and darker at
# the bottom, with a thin darker rim. The logo is set wider than the tile,
# so the burst's tips leave through the tile's edges and the wordmark fills
# the width; the tile's face clips the overflow.
#
# 24 and 16 px (a window's title bar, the taskbar's small mode), where a
# wordmark would be a smear: the logo's film canister alone, on nothing,
# filling the full height. The canister is lifted out of the logo by a
# flood fill from the crop's edges over the burst's yellow and the clear
# ground, so the filmstrip and the burst behind it are left out and nothing
# of the canister is cut; its left side, where the filmstrip lay against it
# in the logo, is the clean right side mirrored, so the two sides match.
#
# Every entry is reduced from the full-resolution logo with a Lanczos
# filter, so no entry is an upscaled or blurred copy of another. Run from
# anywhere; needs Pillow and NumPy. The .ico is tracked, so this only needs
# running when the logo changes.
import os
from collections import deque

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOGO = os.path.join(ROOT, 'docs', 'logo.png')
OUT = os.path.join(ROOT, 'src', 'snap64.ico')

TILE_TOP = (104, 86, 156)       # a little lighter than the logo's outline blue
TILE_BOTTOM = (76, 61, 118)     # a little darker
RIM = (58, 45, 96)              # the thin edge
CANISTER_BOX = (1006, 560, 1084, 694)   # generous box around the canister in docs/logo.png
SS = 8                          # supersampling for the tile's edges


def alpha_bbox(im):
    """The box of every pixel that is not fully transparent."""
    return im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()


def square_mask(S, inset=0):
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rectangle((inset, inset, S - 1 - inset, S - 1 - inset), fill=255)
    return m


def tile(s):
    """A square of the logo's blue filling the icon, with a vertical shade
    and a thin rim, drawn oversize and reduced. Returns the tile and the
    mask of its face (inside the rim)."""
    S = s * SS
    rim = max(1, round(s * 0.025)) * SS
    t = np.linspace(0.0, 1.0, S)[:, None]
    top = np.array(TILE_TOP, dtype=float)[None, :]
    bottom = np.array(TILE_BOTTOM, dtype=float)[None, :]
    rows = np.rint(top * (1 - t) + bottom * t).astype(np.uint8)
    grad = np.repeat(rows[:, None, :], S, axis=1)
    grad = Image.fromarray(np.dstack([grad, np.full((S, S), 255, np.uint8)]), 'RGBA')
    base = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    base.paste((*RIM, 255), (0, 0, S, S), square_mask(S))
    face = square_mask(S, rim)
    base.paste(grad, (0, 0), face)
    return base, face


def on_tile(art, s, width, pad_y=0.04):
    """art on the s-by-s tile: scaled so its width is `width` times the
    tile's (over 1 lets the burst's tips leave through the edges), centred,
    clipped to the tile's face, and never taller than the face less pad_y."""
    art = art.crop(alpha_bbox(art))
    S = s * SS
    w, h = art.size
    k = min((S * width) / w, (S * (1 - 2 * pad_y)) / h)
    art = art.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    base, face = tile(s)
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    layer.paste(art, ((S - art.width) // 2, (S - art.height) // 2), art)
    clipped = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    clipped.paste(layer, (0, 0), face)
    base.alpha_composite(clipped)
    return base.resize((s, s), Image.LANCZOS)


def cut_canister(logo):
    """The film canister alone: everything in its box that a flood fill from
    the box's edges can reach over clear or burst-yellow pixels is ground and
    goes; the enclosed rest is the canister, outline and all."""
    reg = np.asarray(logo.crop(CANISTER_BOX)).astype(int)
    r, g, b, a = reg[..., 0], reg[..., 1], reg[..., 2], reg[..., 3]
    yellow = (r > 190) & (g > 150) & (b < 130) & ((r - b) > 90)
    pale = (r > 225) & (g > 210) & (b > 150) & (a < 250)   # the burst's soft edge
    ground = (a < 8) | yellow | pale
    H, W = ground.shape
    reached = np.zeros_like(ground, dtype=bool)
    q = deque()
    for x in range(W):
        for y in (0, H - 1):
            if ground[y, x] and not reached[y, x]:
                reached[y, x] = True; q.append((y, x))
    for y in range(H):
        for x in (0, W - 1):
            if ground[y, x] and not reached[y, x]:
                reached[y, x] = True; q.append((y, x))
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and ground[ny, nx] and not reached[ny, nx]:
                reached[ny, nx] = True; q.append((ny, nx))
    keep = ~reached
    # The filmstrip's end lies against the canister's left side and is as
    # dark as the outline, so the fill cannot tell them apart. Its remnant
    # is a two-row spur past the body's rounded corner: smooth the left
    # edge with a five-row median and drop whatever sits left of it.
    lefts = np.array([np.argmax(row) if row.any() else W for row in keep])
    smooth = np.array([int(np.median(lefts[max(0, i - 2): i + 3])) for i in range(H)])
    for i in range(H):
        keep[i, :smooth[i]] = False
    out = reg.copy()
    out[..., 3] = np.where(keep, a, 0)
    im = Image.fromarray(out.astype(np.uint8), 'RGBA')
    im = im.crop(alpha_bbox(im))
    # The right side is the clean one; make the left its mirror: the whole
    # silhouette, and the outer band of pixels (outline, the bands' ends),
    # while the label's own art in the middle stays as drawn.
    px = np.asarray(im).copy()
    w = px.shape[1]
    mirrored = px[:, ::-1, :]
    half = w // 2
    band = max(4, w // 5)
    px[:, :half, 3] = mirrored[:, :half, 3]
    px[:, :band, :3] = mirrored[:, :band, :3]
    return Image.fromarray(px, 'RGBA')


def alone(art, s):
    """art on a clear ground, filling the full height (or width if wider)."""
    w, h = art.size
    k = min(s / h, s / w)
    art = art.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    out = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    out.paste(art, ((s - art.width) // 2, (s - art.height) // 2), art)
    return out


def main():
    logo = Image.open(LOGO).convert('RGBA')
    canister = cut_canister(logo)
    frames = [on_tile(logo, s, 1.16) for s in (256, 128, 64, 48, 32)]
    frames += [alone(canister, s) for s in (24, 16)]
    frames[0].save(OUT, format='ICO', sizes=[f.size for f in frames],
                   append_images=frames[1:])
    print('wrote', OUT, [f.size for f in frames], 'canister', canister.size)
    return canister


if __name__ == '__main__':
    main()
