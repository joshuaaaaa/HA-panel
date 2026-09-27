#include "effects.h"
#include <math.h>
#include "config.h"

namespace Effects {

static const char* const NAMES[] = {"rainbow", "plasma", "fire", "matrix", "snow", "sparkle", "waves", "aurora", "stars"};

void list(JsonArray a) {
  for (auto n : NAMES) a.add(n);
}

static uint8_t sin8f(float x) { return (uint8_t)(127.5f + 127.5f * sinf(x)); }

// ---- per-effect state (sized for the largest matrix)
static uint8_t heat[MAX_LEDS];
static int16_t drops[128];
static uint32_t lastStep = 0;

static void rainbow(Canvas& c, uint32_t ms) {
  for (int x = 0; x < c.w; x++) {
    RGB col = hsv((uint8_t)(x * 256 / c.w + ms / 12));
    for (int y = 0; y < c.h; y++) c.set(x, y, col);
  }
}

static void plasma(Canvas& c, uint32_t ms) {
  float t = ms / 1000.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float v = sinf(x * 0.35f + t) + sinf((y * 0.6f + t) * 0.8f) + sinf((x * 0.2f + y * 0.4f + t * 1.3f)) +
                sinf(sqrtf((x - 16) * (x - 16) + (y - 4) * (y - 4)) * 0.5f - t);
      c.set(x, y, hsv((uint8_t)(v * 40 + t * 20)));
    }
}

static void fire(Canvas& c, uint32_t ms) {
  const int W = c.w, H = c.h;
  if (ms - lastStep > 40) {
    lastStep = ms;
    // cool down
    for (int i = 0; i < W * H; i++) heat[i] = max(0, heat[i] - (int)random(0, 30));
    // rise
    for (int x = 0; x < W; x++)
      for (int y = 0; y < H - 1; y++) {
        int below = heat[(y + 1) * W + x];
        int bl = heat[(y + 1) * W + (x + W - 1) % W];
        int br = heat[(y + 1) * W + (x + 1) % W];
        heat[y * W + x] = (below * 2 + bl + br) / 4;
      }
    // ignite bottom row
    for (int x = 0; x < W; x++)
      heat[(H - 1) * W + x] = random(0, 100) < 60 ? random(160, 255) : heat[(H - 1) * W + x] / 2;
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t h = heat[y * W + x];
      RGB col;
      if (h < 85) col = RGB(h * 3, 0, 0);
      else if (h < 170) col = RGB(255, (h - 85) * 3, 0);
      else col = RGB(255, 255, (h - 170) * 3);
      c.set(x, y, col);
    }
}

static void matrixFx(Canvas& c, uint32_t ms) {
  const int W = min(c.w, 128);
  if (ms - lastStep > 90) {
    lastStep = ms;
    for (int x = 0; x < W; x++) {
      if (drops[x] <= -1 || drops[x] > c.h + 6) drops[x] = random(0, 100) < 6 ? 0 : -1;
      else drops[x]++;
    }
  }
  for (int x = 0; x < W; x++) {
    if (drops[x] < 0) continue;
    for (int t = 0; t < 6; t++) {
      int y = drops[x] - t;
      if (y < 0 || y >= c.h) continue;
      c.set(x, y, t == 0 ? RGB(180, 255, 180) : RGB(0, 255 - t * 40, 0));
    }
  }
}

static void snow(Canvas& c, uint32_t ms) {
  const int W = min(c.w, 128);
  if (ms - lastStep > 180) {
    lastStep = ms;
    for (int x = 0; x < W; x++) {
      if (drops[x] < 0) drops[x] = random(0, 100) < 4 ? 0 : -1;
      else if (++drops[x] >= c.h) drops[x] = -1;
    }
  }
  for (int x = 0; x < W; x++)
    if (drops[x] >= 0) c.set(x, drops[x], RGB(200, 220, 255));
}

static void sparkle(Canvas& c, uint32_t ms) {
  randomSeed(ms / 60);
  for (int i = 0; i < c.w * c.h / 12; i++) {
    int x = random(0, c.w), y = random(0, c.h);
    c.set(x, y, hsv(random(0, 255), 180, random(80, 255)));
  }
  randomSeed(esp_random());
}

static void waves(Canvas& c, uint32_t ms) {
  float t = ms / 600.0f;
  for (int x = 0; x < c.w; x++) {
    float yy = (c.h - 1) / 2.0f + sinf(x * 0.4f + t) * (c.h / 2.5f);
    for (int y = 0; y < c.h; y++) {
      float d = fabsf(y - yy);
      if (d < 1.5f) c.set(x, y, hsv((uint8_t)(x * 6 + ms / 20), 255, (uint8_t)(255 - d * 150)));
    }
  }
}

static void aurora(Canvas& c, uint32_t ms) {
  float t = ms / 2000.0f;
  for (int x = 0; x < c.w; x++) {
    uint8_t hue = 96 + (sin8f(x * 0.15f + t) >> 2);
    for (int y = 0; y < c.h; y++) {
      uint8_t v = sin8f(x * 0.25f + y * 0.5f + t * 2.0f);
      v = (v * v) >> 8;
      c.set(x, y, hsv(hue + y * 4, 220, v));
    }
  }
}

static void stars(Canvas& c, uint32_t ms) {
  for (int i = 0; i < 24; i++) {
    uint32_t seed = i * 2654435761u;
    int x = seed % c.w, y = (seed >> 8) % c.h;
    uint8_t v = sin8f(ms / 700.0f + i * 1.7f);
    v = (v * v) >> 8;
    c.set(x, y, RGB(v, v, (uint8_t)min(255, v + 30)));
  }
}

bool render(Canvas& c, const String& name, uint32_t ms) {
  if (name == "rainbow") rainbow(c, ms);
  else if (name == "plasma") plasma(c, ms);
  else if (name == "fire") fire(c, ms);
  else if (name == "matrix") matrixFx(c, ms);
  else if (name == "snow") snow(c, ms);
  else if (name == "sparkle") sparkle(c, ms);
  else if (name == "waves") waves(c, ms);
  else if (name == "aurora") aurora(c, ms);
  else if (name == "stars") stars(c, ms);
  else return false;
  return true;
}

}  // namespace Effects
