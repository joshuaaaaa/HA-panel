#include "display.h"
#include <NeoPixelBus.h>
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
  if (rainbow) return drawTextFn(c, x, y, s, [hue0](int n) { return hsv(hue0 + n * 24); }, clip);
  return drawTextFn(c, x, y, s, [color](int) { return color; }, clip);
}

int charCount(const String& s) {
  int i = 0, n = 0;
  while (i < (int)s.length()) { nextCodepoint(s, i); n++; }
  return n;
}

void drawLine(Canvas& c, int x0, int y0, int x1, int y1, RGB col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    c.set(x0, y0, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawRect(Canvas& c, int x, int y, int w, int h, RGB col) {
  if (w <= 0 || h <= 0) return;
  drawLine(c, x, y, x + w - 1, y, col);
  drawLine(c, x, y + h - 1, x + w - 1, y + h - 1, col);
  drawLine(c, x, y, x, y + h - 1, col);
  drawLine(c, x + w - 1, y, x + w - 1, y + h - 1, col);
}

void drawCircle(Canvas& c, int cx, int cy, int r, RGB col, bool fill) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      int d = x * x + y * y;
      if (fill ? d <= r * r + r : (d <= r * r + r && d >= r * r - r)) c.set(cx + x, cy + y, col);
    }
}

int drawTextFn(Canvas& c, int x, int y, const String& s, const std::function<RGB(int)>& colorAt, Clip clip) {
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
    RGB col = colorAt(n);
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

// WS2812 driver: NeoPixelBus (same library and methods as WLED).
// The bus always sends G,R,B; the configured color order is applied by permuting
// the channels before they are handed to the bus.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3: LCD peripheral parallel output - the same method WLED uses on S3
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32LcdX8Ws2812xMethod> Bus;
#else
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> Bus;
#endif

class Strip {
 public:
  Strip(int n, int pin, const String& order) : bus_(n, pin) {
    // wire position (0,1,2) -> source channel (0=r,1=g,2=b)
    const char* o = order.length() == 3 ? order.c_str() : "GRB";
    for (int i = 0; i < 3; i++) src_[i] = o[i] == 'R' ? 0 : o[i] == 'G' ? 1 : 2;
  }
  void begin() { bus_.Begin(); }
  void setPixelColor(int i, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t c[3] = {r, g, b};
    // NeoGrbFeature transmits (G, R, B) of the RgbColor
    bus_.SetPixelColor(i, RgbColor(c[src_[1]], c[src_[0]], c[src_[2]]));
  }
  void show() { bus_.Show(); }

 private:
  Bus bus_;
  uint8_t src_[3];
};

static Strip* strip = nullptr;
static int stripPin = -1;

static uint8_t gammaTable[256];
static void initGamma() {
  for (int i = 0; i < 256; i++) gammaTable[i] = (uint8_t)(powf(i / 255.0f, 2.6f) * 255.0f + 0.5f);
}
static uint8_t curBright = 0;
static uint16_t lastCurrent = 0;
static int nLeds = 0;


static volatile uint32_t rawTestUntil = 0;
static uint32_t fpsCount = 0, fpsStart = 0;
static int fpsValue = 0;

static void fillAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < nLeds; i++) strip->setPixelColor(i, r, g, b);
  strip->show();
}

void begin() {
  nLeds = cfg.width * cfg.height;
  Serial.printf("[led] %d LEDs on GPIO%d, order %s\n", nLeds, cfg.ledPin, cfg.colorOrder.c_str());
  initGamma();
  strip = new Strip(nLeds, cfg.ledPin, cfg.colorOrder);
  strip->begin();
  stripPin = cfg.ledPin;
  // power-on self test: short dim red / green / blue flash of the whole panel
  // (if this does not appear, the problem is wiring / data pin / power, not the settings)
  fillAll(20, 0, 0);
  delay(300);
  fillAll(0, 20, 0);
  delay(300);
  fillAll(0, 0, 20);
  delay(300);
  fillAll(0, 0, 0);
}

void rawTest(uint32_t ms) { rawTestUntil = millis() + ms; }
int fps() { return fpsValue; }

// ---- pin finder: drives every free GPIO in turn (3.5 s each) with dim white
#if defined(CONFIG_IDF_TARGET_ESP32S3)
static const int8_t SCAN_PINS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
                                   38, 39, 40, 41, 42, 43, 44, 47, 48};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
static const int8_t SCAN_PINS[] = {0, 1, 3, 4, 5, 6, 7, 8, 10, 20, 21};
#else
static const int8_t SCAN_PINS[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
#endif
static const int SCAN_COUNT = sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0]);
static const uint32_t SCAN_STEP_MS = 3500;
static volatile int scanReq = 0;     // 1 = start, -1 = stop
static int scanIdx = -1;             // -1 = not scanning
static uint32_t scanStepStart = 0;


static void useStripPin(int pin) {
  if (pin == stripPin && strip) return;
  delete strip;
  strip = new Strip(nLeds, pin, cfg.colorOrder);
  strip->begin();
  stripPin = pin;
}

void pinScan(bool start) { scanReq = start ? 1 : -1; }
int pinScanCurrent() { return scanIdx >= 0 ? SCAN_PINS[scanIdx] : -1; }

static inline int mapXY(int x, int y) {
  const int W = cfg.width, H = cfg.height;
  if (cfg.startRight) x = W - 1 - x;
  if (cfg.startBottom) y = H - 1 - y;
  if (cfg.tiled) {  // 8x8 tiles side by side, each row progressive (AWTRIX layout 1)
    return (x / 8) * 64 + y * 8 + (x % 8);
  }
  if (cfg.vertical) {
    if (cfg.serpentine && (x & 1)) y = H - 1 - y;
    return x * H + y;
  }
  if (cfg.serpentine && (y & 1)) x = W - 1 - x;
  return y * W + x;
}

void show(const Canvas& c, uint8_t target) {
  if (!strip) return;
  uint32_t nowMs = millis();
  fpsCount++;
  if (nowMs - fpsStart >= 1000) {
    fpsValue = fpsCount * 1000 / (nowMs - fpsStart);
    fpsCount = 0;
    fpsStart = nowMs;
  }
  // pin finder (runs in the render task so the strip is never used concurrently)
  if (scanReq) {
    if (scanReq > 0) { scanIdx = 0; scanStepStart = nowMs; }
    else { scanIdx = -1; }
    scanReq = 0;
    if (scanIdx < 0) { fillAll(0, 0, 0); useStripPin(cfg.ledPin); }
  }
  if (scanIdx >= 0) {
    if (nowMs - scanStepStart >= SCAN_STEP_MS) {
      fillAll(0, 0, 0);
      scanStepStart = nowMs;
      if (++scanIdx >= SCAN_COUNT) {
        scanIdx = -1;
        useStripPin(cfg.ledPin);
        return;
      }
    }
    useStripPin(SCAN_PINS[scanIdx]);
    fillAll(25, 25, 25);
    return;
  }
  if ((int32_t)(rawTestUntil - nowMs) > 0) {
    // cycle red / green / blue / white on every LED, fixed low brightness
    static const uint8_t cols[4][3] = {{40, 0, 0}, {0, 40, 0}, {0, 0, 40}, {25, 25, 25}};
    const uint8_t* k = cols[(nowMs / 700) % 4];
    fillAll(k[0], k[1], k[2]);
    return;
  }
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
        p.r = gammaTable[p.r];
        p.g = gammaTable[p.g];
        p.b = gammaTable[p.b];
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
