# -*- coding: utf-8 -*-
"""生成 TFT_eSPI 可用的中文点阵字库头文件 include/font_cn.h

两级字库:
- 16x16: GB2312 一级汉字 3755 个（消息/描述等任意常用文本）
- 24x24: 界面固定文本用字（状态大字）

数据按 unicode 排序存查找表，固件二分查找。
字模位序: 每行 size/8 字节，bit7=最左像素。
"""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "include", "font_cn.h")

FONTS = ["C:/Windows/Fonts/simhei.ttf", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc"]
FONT_PATH = next((f for f in FONTS if os.path.exists(f)), None)
if not FONT_PATH:
    raise SystemExit("no CJK font found in C:/Windows/Fonts")

def load_font(size):
    return ImageFont.truetype(FONT_PATH, size)

def render_glyph(uni, size, font):
    img = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(img)
    bb = d.textbbox((0, 0), chr(uni), font=font)
    w, h = bb[2] - bb[0], bb[3] - bb[1]
    x = (size - w) // 2 - bb[0]
    y = (size - h) // 2 - bb[1]
    d.text((x, y), chr(uni), font=font, fill=255)
    out = bytearray()
    for ry in range(size):
        for col in range(size // 8):
            b = 0
            for k in range(8):
                rx = col * 8 + k
                b = (b << 1) | (1 if img.getpixel((rx, ry)) >= 128 else 0)
            out.append(b)
    return bytes(out)

# ---- 16x16: GB2312 一级汉字（区位 16-55） ----
font16 = load_font(16)
entries = []  # (uni, idx)
for qu in range(16, 56):
    for wei in range(1, 95):
        try:
            ch = bytes([0xA0 + qu, 0xA0 + wei]).decode("gb2312")[0]
        except Exception:
            continue
        if not ("\u4e00" <= ch <= "\u9fff"):
            continue
        entries.append((ord(ch), (qu - 16) * 94 + (wei - 1)))

assert len(entries) == 3755, len(entries)
entries.sort(key=lambda e: e[0])
font16_data = bytearray(3755 * 32)
for uni, idx in entries:
    g = render_glyph(uni, 16, font16)
    font16_data[idx * 32 : idx * 32 + 32] = g

# ---- 24x24: 界面白名单 ----
WORDS = ("AI工作流状态初始化空闲运行中已完成出错等待冷却严重故障手动控制"
         "系统启动中等待任务正在处理任务出错等待输入限流冷却请检查连接离线就绪")
font24 = load_font(24)
uni24 = sorted({ord(c) for c in WORDS if ord(c) > 0x7F})
font24_data = bytearray(len(uni24) * 72)
for i, uni in enumerate(uni24):
    font24_data[i * 72 : i * 72 + 72] = render_glyph(uni, 24, font24)

# ---- 输出 C 头文件 ----
def arr(name, data, per):
    lines = ["static const uint8_t %s[] PROGMEM = {" % name]
    for i in range(0, len(data), per):
        chunk = data[i : i + per]
        lines.append("  " + ",".join("0x%02X" % b for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)

with open(OUT, "w", encoding="utf-8") as f:
    f.write("""#ifndef FONT_CN_H
#define FONT_CN_H

// 自动生成，请勿手改：scripts/gen_font.py
// 16x16: GB2312 一级汉字 3755 字；24x24: 界面白名单 %d 字
#include <stdint.h>
#include <stddef.h>

typedef struct { uint16_t uni; uint16_t idx; } UniEntry16;

static const UniEntry16 UNI16[%d] = {
""" % (len(uni24), len(entries)))
    for uni, idx in entries:
        f.write("  {0x%04X, %d},\n" % (uni, idx))
    f.write("""};

""" + arr("FONT16", font16_data, 16) + """

static const uint16_t UNI24[%d] = {
""" % len(uni24))
    for u in uni24:
        f.write("  0x%04X,\n" % u)
    f.write("};\n\n" + arr("FONT24", font24_data, 24) + """

#endif // FONT_CN_H
""")

print("font_cn.h generated:", os.path.getsize(OUT), "bytes;",
      "uni16=%d font16=%dB uni24=%d font24=%dB" % (len(entries), len(font16_data), len(uni24), len(font24_data)))