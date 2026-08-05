"""Render preview PNGs of the GB2312 bitmap fonts used by the countdown module.

Uses the real assets: data/fonts/gb2312_*.bin (Chinese glyphs, full 94x94
GB2312 grid) and src/GUI/font12.cpp / font16.cpp / font24.cpp (ASCII fonts).
Outputs:
  - font_sample.png : sample glyphs at 16px and 24px, x6
  - countdown_mockup.png : faithful 200x200 module layout, x3
"""

import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
VIS = Path(
    r"C:\Users\jeff\.codex\visualizations\2026\08\05\019fd1a1-8f98-7d40-a81f-68aa438c6775"
)

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
RED = (204, 24, 24)


class GbFont:
    def __init__(self, path, cell):
        self.cell = cell
        self.gsize = cell * cell // 8
        self.data = path.read_bytes()

    def glyph(self, gbcode):
        hi, lo = gbcode >> 8, gbcode & 0xFF
        off = ((hi - 0xA1) * 94 + (lo - 0xA1)) * self.gsize
        bpr = self.cell // 8
        rows = []
        for r in range(self.cell):
            bits = []
            for c in range(bpr):
                b = self.data[off + r * bpr + c]
                bits.extend((b >> (7 - i)) & 1 for i in range(8))
            rows.append(bits[: self.cell])
        return rows


class AsciiFont:
    def __init__(self, cpp_path, width, height, bpr):
        text = cpp_path.read_text(encoding="utf-8", errors="ignore")
        self.bytes_ = bytes(
            int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", text)
        )
        self.width = width
        self.height = height
        self.bpr = bpr
        self.bpc = height * bpr
        self.first = 0x20
        self.count = len(self.bytes_) // self.bpc

    def glyph(self, ch):
        i = ord(ch) - self.first
        if i < 0 or i >= self.count:
            return [[0] * self.width for _ in range(self.height)]
        off = i * self.bpc
        rows = []
        for r in range(self.height):
            bits = []
            for c in range(self.bpr):
                b = self.bytes_[off + r * self.bpr + c]
                bits.extend((b >> (7 - i)) & 1 for i in range(8))
            rows.append(bits[: self.width])
        return rows


def paste(img, x, y, rows, color):
    px = img.load()
    for r, row in enumerate(rows):
        for c, v in enumerate(row):
            if v:
                px[x + c, y + r] = color


def text_width(s, gb_map, f16, f24, a12, a16, a24, mode):
    w = 0
    for ch in s:
        if ord(ch) < 0x80:
            a = {2: a12, 3: a16, 4: a24}[mode]
            w += a.width
        else:
            w += {2: 16, 3: 16, 4: 24}[mode]
    return w


def draw_text(img, x, y, s, gb_map, f16, f24, a12, a16, a24, mode, color):
    cx = x
    for ch in s:
        if ord(ch) < 0x80:
            a = {2: a12, 3: a16, 4: a24}[mode]
            paste(img, cx, y, a.glyph(ch), color)
            cx += a.width
        else:
            gb = gb_map.get(ord(ch))
            if gb is None:
                cx += {2: 16, 3: 16, 4: 24}[mode]
                continue
            f = f16 if mode in (2, 3) else f24
            paste(img, cx, y, f.glyph(gb), color)
            cx += f.cell


def load_map(path):
    data = path.read_bytes()
    n = struct.unpack("<I", data[:4])[0]
    m = {}
    for i in range(n):
        uni, gb = struct.unpack("<HH", data[4 + i * 4 : 8 + i * 4])
        m[uni] = gb
    return m


def main():
    gb_map = load_map(ROOT / "data/fonts/gb2312_map.bin")
    f16 = GbFont(ROOT / "data/fonts/gb2312_16.bin", 16)
    f24 = GbFont(ROOT / "data/fonts/gb2312_24.bin", 24)
    a12 = AsciiFont(ROOT / "src/GUI/font12.cpp", 7, 12, 1)
    a16 = AsciiFont(ROOT / "src/GUI/font16.cpp", 11, 16, 2)
    a24 = AsciiFont(ROOT / "src/GUI/font24.cpp", 17, 24, 3)

    VIS.mkdir(parents=True, exist_ok=True)

    # ---- sample sheet: 16px row + 24px row, x6 ----
    samples = "生中纪倒计时天年永爱你纪念日０１２３４５６７８９，。°"
    scale = 6
    cell_w = 24
    img = Image.new("RGB", (len(samples) * cell_w, 40), WHITE)
    for i, ch in enumerate(samples):
        gb = gb_map.get(ord(ch))
        if gb is None:
            continue
        paste(img, i * cell_w, 0, f16.glyph(gb), BLACK)
        paste(img, i * cell_w, 16, f24.glyph(gb), BLACK)
    img = img.resize(
        (len(samples) * cell_w * scale, 40 * scale), Image.NEAREST
    )
    _annotate(img, [(4, 2, "16px"), (4, 16 * scale + 2, "24px")])
    img.save(VIS / "font_sample.png")
    print("saved font_sample.png")

    # ---- countdown mockup: native 200x200, x3 ----
    W, H = 200, 200
    scr = Image.new("RGB", (W, H), WHITE)

    def centered(y, s, mode, color):
        tw = text_width(s, gb_map, f16, f24, a12, a16, a24, mode)
        draw_text(scr, (W - tw) // 2, y, s, gb_map, f16, f24, a12, a16, a24, mode, color)

    centered(24, "结婚纪念日", 4, RED)
    centered(54, "2027-08-05", 2, BLACK)
    centered(88, "365", 4, BLACK)
    centered(132, "DAYS", 3, RED)
    centered(168, "EVERY YEAR", 2, BLACK)
    mock = scr.resize((W * 3, H * 3), Image.NEAREST)
    mock.save(VIS / "countdown_mockup.png")
    print("saved countdown_mockup.png")


def _annotate(img, labels):
    from PIL import ImageFont

    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=20)
    except TypeError:
        font = ImageFont.load_default()
    for x, y, text in labels:
        d.text((x, y), text, fill=(150, 150, 150), font=font)


if __name__ == "__main__":
    main()
