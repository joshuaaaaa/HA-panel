#pragma once
#include <Arduino.h>

struct RGB {
  uint8_t r = 0, g = 0, b = 0;
  RGB() {}
  RGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
  explicit RGB(uint32_t c) : r(c >> 16), g(c >> 8), b(c) {}
  bool isBlack() const { return !(r | g | b); }
  RGB scale(uint8_t s) const { return RGB((r * s) >> 8, (g * s) >> 8, (b * s) >> 8); }
};

RGB hsv(uint8_t h, uint8_t s = 255, uint8_t v = 255);
RGB blend(RGB a, RGB b, uint8_t t);   // t=0 -> a, 255 -> b

class Canvas {
 public:
  int w = 0, h = 0;
  RGB* buf = nullptr;
  void alloc(int w_, int h_);
  void clear() { if (buf) memset(buf, 0, sizeof(RGB) * w * h); }
  inline void set(int x, int y, RGB c) {
    if (x >= 0 && y >= 0 && x < w && y < h) buf[y * w + x] = c;
  }
  inline RGB get(int x, int y) const {
    if (x >= 0 && y >= 0 && x < w && y < h) return buf[y * w + x];
    return RGB();
  }
  void fillRect(int x, int y, int rw, int rh, RGB c);
  void copyFrom(const Canvas& o);
};

// clipping window for text rendering (x range)
struct Clip { int x0, x1; };

// ---- text (3x5 proportional font with Czech diacritics)
int textWidth(const String& s);
// draws text; returns width. color used unless rainbow (hue shifts along text)
int drawText(Canvas& c, int x, int y, const String& s, RGB color, Clip clip, bool rainbow = false, uint8_t hue0 = 0);
// big 4x7 digits font (digits, ':', ' ', '-')
int bigTextWidth(const String& s);
int drawBigText(Canvas& c, int x, int y, const String& s, RGB color, Clip clip, uint8_t colonMask = 0xFF);
String toUpperUtf8(const String& s);

namespace Display {
void begin();
// pushes the logical canvas to the LEDs (mapping, gamma, brightness, current limit)
void show(const Canvas& c, uint8_t brightness);
uint8_t currentBrightness();
uint16_t estimatedCurrent();
int ledCount();
}
