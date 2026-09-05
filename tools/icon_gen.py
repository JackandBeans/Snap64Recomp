# The executable's icon, src/snap64.ico, composed from the port's logo,
# docs/logo.png (the author's own art, nothing of the game's). Explorer,
# the desktop and the taskbar's large sizes show the whole logo: the burst
# with the Snap64 wordmark and the Recomp filmstrip, fitted to the square.
# The two smallest entries (24 and 16 px), where the wordmark is a few
# pixels tall, show the logo's film canister instead, so the icon still
# reads as a shape in the taskbar's small mode and in a window's title bar.
# Every size is reduced from the full-resolution logo with a Lanczos filter,
# so no entry is an upscaled or blurred copy of another. Run from anywhere;
# needs Pillow. The .ico is tracked, so this only needs running when the logo
# changes.
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOGO = os.path.join(ROOT, 'docs', 'logo.png')
OUT = os.path.join(ROOT, 'src', 'snap64.ico')

# The canister in the logo's lower right (pixel box in docs/logo.png).
CANISTER = (1000, 560, 1095, 700)


def alpha_bbox(im):
    """The box of every pixel that is not fully transparent."""
    return im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()


def fit(im, s, pad=0.02):
    """im fitted into an s-by-s transparent square, centred, with a small
    margin so the burst's tips do not touch the edge."""
    im = im.crop(alpha_bbox(im))
    w, h = im.size
    inner = s * (1 - 2 * pad)
    k = min(inner / w, inner / h)
    im = im.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    sq = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    sq.paste(im, ((s - im.width) // 2, (s - im.height) // 2), im)
    return sq


def main():
    logo = Image.open(LOGO).convert('RGBA')
    canister = logo.crop(CANISTER)
    frames = []
    for s in (256, 128, 64, 48, 32):
        frames.append(fit(logo, s))
    for s in (24, 16):
        frames.append(fit(canister, s, pad=0.0))
    # Pillow writes one .ico with every frame as its own entry when given
    # append_images; the first frame's size list must name them all.
    frames[0].save(OUT, format='ICO', sizes=[f.size for f in frames],
                   append_images=frames[1:])
    print('wrote', OUT, [f.size for f in frames])


if __name__ == '__main__':
    main()
