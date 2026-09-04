# The executable's icon: src/snap64.ico, drawn here in the title wordmark's
# colours (a yellow rounded tile with a dark-red rim and a camera lens).
# Original art, nothing of the game's. Each size is drawn eight times
# oversize and reduced, so the 16 and 32 px entries are crisp rather than a
# blurred 256. Run from anywhere; needs Pillow. The .ico is tracked, so this
# only needs running when the design changes.
import os
from PIL import Image, ImageDraw

YELLOW = (248, 192, 0, 255)
RED = (168, 24, 0, 255)
ORANGE = (224, 64, 0, 255)
BLACK = (24, 16, 8, 255)
WHITE = (255, 248, 224, 255)
CLEAR = (0, 0, 0, 0)


def draw(s):
    ss = 8
    S = s * ss
    im = Image.new('RGBA', (S, S), CLEAR)
    d = ImageDraw.Draw(im)
    rim = max(1, round(s * 0.075)) * ss
    rad = int(S * 0.22)
    d.rounded_rectangle((0, 0, S - 1, S - 1), rad, fill=RED)
    d.rounded_rectangle((rim, rim, S - 1 - rim, S - 1 - rim), max(1, rad - rim), fill=YELLOW)
    c = S / 2 + S * 0.03  # the lens sits a little low, under the button

    def disc(r, col, cx=S / 2, cy=None):
        cy = c if cy is None else cy
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=col)

    disc(S * 0.30, RED)
    disc(S * 0.24, ORANGE)
    disc(S * 0.17, BLACK)
    disc(S * 0.045, WHITE, S / 2 - S * 0.065, c - S * 0.065)
    if s > 20:  # the shutter button on the rim, top left; too small below 24 px
        d.rounded_rectangle((S * 0.20, S * 0.06, S * 0.36, S * 0.145), int(S * 0.02), fill=RED)
    return im.resize((s, s), Image.LANCZOS)


if __name__ == '__main__':
    sizes = [256, 64, 48, 32, 24, 16]
    ims = [draw(s) for s in sizes]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'snap64.ico')
    ims[0].save(out, format='ICO', sizes=[(s, s) for s in sizes], append_images=ims[1:])
    print('wrote', os.path.normpath(out), 'sizes', sizes)
