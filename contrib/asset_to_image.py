"""
asset_to_image.py

Convert an asset (RGB565 C++ header format) to image (PNG).
"""
import re
import sys
try:
    from PIL import Image
except ImportError:
    print("[!] PIL not found. Run: pip install Pillow")
    sys.exit(1)


def read_and_save(input_file, output_file, size):
    with open(input_file) as f:
        data = f.read()

    # Grab everything between { and };
    body = data.split('{', 1)[1].rsplit('}', 1)[0]
    words = [int(w, 16) for w in re.findall(r'0x[0-9a-fA-F]+', body)]

    W, H = size
    assert len(words) == W * H, f"got {len(words)}, expected {W*H}"

    img = Image.new('RGB', (W, H))
    px = img.load()
    for i, w in enumerate(words):
        # Undo the byte swap your generator does
        w = ((w << 8) & 0xFF00) | (w >> 8)
        r = ((w >> 11) & 0x1F) << 3
        g = ((w >> 5) & 0x3F) << 2
        b = (w & 0x1F) << 3
        # Replicate top bits into low bits for nicer round-trip
        r |= r >> 5
        g |= g >> 6
        b |= b >> 5
        px[i % W, i // W] = (r, g, b)

    img.save(output_file)


def main():
    if len(sys.argv) < 4:
        print(
            "Usage: python asset_to_image.py "
            "<input_asset.h> <output.png> <56x56>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    size = tuple(int(x) for x in sys.argv[3].split('x'))

    read_and_save(input_file, output_file, size)
    print(f"[OK] Generated {output_file}")


if __name__ == "__main__":
    main()
