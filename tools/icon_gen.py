# The executable's icon, src/snap64.ico, composed from the port's logo,
# docs/logo.png (the author's own art, nothing of the game's). The logo sits
# on a rounded tile of the blue-violet that the logo itself uses as the
# outline behind "Snap64" (its median, sampled from the logo: 90, 73, 136),
# shaded a little lighter at the top and darker at the bottom, with a thin
# darker rim, so the icon is a solid shape with some depth on any desktop
# and its colours are the logo's own. The logo is set wider than the tile,
# so the burst's tips leave through the tile's edges and the wordmark fills
# the width; the tile's rounded mask clips the overflow. The tile carries
# the whole logo (burst, Snap64, the Recomp filmstrip) at 256, 128, 64, 48
# and 32 px, and the logo's film canister at 24 and 16 px, where a wordmark
# would be a smear. Every entry is reduced from the full-resolution logo
# with a Lanczos filter, so no entry is an upscaled or blurred copy of
# another. Run from anywhere; needs Pillow. The .ico is tracked, so this
# only needs running when the logo changes.
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOGO = os.path.join(ROOT, 'docs', 'logo.png')
OUT = os.path.join(ROOT, 'src', 'snap64.ico')

TILE = (90, 73, 136)            # the logo's outline blue-violet, the tile's middle
TILE_TOP = (104, 86, 156)       # a little lighter at the top
TILE_BOTTOM = (76, 61, 118)     # a little darker at the bottom
RIM = (58, 45, 96, 255)         # the thin edge
CANISTER = (1000, 560, 1095, 700)   # the film canister's box in docs/logo.png
SS = 8                          # supersampling for the tile's edges


def alpha_bbox(im):
    """The box of every pixel that is not fully transparent."""
    return im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()


def rounded_mask(S, rad, inset=0):
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle((inset, inset, S - 1 - inset, S - 1 - inset), max(1, rad - inset), fill=255)
    return m


def tile(s):
    """A rounded square of the logo's blue with a vertical shade and a rim,
    drawn oversize and reduced so the corners are smooth at every size.
    Returns the tile and the mask of its face (inside the rim)."""
    S = s * SS
    rad = int(S * 0.2)
    rim = max(1, round(s * 0.03)) * SS
    grad = Image.new('RGBA', (S, S))
    px = grad.load()
    for y in range(S):
        t = y / (S - 1)
        c = tuple(round(TILE_TOP[i] * (1 - t) + TILE_BOTTOM[i] * t) for i in range(3)) + (255,)
        for x in range(S):
            px[x, y] = c
    base = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    base.paste((*RIM[:3], 255), (0, 0, S, S), rounded_mask(S, rad))
    face = rounded_mask(S, rad, rim)
    base.paste(grad, (0, 0), face)
    return base, face


def compose(art, s, width, pad_y=0.04):
    """art on the s-by-s tile: scaled so its width is `width` times the
    tile's (over 1 lets the burst's tips leave through the edges), centred,
    clipped to the tile's face, and never taller than the face's height less
    the vertical pad."""
    art = art.crop(alpha_bbox(art))
    S = s * SS
    w, h = art.size
    k = (S * width) / w
    k = min(k, (S * (1 - 2 * pad_y)) / h)
    art = art.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    base, face = tile(s)
    layer = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    layer.paste(art, ((S - art.width) // 2, (S - art.height) // 2), art)
    clipped = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    clipped.paste(layer, (0, 0), face)
    base.alpha_composite(clipped)
    return base.resize((s, s), Image.LANCZOS)


def main():
    logo = Image.open(LOGO).convert('RGBA')
    canister = logo.crop(CANISTER)
    frames = [compose(logo, s, 1.16) for s in (256, 128, 64, 48, 32)]
    frames += [compose(canister, s, 0.62, pad_y=0.1) for s in (24, 16)]
    frames[0].save(OUT, format='ICO', sizes=[f.size for f in frames],
                   append_images=frames[1:])
    print('wrote', OUT, [f.size for f in frames])


if __name__ == '__main__':
    main()
