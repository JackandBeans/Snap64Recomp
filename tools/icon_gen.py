# The executable's icon, src/snap64.ico, composed from the port's logo,
# docs/logo.png (the author's own art, nothing of the game's). The logo sits
# on a rounded tile of the blue-violet that the logo itself uses as the
# outline behind "Snap64" (its median, sampled from the logo: 90, 73, 136),
# so the icon is a solid shape on any desktop and the colours are the
# logo's own. The tile carries the whole logo (burst, Snap64, the Recomp
# filmstrip) at 256, 128, 64, 48 and 32 px, and the logo's film canister at
# 24 and 16 px, where a wordmark would be a smear. Every entry is reduced
# from the full-resolution logo with a Lanczos filter, so no entry is an
# upscaled or blurred copy of another. Run from anywhere; needs Pillow. The
# .ico is tracked, so this only needs running when the logo changes.
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOGO = os.path.join(ROOT, 'docs', 'logo.png')
OUT = os.path.join(ROOT, 'src', 'snap64.ico')

TILE = (90, 73, 136, 255)       # the logo's outline blue-violet
RIM = (62, 48, 104, 255)        # a shade darker, one thin edge
CANISTER = (1000, 560, 1095, 700)   # the film canister's box in docs/logo.png


def alpha_bbox(im):
    """The box of every pixel that is not fully transparent."""
    return im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()


def tile(s):
    """A rounded square of the logo's blue, drawn oversize and reduced so
    the corners are smooth at every size."""
    ss = 8
    S = s * ss
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    rad = int(S * 0.2)
    d.rounded_rectangle((0, 0, S - 1, S - 1), rad, fill=RIM)
    rim = max(1, round(s * 0.03)) * ss
    d.rounded_rectangle((rim, rim, S - 1 - rim, S - 1 - rim), max(1, rad - rim), fill=TILE)
    return im.resize((s, s), Image.LANCZOS)


def compose(art, s, pad):
    """art fitted into the s-by-s tile, centred, with pad as a fraction of s
    kept clear on each side."""
    art = art.crop(alpha_bbox(art))
    w, h = art.size
    inner = s * (1 - 2 * pad)
    k = min(inner / w, inner / h)
    art = art.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    base = tile(s)
    base.paste(art, ((s - art.width) // 2, (s - art.height) // 2), art)
    return base


def main():
    logo = Image.open(LOGO).convert('RGBA')
    canister = logo.crop(CANISTER)
    frames = [compose(logo, s, 0.06) for s in (256, 128, 64, 48, 32)]
    frames += [compose(canister, s, 0.12) for s in (24, 16)]
    frames[0].save(OUT, format='ICO', sizes=[f.size for f in frames],
                   append_images=frames[1:])
    print('wrote', OUT, [f.size for f in frames])


if __name__ == '__main__':
    main()
