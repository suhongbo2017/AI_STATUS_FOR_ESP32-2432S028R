#ifndef CHINESE_FONT_H
#define CHINESE_FONT_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "font_cn.h"

// 中文点阵渲染：16x16（GB2312 一级 3755 字）与 24x24（界面白名单）
// 与 ASCII 混排：ASCII 用 Sprite 的内置 GLCD 字体（8x8）
class ChineseFont {
public:
    // 在 sprite 上绘制 UTF-8 文本；返回绘制后 x 坐标。未知字显示 □
    static int draw(TFT_eSprite& sp, int x, int y, const char* utf8,
                    uint16_t color, bool big = false, int maxChars = 32) {
        const int size = big ? 24 : 16;
        int count = 0;
        while (*utf8 && count < maxChars) {
            uint16_t uni = decodeUTF8(utf8);
            if (uni < 0x80) {
                // ASCII：GLCD 8x8，垂直居中
                sp.setTextColor(color);
                sp.setTextSize(1);
                sp.setCursor(x, y + (size - 8) / 2);
                sp.print((char)uni);
                x += 8;
            } else {
                const uint8_t* g = nullptr;
                if (!big) g = glyph16(uni);
                else      g = glyph24(uni);
                if (g) {
                    sp.drawBitmap(x, y, g, size, size, color);
                } else {
                    drawFallback(sp, x, y, size, color);
                }
                x += size;
            }
            count++;
        }
        return x;
    }

    // 直接在 TFT 上绘制（1bpp pushImage），用于标题栏等固定文本
    static int draw(TFT_eSPI& tft, int x, int y, const char* utf8,
                    uint16_t fg, uint16_t bg, bool big = false, int maxChars = 32) {
        const int size = big ? 24 : 16;
        int count = 0;
        while (*utf8 && count < maxChars) {
            uint16_t uni = decodeUTF8(utf8);
            if (uni < 0x80) {
                tft.setTextColor(fg, bg);
                tft.setTextSize(1);
                tft.setCursor(x, y + (size - 8) / 2);
                tft.print((char)uni);
                x += 8;
            } else {
                const uint8_t* g = big ? glyph24(uni) : glyph16(uni);
                if (g) {
                    tft.setBitmapColor(fg, bg);
                    tft.pushImage(x, y, size, size, g, true);  // 1bpp，0 位透明
                } else {
                    tft.drawRect(x, y, size, size, fg);
                }
                x += size;
            }
            count++;
        }
        return x;
    }

private:
    static uint16_t decodeUTF8(const char*& s) {
        uint8_t c = (uint8_t)*s;
        uint16_t v;
        if (c < 0x80) { v = c; s++; return v; }
        if ((c & 0xE0) == 0xC0) {
            v = ((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
            s += 2; return v;
        }
        v = ((c & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) | ((uint8_t)s[2] & 0x3F);
        s += 3; return v;
    }

    static const uint8_t* glyph16(uint16_t uni) {
        int lo = 0, hi = (int)(sizeof(UNI16) / sizeof(UNI16[0])) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (UNI16[mid].uni == uni) return &FONT16[UNI16[mid].idx * 32];
            if (UNI16[mid].uni < uni) lo = mid + 1; else hi = mid - 1;
        }
        return nullptr;
    }

    static const uint8_t* glyph24(uint16_t uni) {
        int count = (int)(sizeof(UNI24) / sizeof(UNI24[0]));
        for (int i = 0; i < count; i++) {
            if (UNI24[i] == uni) return &FONT24[i * 72];
        }
        return nullptr;
    }

    static void drawFallback(TFT_eSprite& sp, int x, int y, int size, uint16_t color) {
        sp.drawRect(x + 1, y + 1, size - 2, size - 2, color);  // 缺字：空心方框
    }
};

#endif // CHINESE_FONT_H