"""
draw_tuner_icon.py

Draw the app_tuner launcher icon: a tuning fork leaning over a keyboard.

The icon is drawn rather than painted so it can be regenerated and nudged,
which the .xcf sources behind the other icons cannot be -- they are not in
the repository.

House style, read off the existing icons: an #E6E6E6 background, flat fills
in a handful of shared colours, and a thick black outline round every shape.
The outline is produced by dilating a silhouette mask rather than by
stroking each primitive, so a shape built out of several primitives -- the
fork is a polygon plus an ellipse -- comes out with one outline instead of
seams where the pieces meet. It also survives rotation: the tilt is applied
to the silhouette before it is outlined, so the outline stays an even width
all the way round instead of thinning on the diagonals.

The fork is drawn after the keyboard, so its outline cuts into the keys and
it reads as standing in front of them rather than beside them.

Usage:

    python contrib/draw_tuner_icon.py <output_dir>

then turn the two PNGs into headers, and re-add the SPDX header that
image_to_asset.py does not write:

    python contrib/image_to_asset.py tuner_big.png tuner_big.h 56x56
    python contrib/image_to_asset.py tuner_small.png tuner_small.h 40x40

image_to_asset.py lives on the feat/CardputerADV-tools-for-assets branch and
has not landed here yet; take it from there until it does.
"""
import sys
import os
try:
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    print("[!] PIL not found. Run: pip install Pillow")
    sys.exit(1)

# The icon is laid out on a 56x56 grid and drawn oversampled, so that
# downsampling at the end does the anti-aliasing.
GRID = 56
SUPERSAMPLE = 8
OUTLINE = 2.5  # in grid units

# Sampled from the icons already in the tree.
BG = (0xE6, 0xE6, 0xE6)
BLACK = (0x00, 0x00, 0x00)
WHITE = (0xFF, 0xFF, 0xFF)
PINK = (0xFF, 0x00, 0x63)  # as used by the chat, IR and LoRa icons

# Keyboard, inset far enough that its outline never touches the frame.
KEYS_X0, KEYS_Y0, KEYS_X1, KEYS_Y1 = 4, 29, 52, 51
WHITE_KEYS = 7
BLACK_KEY_HEIGHT = 0.6  # of the keyboard's height
BLACK_KEY_WIDTH = 0.32  # of a white key's width, either side of the groove

# Tuning fork, off to the right and tipped over so the icon is not a
# symmetrical stack. Tipping it sweeps the prongs towards the top corner,
# so they are shortened to buy that room back: a prong sheared off by the
# frame reads as a mistake rather than as style.
FORK_X = 24
FORK_TOP = 12
FORK_TILT = -14  # degrees, negative leans the prongs to the right
FORK_PIVOT_Y = 44
PRONG_WIDTH = 6.5
PRONG_GAP = 6


def px(value):
    """Grid units to oversampled pixels."""
    return int(round(value * SUPERSAMPLE))


def poly(*points):
    return [(px(x), px(y)) for x, y in points]


def box(x0, y0, x1, y1):
    return [px(x0), px(y0), px(x1), px(y1)]


def dilate(mask, amount):
    """Grow `mask` by `amount` grid units."""
    remaining = px(amount)
    while remaining > 0:
        step = min(remaining, 2)
        mask = mask.filter(ImageFilter.MaxFilter(step * 2 + 1))
        remaining -= step
    return mask


def silhouette(size, shapes):
    """Build one mask covering a union of shapes."""
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    for shape in shapes:
        shape(draw)
    return mask


def stamp(img, mask, fill, outline=OUTLINE):
    """Paint a silhouette in `fill`, with one black outline around it."""
    if outline > 0:
        img.paste(BLACK, (0, 0), dilate(mask, outline))
    img.paste(fill, (0, 0), mask)


def draw_keyboard(img):
    """A keyboard seen head on: white keys with black keys sitting on them."""
    stamp(img, silhouette(img.size, [
        lambda d: d.rounded_rectangle(
            box(KEYS_X0, KEYS_Y0, KEYS_X1, KEYS_Y1),
            radius=px(2), fill=255)]), WHITE)

    step = (KEYS_X1 - KEYS_X0) / WHITE_KEYS

    # Grooves between the white keys.
    for i in range(1, WHITE_KEYS):
        x = KEYS_X0 + step * i
        stamp(img, silhouette(img.size, [
            lambda d, x=x: d.rectangle(
                box(x - 0.6, KEYS_Y0, x + 0.6, KEYS_Y1), fill=255)]),
            BLACK, outline=0)

    # Black keys, grouped two then three as on a real keyboard: the grooves
    # at C-D, D-E, then F-G, G-A, A-B, with E-F left bare.
    height = (KEYS_Y1 - KEYS_Y0) * BLACK_KEY_HEIGHT
    width = step * BLACK_KEY_WIDTH
    for i in (1, 2, 4, 5, 6):
        x = KEYS_X0 + step * i
        stamp(img, silhouette(img.size, [
            lambda d, x=x: d.rounded_rectangle(
                box(x - width, KEYS_Y0, x + width, KEYS_Y0 + height),
                radius=px(1), fill=255)]), BLACK, outline=0)


def draw_fork(img):
    """A tuning fork: two prongs, a bridge, and a weighted stem."""
    x0 = FORK_X
    x1 = x0 + PRONG_WIDTH
    x2 = x1 + PRONG_GAP
    x3 = x2 + PRONG_WIDTH

    fork = silhouette(img.size, [
        lambda d: d.polygon(poly(
            (x0, FORK_TOP), (x1, FORK_TOP), (x1, 25), (x2, 25),
            (x2, FORK_TOP), (x3, FORK_TOP), (x3, 31), (x2 - 0.5, 31),
            (x2 - 0.5, 38), (x1 + 0.5, 38), (x1 + 0.5, 31), (x0, 31)),
            fill=255),
        lambda d: d.ellipse(box(x1 - 2.5, 35, x2 + 2.5, 47), fill=255),
    ])
    # Tilt the silhouette, not the finished shape, so the outline added
    # below stays an even width on the diagonals.
    fork = fork.rotate(FORK_TILT, resample=Image.Resampling.BICUBIC,
                       center=(px(x0 + PRONG_WIDTH + PRONG_GAP / 2),
                               px(FORK_PIVOT_Y)))
    stamp(img, fork, PINK)


def main():
    if len(sys.argv) != 2:
        print("Usage: python draw_tuner_icon.py <output_dir>")
        sys.exit(1)
    out_dir = sys.argv[1]

    img = Image.new("RGB", (GRID * SUPERSAMPLE, GRID * SUPERSAMPLE), BG)
    draw_keyboard(img)
    draw_fork(img)

    # Render each size from the oversampled drawing rather than shrinking
    # the big icon, so the small one keeps as much of its detail as it can.
    for size, name in ((56, "tuner_big"), (40, "tuner_small")):
        path = os.path.join(out_dir, f"{name}.png")
        img.resize((size, size), Image.Resampling.LANCZOS).save(path)
        print(f"[OK] Generated {path}")


if __name__ == "__main__":
    main()
