"""Print ASCII art for glyphs at full-grid slots from HZK files.

Usage: python check_hzk_glyphs.py <file> <gsize> <w> [qu wei ...]
Compares against the same slots in data/fonts/gb2312_*.bin (current Noto).
"""

import sys


def read_glyph(data, slot, gsize, w):
    off = slot * gsize
    pixels = set()
    bpr = gsize // w
    for r in range(w):
        for c in range(bpr):
            byte = data[off + r * bpr + c]
            for bit in range(8):
                if (byte >> (7 - bit)) & 1:
                    pixels.add((c * 8 + bit, r))
    return pixels


def art(pixels, w):
    return "\n".join(
        "".join("#" if (c, r) in pixels else "." for c in range(w))
        for r in range(w)
    )


def main():
    path = sys.argv[1]
    gsize = int(sys.argv[2])
    w = int(sys.argv[3])
    pairs = []
    for i in range(4, len(sys.argv), 2):
        pairs.append((int(sys.argv[i], 16), int(sys.argv[i + 1], 16)))

    with open(path, "rb") as f:
        data = f.read()
    ref_path = "data/fonts/gb2312_16.bin" if gsize == 32 else "data/fonts/gb2312_24.bin"
    with open(ref_path, "rb") as f:
        ref = f.read()

    print(f"file={path} slots={len(data)//gsize}")
    for qu, wei in pairs:
        slot = ((qu - 0xA1) * 94 + (wei - 0xA1))
        name = ascii(bytes((qu, wei)).decode("gb2312"))
        print("=" * (w + 4))
        print(f"{name}  GB {qu:02X}{wei:02X}  slot={slot}")
        print("-- current bin --")
        print(art(read_glyph(ref, slot, gsize, w), w))
        print("-- candidate --")
        print(art(read_glyph(data, slot, gsize, w), w))
        print()


if __name__ == "__main__":
    main()
