#include "display.h"
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "font.h"

// ================================================================ color helpers
RGB hsv(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0: return RGB(v, t, p);
    case 1: return RGB(q, v, p);
    case 2: return RGB(p, v, t);
    case 3: return RGB(p, q, v);
    case 4: return RGB(t, p, v);
    default: return RGB(v, p, q);
  }
}

RGB blend(RGB a, RGB b, uint8_t t) {
  return RGB(a.r + (((int)b.r - a.r) * t) / 255, a.g + (((int)b.g - a.g) * t) / 255,
             a.b + (((int)b.b - a.b) * t) / 255);
}

void Canvas::alloc(int w_, int h_) {
  if (buf) free(buf);
  w = w_;
  h = h_;
  buf = (RGB*)calloc(w * h, sizeof(RGB));
}

void Canvas::fillRect(int x, int y, int rw, int rh, RGB c) {
  for (int yy = y; yy < y + rh; yy++)
    for (int xx = x; xx < x + rw; xx++) set(xx, yy, c);
}

void Canvas::copyFrom(const Canvas& o) {
  if (o.w == w && o.h == h) memcpy(buf, o.buf, sizeof(RGB) * w * h);
}

// ================================================================ UTF-8 + diacritics
enum Accent : uint8_t { AC_NONE = 0, AC_ACUTE, AC_CARON, AC_RING, AC_UMLAUT, AC_CIRC };

struct CharMap { uint16_t cp; char base; uint8_t accent; };
static const CharMap CHARMAP[] = {
  // Czech / Slovak
  {0xC1, 'A', AC_ACUTE}, {0xE1, 'a', AC_ACUTE}, {0x10C, 'C', AC_CARON}, {0x10D, 'c', AC_CARON},
  {0x10E, 'D', AC_CARON}, {0x10F, 'd', AC_CARON}, {0xC9, 'E', AC_ACUTE}, {0xE9, 'e', AC_ACUTE},
  {0x11A, 'E', AC_CARON}, {0x11B, 'e', AC_CARON}, {0xCD, 'I', AC_ACUTE}, {0xED, 'i', AC_ACUTE},
  {0x147, 'N', AC_CARON}, {0x148, 'n', AC_CARON}, {0xD3, 'O', AC_ACUTE}, {0xF3, 'o', AC_ACUTE},
  {0x158, 'R', AC_CARON}, {0x159, 'r', AC_CARON}, {0x160, 'S', AC_CARON}, {0x161, 's', AC_CARON},
  {0x164, 'T', AC_CARON}, {0x165, 't', AC_CARON}, {0xDA, 'U', AC_ACUTE}, {0xFA, 'u', AC_ACUTE},
  {0x16E, 'U', AC_RING}, {0x16F, 'u', AC_RING}, {0xDD, 'Y', AC_ACUTE}, {0xFD, 'y', AC_ACUTE},
  {0x17D, 'Z', AC_CARON}, {0x17E, 'z', AC_CARON}, {0x13D, 'L', AC_CARON}, {0x13E, 'l', AC_CARON},
  {0x139, 'L', AC_ACUTE}, {0x13A, 'l', AC_ACUTE}, {0x154, 'R', AC_ACUTE}, {0x155, 'r', AC_ACUTE},
  {0xD4, 'O', AC_CIRC}, {0xF4, 'o', AC_CIRC},
  // German & co
  {0xC4, 'A', AC_UMLAUT}, {0xE4, 'a', AC_UMLAUT}, {0xD6, 'O', AC_UMLAUT}, {0xF6, 'o', AC_UMLAUT},
  {0xDC, 'U', AC_UMLAUT}, {0xFC, 'u', AC_UMLAUT}, {0xEB, 'e', AC_UMLAUT}, {0xCB, 'E', AC_UMLAUT},
  // Polish (approximations)
  {0x141, 'L', AC_NONE}, {0x142, 'l', AC_NONE}, {0x15A, 'S', AC_ACUTE}, {0x15B, 's', AC_ACUTE},
  {0x106, 'C', AC_ACUTE}, {0x107, 'c', AC_ACUTE}, {0x143, 'N', AC_ACUTE}, {0x144, 'n', AC_ACUTE},
  {0x179, 'Z', AC_ACUTE}, {0x17A, 'z', AC_ACUTE}, {0x17B, 'Z', AC_NONE}, {0x17C, 'z', AC_NONE},
  {0x104, 'A', AC_NONE}, {0x105, 'a', AC_NONE}, {0x118, 'E', AC_NONE}, {0x119, 'e', AC_NONE},
  {0xDF, 'B', AC_NONE},
};

struct GlyphRef { uint8_t idx; uint8_t accent; };

static uint32_t nextCodepoint(const String& s, int& i) {
  uint8_t c = s[i++];
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  while (extra-- > 0 && i < (int)s.length()) cp = (cp << 6) | (s[i++] & 0x3F);
  return cp;
}

static GlyphRef mapCodepoint(uint32_t cp) {
  if (cp >= 32 && cp < 127) return {(uint8_t)(cp - FONT_FIRST), AC_NONE};
  switch (cp) {
    case 0xB0: return {GLYPH_DEGREE, AC_NONE};
    case 0xB2: return {GLYPH_SUP2, AC_NONE};
    case 0xB3: return {GLYPH_SUP3, AC_NONE};
    case 0x20AC: return {GLYPH_EURO, AC_NONE};
    case 0xB5: case 0x3BC: return {GLYPH_MICRO, AC_NONE};
    case 0xA0: return {0, AC_NONE};
    case 0x01: return {GLYPH_BLANK1, AC_NONE};  // invisible 1px (blinking colon)
    case 0x2013: case 0x2014: case 0x2212: return {(uint8_t)('-' - FONT_FIRST), AC_NONE};
    case 0x201E: case 0x201C: case 0x201D: return {(uint8_t)('"' - FONT_FIRST), AC_NONE};
    case 0x2018: case 0x2019: case 0x201A: return {(uint8_t)('\'' - FONT_FIRST), AC_NONE};
  }
  for (auto& m : CHARMAP) {
    if (m.cp == cp) {
      if (m.base == 'i' && m.accent != AC_NONE) return {GLYPH_DOTLESS_I, m.accent};
      return {(uint8_t)(m.base - FONT_FIRST), m.accent};
    }
  }
  return {GLYPH_UNKNOWN, AC_NONE};
}

static inline void readGlyph(uint8_t idx, Glyph5& g) { memcpy_P(&g, &FONT5[idx], sizeof(Glyph5)); }

int textWidth(const String& s) {
  int w = 0, i = 0, n = 0;
  Glyph5 g;
  while (i < (int)s.length()) {
    GlyphRef r = mapCodepoint(nextCodepoint(s, i));
    readGlyph(r.idx, g);
    w += g.w + 1;
    n++;
  }
  return n ? w - 1 : 0;
}

static void drawAccent(Canvas& c, int x, int w, int yTop, uint8_t accent, RGB col, const Clip& clip) {
  // yTop = row directly above the accent's lower row; accent occupies rows yTop-1 (upper) and yTop (lower)
  int cx = x + (w - 3) / 2;
  auto px = [&](int xx, int yy) {
    if (xx >= clip.x0 && xx <= clip.x1) c.set(xx, yy, col);
  };
  bool twoRows = yTop - 1 >= 0;
  switch (accent) {
    case AC_ACUTE:
      if (twoRows) { px(cx + 2, yTop - 1); px(cx + 1, yTop); }
      else px(cx + 2, yTop);
      break;
    case AC_CARON:
      if (twoRows) { px(cx, yTop - 1); px(cx + 2, yTop - 1); px(cx + 1, yTop); }
      else { px(cx, yTop); px(cx + 1, yTop); px(cx + 2, yTop); }
      break;
    case AC_CIRC:
      if (twoRows) { px(cx + 1, yTop - 1); px(cx, yTop); px(cx + 2, yTop); }
      else px(cx + 1, yTop);
      break;
    case AC_RING:
      if (twoRows) { px(cx + 1, yTop - 1); }
      px(cx + 1, yTop);
      break;
    case AC_UMLAUT:
      px(cx, yTop); px(cx + 2, yTop);
      break;
  }
}

int drawText(Canvas& c, int x, int y, const String& s, RGB color, Clip clip, bool rainbow, uint8_t hue0) {
  int i = 0, cx = x, n = 0;
  Glyph5 g;
  while (i < (int)s.length()) {
    GlyphRef r = mapCodepoint(nextCodepoint(s, i));
    readGlyph(r.idx, g);
    if (cx > clip.x1) {  // nothing more visible - only measure
      cx += g.w + 1;
      n++;
      continue;
    }
    RGB col = rainbow ? hsv(hue0 + n * 24) : color;
    if (cx + g.w >= clip.x0) {
      for (int row = 0; row < 5; row++) {
        uint8_t bits = g.rows[row];
        for (int col_ = 0; col_ < g.w; col_++) {
          if (bits & (0x80 >> col_)) {
            int px = cx + col_;
            if (px >= clip.x0 && px <= clip.x1) c.set(px, y + row, col);
          }
        }
      }
      if (r.accent) {
        // lowercase glyphs with empty first row can carry the accent lower
        bool low = (g.rows[0] == 0);
        drawAccent(c, cx, g.w, low ? y : y - 1, r.accent, col, clip);
      }
    }
    cx += g.w + 1;
    n++;
  }
  return n ? cx - x - 1 : 0;
}

static int bigIndex(uint32_t cp) {
  for (int i = 0; BIG_CHARS[i]; i++) if ((uint32_t)BIG_CHARS[i] == cp) return i;
  return -1;
}

int bigTextWidth(const String& s) {
  int w = 0, i = 0, n = 0;
  while (i < (int)s.length()) {
    int bi = bigIndex(nextCodepoint(s, i));
    if (bi < 0) continue;
    w += pgm_read_byte(&FONT7[bi].w) + 1;
    n++;
  }
  return n ? w - 1 : 0;
}

int drawBigText(Canvas& c, int x, int y, const String& s, RGB color, Clip clip, uint8_t colonMask) {
  int i = 0, cx = x;
  Glyph7 g;
  int colonNo = 0;
  while (i < (int)s.length()) {
    uint32_t cp = nextCodepoint(s, i);
    int bi = bigIndex(cp);
    if (bi < 0) continue;
    memcpy_P(&g, &FONT7[bi], sizeof(Glyph7));
    bool visible = true;
    if (cp == ':') visible = colonMask & (1 << colonNo++);
    if (visible) {
      for (int row = 0; row < 7; row++)
        for (int col = 0; col < g.w; col++)
          if (g.rows[row] & (0x80 >> col)) {
            int px = cx + col;
            if (px >= clip.x0 && px <= clip.x1) c.set(px, y + row, color);
          }
    }
    cx += g.w + 1;
  }
  return cx - x - 1;
}

String toUpperUtf8(const String& s) {
  String out;
  out.reserve(s.length());
  int i = 0;
  while (i < (int)s.length()) {
    int start = i;
    uint32_t cp = nextCodepoint(s, i);
    if (cp >= 'a' && cp <= 'z') { out += (char)(cp - 32); continue; }
    // Latin-1 / Latin Extended-A lowercase -> uppercase
    uint32_t up = cp;
    if ((cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)) up = cp - 0x20;
    else if (cp >= 0x100 && cp <= 0x17F) {
      // pairs: even = upper, odd = lower (with a few exceptions handled by table lookup)
      bool isLowerPair = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E) ? (cp % 2 == 0) : (cp % 2 == 1);
      if (isLowerPair) up = cp - 1;
    }
    if (up != cp) {
      if (up < 0x800) { out += (char)(0xC0 | (up >> 6)); out += (char)(0x80 | (up & 0x3F)); }
      continue;
    }
    for (int k = start; k < i; k++) out += s[k];
  }
  return out;
}

// ================================================================ LED output
namespace Display {

static Adafruit_NeoPixel* strip = nullptr;
static uint8_t curBright = 0;
static uint16_t lastCurrent = 0;
static int nLeds = 0;

static neoPixelType orderType(const String& o) {
  if (o == "RGB") return NEO_RGB;
  if (o == "RBG") return NEO_RBG;
  if (o == "BRG") return NEO_BRG;
  if (o == "BGR") return NEO_BGR;
  if (o == "GBR") return NEO_GBR;
  return NEO_GRB;
}

void begin() {
  nLeds = cfg.width * cfg.height;
  strip = new Adafruit_NeoPixel(nLeds, cfg.ledPin, orderType(cfg.colorOrder) + NEO_KHZ800);
  strip->begin();
  strip->clear();
  strip->show();
}

static inline int mapXY(int x, int y) {
  const int W = cfg.width, H = cfg.height;
  if (cfg.startRight) x = W - 1 - x;
  if (cfg.startBottom) y = H - 1 - y;
  if (cfg.vertical) {
    if (cfg.serpentine && (x & 1)) y = H - 1 - y;
    return x * H + y;
  }
  if (cfg.serpentine && (y & 1)) x = W - 1 - x;
  return y * W + x;
}

void show(const Canvas& c, uint8_t target) {
  if (!strip) return;
  // smooth brightness changes
  if (curBright < target) curBright += max(1, (target - curBright) / 6);
  else if (curBright > target) curBright -= max(1, (curBright - target) / 6);

  const int W = min<int>(c.w, cfg.width), H = min<int>(c.h, cfg.height);
  uint32_t sum = 0;
  static uint8_t* tmp = nullptr;
  static int tmpN = 0;
  if (tmpN != nLeds * 3) {
    free(tmp);
    tmp = (uint8_t*)calloc(nLeds * 3, 1);
    tmpN = nLeds * 3;
  }
  memset(tmp, 0, tmpN);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      RGB p = c.buf[y * c.w + x];
      if (cfg.gamma) {
        p.r = Adafruit_NeoPixel::gamma8(p.r);
        p.g = Adafruit_NeoPixel::gamma8(p.g);
        p.b = Adafruit_NeoPixel::gamma8(p.b);
      }
      uint8_t r = (p.r * (curBright + 1)) >> 8, g = (p.g * (curBright + 1)) >> 8, b = (p.b * (curBright + 1)) >> 8;
      // keep very dim pixels visible at low brightness
      if (curBright && !r && p.r > 32) r = 1;
      if (curBright && !g && p.g > 32) g = 1;
      if (curBright && !b && p.b > 32) b = 1;
      int idx = mapXY(x, y);
      if (idx < 0 || idx >= nLeds) continue;
      tmp[idx * 3] = r; tmp[idx * 3 + 1] = g; tmp[idx * 3 + 2] = b;
      sum += r + g + b;
    }
  }
  // automatic current limiter (~20 mA per channel at full, ~1 mA idle per LED)
  uint32_t idle = nLeds;
  uint32_t est = idle + sum * 20 / 255;
  uint16_t scale = 256;
  if (cfg.maxCurrent > 0 && est > cfg.maxCurrent && est > idle) {
    uint32_t budget = cfg.maxCurrent > idle ? cfg.maxCurrent - idle : 0;
    scale = (uint16_t)((budget * 256) / (est - idle));
    est = idle + (est - idle) * scale / 256;
  }
  lastCurrent = est;
  for (int i = 0; i < nLeds; i++) {
    uint8_t* t = &tmp[i * 3];
    if (scale < 256) strip->setPixelColor(i, (t[0] * scale) >> 8, (t[1] * scale) >> 8, (t[2] * scale) >> 8);
    else strip->setPixelColor(i, t[0], t[1], t[2]);
  }
  strip->show();
}

uint8_t currentBrightness() { return curBright; }
uint16_t estimatedCurrent() { return lastCurrent; }
int ledCount() { return nLeds; }

}  // namespace Display
