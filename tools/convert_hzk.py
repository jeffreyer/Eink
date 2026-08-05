"""Convert classic HZK16/HZK24S fonts into the firmware's full-grid SPIFFS bins.

The classic UCDOS HZK files and the firmware share the same addressing:

    slot = (hi - 0xA1) * 94 + (lo - 0xA1)
    offset = slot * glyph_size        # 32 bytes for 16px, 72 bytes for 24px

with bit = 1 meaning ink, MSB-first per row. The classic files only cover the
valid GB2312 range (rows A1..F7 = 8178 slots); this script pads rows F8..FE
with zeros so the output is a complete 8836-slot grid, matching the previous
font files exactly in size and layout.

Sources:
  - HZK16 (267,616 B): UCDOS 16x16 Song, from
    https://github.com/beihunshenlu/HZK-Chinese-character-library-
    (byte-identical to ProfFan/BitmapFont font/HZK16)
  - HZK24S (600,048 B): 24x24 Song incl. the symbol region (rows A1..A9), from
    https://github.com/ProfFan/BitmapFont font/HZK24S

Usage:
    python tools/convert_hzk.py <in.bin> <out.bin> <glyph_size>
"""

import sys

ROWS = 94
COLS = 94
FULL_SLOTS = ROWS * COLS          # 8836
VALID_SLOTS = 0xF7 - 0xA1 + 1     # rows A1..F7


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    gsize = int(sys.argv[3])

    with open(src, "rb") as f:
        data = f.read()

    n_slots = len(data) // gsize
    valid = min(VALID_SLOTS * COLS, n_slots)
    out = bytearray(FULL_SLOTS * gsize)
    out[: valid * gsize] = data[: valid * gsize]

    with open(dst, "wb") as f:
        f.write(out)

    print(f"{src}: {n_slots} slots -> {dst}: {len(out)} bytes "
          f"({valid} valid slots copied, rest zero-filled)")


if __name__ == "__main__":
    main()
