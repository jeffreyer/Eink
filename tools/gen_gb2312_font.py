#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 GB2312 全量点阵字库（16x16 / 24x24）及 Unicode->GB2312 映射表。

输出（均写入 data/fonts/，随文件系统镜像上传到 /spiffs/fonts/）:
  gb2312_16.bin   94x94 网格，每字形 32 字节，行优先、MSB 在前
  gb2312_24.bin   94x94 网格，每字形 72 字节，行优先、MSB 在前
  gb2312_map.bin  [u32 count][count * (u16 unicode, u16 gbcode)] 小端，按 unicode 升序

字库偏移 = ((区 - 0xA1) * 94 + (位 - 0xA1)) * 每字形字节数

用法:
  python tools/gen_gb2312_font.py
  python tools/gen_gb2312_font.py --font C:/path/to/NotoSansSC.ttf --outdir data/fonts
  python tools/gen_gb2312_font.py --thicken 0   # 不加粗
"""

import argparse
import os
import struct

from PIL import Image, ImageDraw, ImageFont


def enumerate_gb2312():
    """枚举 GB2312 全部有效码位（含 6763 汉字 + 682 符号）。"""
    chars = []  # (hi, lo, ch)
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
                chars.append((hi, lo, ch))
            except UnicodeDecodeError:
                pass
    return chars


def render_glyph(font, ch, size, threshold):
    """以 size 为单元格渲染单个字符的 1bit 位图（直接字号渲染、居中、超界才缩放）。"""
    tmp = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(tmp).text((0, 0), ch, font=font, fill=255)
    bbox = tmp.getbbox()
    if not bbox:
        return Image.new("1", (size, size), 0)

    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    glyph = tmp.crop(bbox)

    if w > size or h > size:
        scale = min(size / w, size / h)
        nw = max(1, round(w * scale))
        nh = max(1, round(h * scale))
        glyph = glyph.resize((nw, nh), Image.LANCZOS)

    out = Image.new("1", (size, size), 0)
    ox = (size - glyph.width) // 2
    oy = (size - glyph.height) // 2
    # 阈值越低笔画越粗（把抗锯齿边缘像素收进来）；16px 用 48，24px 用 1
    out.paste(glyph.point(lambda p: 255 if p >= threshold else 0), (ox, oy))
    return out


def thicken_glyph(img, passes):
    """对 1bit 位图做 4 邻域膨胀，每轮笔画向外扩 1 像素（解决小字号线细问题）。"""
    w, h = img.size
    for _ in range(passes):
        tmp = img.copy()
        for y in range(h):
            for x in range(w):
                if img.getpixel((x, y)):
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h:
                            tmp.putpixel((nx, ny), 1)
        img = tmp
    return img


def glyph_to_bytes(img, size):
    """位图转行优先字节流，MSB 在前（bit7 = 最左像素）。"""
    data = bytearray()
    for y in range(size):
        byte = 0
        for x in range(size):
            if img.getpixel((x, y)):
                byte |= 0x80 >> (x % 8)
            if x % 8 == 7:
                data.append(byte)
                byte = 0
        if size % 8:
            data.append(byte)
    return bytes(data)


def build_font_bin(font, chars, size, thicken, threshold):
    """按 94x94 网格生成扁平字库文件字节。"""
    grid = bytearray(94 * 94 * (size * size // 8))
    blank = 0
    for hi, lo, ch in chars:
        img = render_glyph(font, ch, size, threshold)
        if thicken > 0:
            img = thicken_glyph(img, thicken)
        glyph = glyph_to_bytes(img, size)
        offset = ((hi - 0xA1) * 94 + (lo - 0xA1)) * len(glyph)
        grid[offset : offset + len(glyph)] = glyph
        if not img.getbbox():
            blank += 1
    return bytes(grid), blank


def build_map_bin(chars):
    """生成 unicode->gbcode 映射表（小端，按 unicode 升序）。"""
    uni_to_gb = {}
    for hi, lo, ch in chars:
        cp = ord(ch)
        gb = (hi << 8) | lo
        if cp not in uni_to_gb:
            uni_to_gb[cp] = gb

    items = sorted(uni_to_gb.items())
    buf = bytearray()
    buf += struct.pack("<I", len(items))
    for uni, gb in items:
        buf += struct.pack("<HH", uni, gb)
    return bytes(buf), len(items)


def main():
    parser = argparse.ArgumentParser(description="Generate GB2312 bitmap fonts")
    parser.add_argument(
        "--font",
        default=r"C:\Windows\Fonts\NotoSansSC-VF.ttf",
        help="开源中文字体路径（默认 Noto Sans SC）",
    )
    parser.add_argument("--outdir", default="data/fonts")
    parser.add_argument(
        "--thicken16",
        type=int,
        default=0,
        help="16x16 膨胀加粗轮数（默认 0，阈值 48 已足够）",
    )
    parser.add_argument(
        "--thicken24",
        type=int,
        default=1,
        help="24x24 膨胀加粗轮数（默认 1，保证笔画均匀 2~3px）",
    )
    parser.add_argument(
        "--threshold16",
        type=int,
        default=128,
        help="16x16 渲染阈值（默认 128，配合膨胀加粗）",
    )
    parser.add_argument(
        "--threshold24",
        type=int,
        default=128,
        help="24x24 渲染阈值（默认 128，配合膨胀加粗）",
    )
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    chars = enumerate_gb2312()
    hanzi = sum(1 for _, _, ch in chars if 0x4E00 <= ord(ch) <= 0x9FFF)
    print(f"GB2312 有效码位: {len(chars)} (汉字 {hanzi} + 符号 {len(chars) - hanzi})")

    # 预加载字体（直接按目标字号渲染）
    font16 = ImageFont.truetype(args.font, 16)
    font24 = ImageFont.truetype(args.font, 24)

    data16, blank16 = build_font_bin(font16, chars, 16, args.thicken16, args.threshold16)
    data24, blank24 = build_font_bin(font24, chars, 24, args.thicken24, args.threshold24)
    map_bin, map_count = build_map_bin(chars)

    p16 = os.path.join(args.outdir, "gb2312_16.bin")
    p24 = os.path.join(args.outdir, "gb2312_24.bin")
    pmap = os.path.join(args.outdir, "gb2312_map.bin")

    with open(p16, "wb") as f:
        f.write(data16)
    with open(p24, "wb") as f:
        f.write(data24)
    with open(pmap, "wb") as f:
        f.write(map_bin)

    print(f"16x16: {len(data16)} bytes (空白字形 {blank16})")
    print(f"24x24: {len(data24)} bytes (空白字形 {blank24})")
    print(f"映射表: {len(map_bin)} bytes, {map_count} 条")
    print(f"输出目录: {os.path.abspath(args.outdir)}")

    # 抽样检查：ASCII 艺术
    for ch in "纪倒计时中A，" :
        idx = ord(ch)
        gb = None
        for hi, lo, c in chars:
            if c == ch:
                gb = (hi << 8) | lo
                break
        if gb is None:
            continue
        offset = ((gb >> 8) - 0xA1) * 94 + ((gb & 0xFF) - 0xA1)
        glyph = data16[offset * 32 : offset * 32 + 32]
        print(f"\n[{ch}] (gbcode 0x{gb:04X}) 16x16:")
        for r in range(16):
            row = glyph[r * 2 : r * 2 + 2]
            print("".join("#" if row[c // 8] & (0x80 >> (c % 8)) else "." for c in range(16)))


if __name__ == "__main__":
    main()
