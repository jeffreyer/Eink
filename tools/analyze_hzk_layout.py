"""Empirically determine the layout of classic HZK font files.

Scans every glyph slot in a candidate HZK file and finds the slot that best
matches a reference glyph (e.g. the Noto-generated glyph for 生 from the
current full-grid bins). From the winning slot index we can infer the
addressing formula used by the source file.
"""

import sys


def read_glyph(data, slot, gsize, w):
    """Read a glyph at slot index; returns a set of (x, y) ink pixels."""
    off = slot * gsize
    pixels = set()
    bytes_per_row = gsize // w
    for r in range(w):
        for c in range(bytes_per_row):
            byte = data[off + r * bytes_per_row + c]
            for bit in range(8):
                if (byte >> (7 - bit)) & 1:
                    pixels.add((c * 8 + bit, r))
    return pixels


def ink_count(pixels):
    return len(pixels)


def overlap(a, b):
    inter = len(a & b)
    union = len(a | b)
    return inter / union if union else 0.0


def art(pixels, w):
    lines = []
    for r in range(w):
        lines.append("".join("#" if (c, r) in pixels else "." for c in range(w)))
    return "\n".join(lines)


def scan(data, gsize, w, ref_pixels, top_n=8):
    n_slots = len(data) // gsize
    scores = []
    for i in range(n_slots):
        g = read_glyph(data, i, gsize, w)
        s = overlap(g, ref_pixels)
        scores.append((s, i, g))
    scores.sort(key=lambda x: -x[0])
    return scores[:top_n]


def main():
    path = sys.argv[1]
    gsize = int(sys.argv[2])  # 32 or 72
    w = int(sys.argv[3])      # 16 or 24
    ref_path = sys.argv[4]    # current full-grid bin containing Noto glyph
    qu = int(sys.argv[5], 16)
    wei = int(sys.argv[6], 16)

    with open(path, "rb") as f:
        data = f.read()
    with open(ref_path, "rb") as f:
        refdata = f.read()

    ref_idx = ((qu - 0xA1) * 94 + (wei - 0xA1))
    ref_pixels = read_glyph(refdata, ref_idx, gsize, w)
    print(f"file={path} size={len(data)} slots={len(data)//gsize} "
          f"ref_slot={ref_idx} ref_ink={ink_count(ref_pixels)}")
    print("reference glyph (from current bin):")
    print(art(ref_pixels, w))
    print()

    hits = scan(data, gsize, w, ref_pixels)
    for score, slot, g in hits:
        print(f"slot={slot:5d} score={score:.3f} ink={ink_count(g)}")
        print(art(g, w))
        print()


if __name__ == "__main__":
    main()
