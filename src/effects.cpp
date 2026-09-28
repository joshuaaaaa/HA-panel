#include "effects.h"
#include <math.h>
#include "config.h"
#include "eyes.h"

// Animated backgrounds / full-screen effects for the matrix.
// Inspired by the AWTRIX 3 effect set (Fireworks, Ripple, Snake, PingPong, BrickBreaker, Radar,
// Checkerboard, TheaterChase, ColorWaves, SwirlIn/Out, Pacifica, PlasmaCloud, MovingLine, Fade,
// TwinklingStars, ...) and popular WLED 2D effects. All effects are written for any w x h canvas.

namespace Effects {

static uint8_t sin8f(float x) { return (uint8_t)(127.5f + 127.5f * sinf(x)); }
static float frand(float a, float b) { return a + (b - a) * (float)(esp_random() % 10000) / 10000.0f; }

// ------------------------------------------------------------------ shared state
// Only one effect runs at a time (per frame); state is reset whenever the effect changes.
#define NP 48
static struct State {
  uint8_t a[MAX_LEDS], b[MAX_LEDS];
  RGB trail[MAX_LEDS];
  float px[NP], py[NP], vx[NP], vy[NP];
  uint8_t hue[NP];
  int16_t life[NP];
  int16_t drops[128];
  int16_t sx[64], sy[64];
  int n, dir, gen, score;
  float f1, f2, f3;
  uint32_t last, t0;
} S;
static const char* curName = nullptr;
static uint32_t lastMs = 0;

static bool step(uint32_t ms, uint32_t interval) {
  if (ms - S.last < interval) return false;
  S.last = ms;
  return true;
}
// trail buffer helpers: fade, plot, blit
static void tFade(Canvas& c, uint8_t keep) {
  for (int i = 0; i < c.w * c.h; i++) S.trail[i] = S.trail[i].scale(keep);
}
static void tSet(Canvas& c, int x, int y, RGB col) {
  if (x >= 0 && y >= 0 && x < c.w && y < c.h) S.trail[y * c.w + x] = col;
}
static void tAdd(Canvas& c, int x, int y, RGB col) {
  if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
  RGB& t = S.trail[y * c.w + x];
  t = RGB(min(255, t.r + col.r), min(255, t.g + col.g), min(255, t.b + col.b));
}
static void tBlit(Canvas& c) {
  for (int i = 0; i < c.w * c.h; i++)
    if (!S.trail[i].isBlack()) c.buf[i] = S.trail[i];
}
static void drawSprite(Canvas& c, int x, int y, const uint8_t* rows, int w, int h, RGB col) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      if (rows[j] & (1 << (w - 1 - i))) c.set(x + i, y + j, col);
}

// ------------------------------------------------------------------ classic
static void rainbow(Canvas& c, uint32_t ms) {
  for (int x = 0; x < c.w; x++) {
    RGB col = hsv((uint8_t)(x * 256 / c.w + ms / 12));
    for (int y = 0; y < c.h; y++) c.set(x, y, col);
  }
}

static void rainbowDiag(Canvas& c, uint32_t ms) {
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) c.set(x, y, hsv((uint8_t)((x + y * 2) * 6 + ms / 10)));
}

static void plasma(Canvas& c, uint32_t ms) {
  float t = ms / 1000.0f;
  float cx = c.w / 2.0f, cy = c.h / 2.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float v = sinf(x * 0.35f + t) + sinf((y * 0.6f + t) * 0.8f) + sinf((x * 0.2f + y * 0.4f + t * 1.3f)) +
                sinf(sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) * 0.5f - t);
      c.set(x, y, hsv((uint8_t)(v * 40 + t * 20)));
    }
}

static void plasmaCloud(Canvas& c, uint32_t ms) {
  float t = ms / 1800.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float v = sinf(x * 0.21f + t) + sinf(y * 0.5f - t * 1.3f) + sinf((x + y) * 0.17f + t * 0.7f) +
                sinf(sqrtf(x * x * 0.04f + y * y * 0.3f) + t);
      uint8_t val = (uint8_t)(128 + v * 30);
      c.set(x, y, hsv((uint8_t)(150 + v * 22), 200, val));
    }
}

static void fire(Canvas& c, uint32_t ms) {
  const int W = c.w, H = c.h;
  uint8_t* heat = S.a;
  if (step(ms, 40)) {
    for (int i = 0; i < W * H; i++) heat[i] = max(0, heat[i] - (int)random(0, 30));
    for (int x = 0; x < W; x++)
      for (int y = 0; y < H - 1; y++) {
        int below = heat[(y + 1) * W + x];
        int bl = heat[(y + 1) * W + (x + W - 1) % W];
        int br = heat[(y + 1) * W + (x + 1) % W];
        heat[y * W + x] = (below * 2 + bl + br) / 4;
      }
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
  if (step(ms, 90)) {
    for (int x = 0; x < W; x++) {
      if (S.drops[x] <= 0 || S.drops[x] > c.h + 6) S.drops[x] = random(0, 100) < 6 ? 1 : 0;
      else S.drops[x]++;
    }
  }
  for (int x = 0; x < W; x++) {
    if (S.drops[x] <= 0) continue;
    for (int t = 0; t < 6; t++) {
      int y = S.drops[x] - 1 - t;
      if (y < 0 || y >= c.h) continue;
      c.set(x, y, t == 0 ? RGB(180, 255, 180) : RGB(0, 255 - t * 40, 0));
    }
  }
}

static void snow(Canvas& c, uint32_t ms) {
  const int W = min(c.w, 128);
  if (step(ms, 180)) {
    for (int x = 0; x < W; x++) {
      if (S.drops[x] <= 0) S.drops[x] = random(0, 100) < 4 ? 1 : 0;
      else if (++S.drops[x] > c.h) S.drops[x] = 0;
    }
  }
  for (int x = 0; x < W; x++)
    if (S.drops[x] > 0) c.set(x, S.drops[x] - 1, RGB(200, 220, 255));
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

// ------------------------------------------------------------------ AWTRIX style
static void twinkle(Canvas& c, uint32_t ms) {
  // colourful twinkling stars (AWTRIX TwinklingStars)
  if (step(ms, 30)) {
    tFade(c, 235);
    if (random(0, 100) < 35) tSet(c, random(0, c.w), random(0, c.h), hsv(random(0, 256), 150, 255));
  }
  tBlit(c);
}

static void fireworks(Canvas& c, uint32_t ms) {
  // particle 0..3 = rockets (life>0 while flying), others = sparks
  if (!step(ms, 30)) { tBlit(c); return; }
  tFade(c, 200);
  for (int i = 0; i < NP; i++) {
    if (S.life[i] <= 0) continue;
    S.px[i] += S.vx[i];
    S.py[i] += S.vy[i];
    if (i < 3) {  // rocket
      S.vy[i] += 0.02f;
      tSet(c, (int)S.px[i], (int)S.py[i], RGB(255, 200, 120));
      if (S.vy[i] >= -0.05f || S.py[i] < 1) {  // explode
        S.life[i] = 0;
        uint8_t hue = random(0, 256);
        int made = 0;
        for (int j = 3; j < NP && made < 14; j++) {
          if (S.life[j] > 0) continue;
          float a = made * 6.2832f / 14 + frand(0, 0.3f), sp = frand(0.25f, 0.6f);
          S.px[j] = S.px[i];
          S.py[j] = S.py[i];
          S.vx[j] = cosf(a) * sp * 1.6f;
          S.vy[j] = sinf(a) * sp;
          S.hue[j] = hue + random(0, 20);
          S.life[j] = random(18, 30);
          made++;
        }
      }
    } else {
      S.vy[i] += 0.025f;
      S.vx[i] *= 0.96f;
      S.life[i]--;
      tAdd(c, (int)roundf(S.px[i]), (int)roundf(S.py[i]), hsv(S.hue[i], 200, min(255, S.life[i] * 14)));
    }
  }
  for (int i = 0; i < 3; i++)
    if (S.life[i] <= 0 && random(0, 100) < 4) {
      S.px[i] = frand(3, c.w - 3);
      S.py[i] = c.h - 1;
      S.vx[i] = frand(-0.2f, 0.2f);
      S.vy[i] = -frand(0.28f, 0.38f);
      S.life[i] = 1;
    }
  tBlit(c);
}

static void ripple(Canvas& c, uint32_t ms) {
  // up to 4 expanding rings
  if (step(ms, 40)) {
    for (int i = 0; i < 4; i++) {
      if (S.life[i] > 0) S.life[i]++;
      if (S.life[i] > 40) S.life[i] = 0;
      if (S.life[i] == 0 && random(0, 100) < 3) {
        S.px[i] = random(0, c.w);
        S.py[i] = random(0, c.h);
        S.hue[i] = random(0, 256);
        S.life[i] = 1;
      }
    }
  }
  for (int i = 0; i < 4; i++) {
    if (S.life[i] <= 0) continue;
    float r = S.life[i] * 0.45f;
    uint8_t v = 255 - S.life[i] * 6;
    for (int y = 0; y < c.h; y++)
      for (int x = 0; x < c.w; x++) {
        float d = fabsf(sqrtf((x - S.px[i]) * (x - S.px[i]) + (y - S.py[i]) * (y - S.py[i])) - r);
        if (d < 1) {
          RGB col = hsv(S.hue[i], 220, (uint8_t)(v * (1 - d)));
          RGB cur = c.get(x, y);
          c.set(x, y, RGB(max(cur.r, col.r), max(cur.g, col.g), max(cur.b, col.b)));
        }
      }
  }
}

static void snake(Canvas& c, uint32_t ms) {
  // S.sx/sy body (0 = head), S.n length, S.f1/f2 food, S.dir 0..3
  static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
  if (S.n == 0) {
    S.n = 3;
    for (int i = 0; i < 3; i++) { S.sx[i] = 5 - i; S.sy[i] = c.h / 2; }
    S.f1 = random(0, c.w);
    S.f2 = random(0, c.h);
  }
  if (step(ms, 110)) {
    int hx = S.sx[0], hy = S.sy[0];
    // steer towards food, avoid own body
    int best = -1, bestD = 9999;
    for (int k = 0; k < 4; k++) {
      if ((k + 2) % 4 == S.dir) continue;
      int nx = (hx + DX[k] + c.w) % c.w, ny = (hy + DY[k] + c.h) % c.h;
      bool hit = false;
      for (int i = 0; i < S.n - 1; i++) if (S.sx[i] == nx && S.sy[i] == ny) hit = true;
      if (hit) continue;
      int d = abs(nx - (int)S.f1) + abs(ny - (int)S.f2) + (random(0, 100) < 10 ? 3 : 0);
      if (d < bestD) { bestD = d; best = k; }
    }
    if (best < 0) { S.n = 0; return; }  // trapped -> restart
    S.dir = best;
    int nx = (hx + DX[best] + c.w) % c.w, ny = (hy + DY[best] + c.h) % c.h;
    bool eat = nx == (int)S.f1 && ny == (int)S.f2;
    if (eat && S.n < 60) S.n++;
    for (int i = S.n - 1; i > 0; i--) { S.sx[i] = S.sx[i - 1]; S.sy[i] = S.sy[i - 1]; }
    S.sx[0] = nx;
    S.sy[0] = ny;
    if (eat) {
      S.f1 = random(0, c.w);
      S.f2 = random(0, c.h);
      if (S.n >= 40) S.n = 0;
    }
  }
  c.set((int)S.f1, (int)S.f2, RGB(255, 40, 0));
  for (int i = S.n - 1; i >= 0; i--)
    c.set(S.sx[i], S.sy[i], i == 0 ? RGB(180, 255, 80) : hsv(96 - i * 2, 255, max(60, 230 - i * 5)));
}

static void pingpong(Canvas& c, uint32_t ms) {
  if (S.t0 == 0) {
    S.t0 = 1;
    S.px[0] = c.w / 2;
    S.py[0] = c.h / 2;
    S.vx[0] = 0.55f;
    S.vy[0] = 0.33f;
    S.f1 = S.f2 = c.h / 2;
  }
  if (step(ms, 30)) {
    S.px[0] += S.vx[0];
    S.py[0] += S.vy[0];
    if (S.py[0] < 0) { S.py[0] = -S.py[0]; S.vy[0] = -S.vy[0]; }
    if (S.py[0] > c.h - 1) { S.py[0] = 2 * (c.h - 1) - S.py[0]; S.vy[0] = -S.vy[0]; }
    if (S.px[0] < 1) { S.px[0] = 1; S.vx[0] = fabsf(S.vx[0]); S.vy[0] += frand(-0.1f, 0.1f); }
    if (S.px[0] > c.w - 2) { S.px[0] = c.w - 2; S.vx[0] = -fabsf(S.vx[0]); S.vy[0] += frand(-0.1f, 0.1f); }
    // paddles follow the ball (the one it moves towards reacts faster)
    float kL = S.vx[0] < 0 ? 0.25f : 0.05f, kR = S.vx[0] > 0 ? 0.25f : 0.05f;
    S.f1 += (S.py[0] - S.f1) * kL;
    S.f2 += (S.py[0] - S.f2) * kR;
  }
  for (int i = -1; i <= 1; i++) {
    c.set(0, (int)roundf(S.f1) + i, RGB(0, 120, 255));
    c.set(c.w - 1, (int)roundf(S.f2) + i, RGB(255, 60, 0));
  }
  c.set((int)roundf(S.px[0]), (int)roundf(S.py[0]), RGB(255, 255, 255));
}

static void brickbreaker(Canvas& c, uint32_t ms) {
  const int rows = max(2, c.h / 4);
  uint8_t* br = S.a;  // brick alive flags per cell (rows x w)
  if (S.t0 == 0 || S.n == 0) {
    S.t0 = 1;
    S.n = 0;
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < c.w; x++) { br[y * c.w + x] = 1; S.n++; }
    S.px[0] = c.w / 2;
    S.py[0] = c.h - 2;
    S.vx[0] = 0.45f;
    S.vy[0] = -0.4f;
    S.f1 = c.w / 2;
  }
  if (step(ms, 30)) {
    float nx = S.px[0] + S.vx[0], ny = S.py[0] + S.vy[0];
    if (nx < 0 || nx > c.w - 1) { S.vx[0] = -S.vx[0]; nx = S.px[0]; }
    if (ny < 0) { S.vy[0] = -S.vy[0]; ny = S.py[0]; }
    int bx = (int)roundf(nx), by = (int)roundf(ny);
    if (by >= 0 && by < rows && bx >= 0 && bx < c.w && br[by * c.w + bx]) {
      br[by * c.w + bx] = 0;
      S.n--;
      S.vy[0] = -S.vy[0];
      ny = S.py[0];
    }
    if (ny >= c.h - 2 && S.vy[0] > 0) {
      if (fabsf(nx - S.f1) <= 2.5f) {
        S.vy[0] = -S.vy[0];
        S.vx[0] += (nx - S.f1) * 0.08f;
        S.vx[0] = constrain(S.vx[0], -0.7f, 0.7f);
        ny = c.h - 2;
      } else if (ny > c.h - 1) {
        nx = c.w / 2;
        ny = c.h - 3;
        S.vy[0] = -0.4f;
      }
    }
    S.px[0] = nx;
    S.py[0] = ny;
    S.f1 += (S.px[0] - S.f1) * 0.18f;
    S.f1 = constrain(S.f1, 2.0f, c.w - 3.0f);
  }
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < c.w; x++)
      if (br[y * c.w + x]) c.set(x, y, hsv((uint8_t)(y * 40 + (x / 4) * 20), 255, 200));
  for (int i = -2; i <= 2; i++) c.set((int)roundf(S.f1) + i, c.h - 1, RGB(200, 200, 200));
  c.set((int)roundf(S.px[0]), (int)roundf(S.py[0]), RGB(255, 255, 255));
}

static void radar(Canvas& c, uint32_t ms) {
  float cx = (c.w - 1) / 2.0f, cy = c.h - 0.5f;
  // sweep across the upper half-plane
  float a = 3.14159f - fabsf(fmodf(ms / 1100.0f, 6.28318f) - 3.14159f);  // 0..pi..0
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float dx = (x - cx) / 3.5f, dy = cy - y;
      float r = sqrtf(dx * dx + dy * dy);
      if (r > c.h) continue;
      float pa = atan2f(dy, dx);
      float d = fabsf(pa - a);
      uint8_t v = d < 0.6f ? (uint8_t)(255 * (1 - d / 0.6f)) : 0;
      RGB col(0, (uint8_t)max((int)v, (fabsf(r - 4) < 0.3f || fabsf(r - 7.6f) < 0.3f) ? 30 : 0), 0);
      c.set(x, y, col);
    }
  // blips
  for (int i = 0; i < 3; i++) {
    uint32_t sd = (ms / 7000 + i) * 2654435761u;
    int bx = sd % c.w, by = (sd >> 12) % (c.h - 1);
    float dx = (bx - cx) / 3.5f, dy = cy - by;
    float d = fabsf(atan2f(dy, dx) - a);
    if (d < 1.2f) c.set(bx, by, RGB(255, 255, 0).scale((uint8_t)(255 * (1 - d / 1.2f))));
  }
}

static void checkerboard(Canvas& c, uint32_t ms) {
  int ph = (ms / 600) & 1;
  uint8_t hue = ms / 40;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      bool on = (((x / 4) + (y / 4) + ph) & 1);
      c.set(x, y, on ? hsv(hue, 255, 200) : hsv(hue + 128, 255, 60));
    }
}

static void theater(Canvas& c, uint32_t ms) {
  int off = (ms / 120) % 3;
  uint8_t hue = ms / 30;
  for (int x = 0; x < c.w; x++)
    if ((x + off) % 3 == 0)
      for (int y = 0; y < c.h; y++) c.set(x, y, hsv(hue + x * 4));
}

static void colorwaves(Canvas& c, uint32_t ms) {
  float t = ms / 1000.0f;
  for (int x = 0; x < c.w; x++) {
    uint8_t hue = (uint8_t)(sin8f(x * 0.12f + t * 0.7f) / 2 + t * 25);
    for (int y = 0; y < c.h; y++) {
      uint8_t v = sin8f(x * 0.3f - t * 2.2f + y * 0.15f);
      c.set(x, y, hsv(hue + y * 3, 230, 60 + (v * 195 >> 8)));
    }
  }
}

static void swirl(Canvas& c, uint32_t ms, bool in) {
  float cx = (c.w - 1) / 2.0f, cy = (c.h - 1) / 2.0f;
  float t = ms / 25.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float dx = x - cx, dy = (y - cy) * 2;
      float d = sqrtf(dx * dx + dy * dy);
      float a = atan2f(dy, dx) * 40.7f;  // 256 / 2pi
      c.set(x, y, hsv((uint8_t)(a + (in ? d * 10 + t : -d * 10 + t) )));
    }
}
static void swirlIn(Canvas& c, uint32_t ms) { swirl(c, ms, true); }
static void swirlOut(Canvas& c, uint32_t ms) { swirl(c, ms, false); }

static void pacifica(Canvas& c, uint32_t ms) {
  float t = ms / 1000.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float w1 = sinf(x * 0.23f + t * 0.9f + y * 0.3f), w2 = sinf(x * 0.11f - t * 0.6f + y * 0.5f);
      float w3 = sinf(x * 0.37f + t * 1.7f - y * 0.2f);
      float v = (w1 + w2 + w3 * 0.6f) / 2.6f;  // -1..1
      int b = 60 + (int)(v * 70), g = 25 + (int)(v * 45);
      RGB col((uint8_t)0, (uint8_t)max(0, g), (uint8_t)max(0, b));
      if (v > 0.72f) {  // whitecaps
        int wv = (int)((v - 0.72f) * 500);
        col = RGB(min(255, wv), min(255, g + wv), min(255, b + wv));
      }
      c.set(x, y, col);
    }
}

static void movingLine(Canvas& c, uint32_t ms) {
  float p = (sinf(ms / 700.0f) + 1) / 2 * (c.w - 1);
  uint8_t hue = ms / 25;
  for (int x = 0; x < c.w; x++) {
    float d = fabsf(x - p);
    if (d < 4) {
      RGB col = hsv(hue, 255, (uint8_t)(255 * (1 - d / 4) * (1 - d / 4)));
      for (int y = 0; y < c.h; y++) c.set(x, y, col);
    }
  }
}

static void fade(Canvas& c, uint32_t ms) {
  RGB col = hsv((uint8_t)(ms / 60), 255, sin8f(ms / 900.0f) / 2 + 80);
  c.fillRect(0, 0, c.w, c.h, col);
}

// ------------------------------------------------------------------ WLED style 2D
static void life(Canvas& c, uint32_t ms) {
  uint8_t *g = S.a, *n = S.b;
  const int W = c.w, H = c.h;
  if (S.gen == 0) {
    for (int i = 0; i < W * H; i++) g[i] = random(0, 100) < 35;
    S.gen = 1;
    S.hue[0] = random(0, 256);
  }
  if (step(ms, 180)) {
    int alive = 0, changed = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        int k = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) k += g[((y + dy + H) % H) * W + (x + dx + W) % W] ? 1 : 0;
        uint8_t cur = g[y * W + x];
        uint8_t nv = cur ? (k == 2 || k == 3 ? min(250, cur + 1) : 0) : (k == 3 ? 1 : 0);
        n[y * W + x] = nv;
        alive += nv > 0;
        changed += (nv > 0) != (cur > 0);
      }
    memcpy(g, n, W * H);
    if (++S.gen > 250 || alive < 4 || changed == 0) S.gen = 0;
  }
  for (int i = 0; i < W * H; i++)
    if (g[i]) c.buf[i] = hsv(S.hue[0] + min(120, (int)g[i] * 6), 230, 220);
}

static void metaballsImpl(Canvas& c, uint32_t ms, bool lava) {
  float t = ms / 1000.0f;
  float bx[3], by[3];
  for (int i = 0; i < 3; i++) {
    bx[i] = (c.w - 1) * (0.5f + 0.45f * sinf(t * (0.4f + i * 0.17f) + i * 2.1f));
    by[i] = (c.h - 1) * (0.5f + 0.5f * sinf(t * (0.6f + i * 0.23f) + i * 1.3f));
  }
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float s = 0;
      for (int i = 0; i < 3; i++) {
        float dx = (x - bx[i]) * 0.45f, dy = y - by[i];
        s += 3.2f / (dx * dx + dy * dy + 0.6f);
      }
      if (lava) {
        if (s < 0.6f) c.set(x, y, RGB(25, 0, 10));
        else {
          int v = min(255, (int)(s * 140));
          c.set(x, y, RGB(255, (uint8_t)(max(0, v - 120)), 0).scale(min(255, 90 + v)));
        }
      } else {
        if (s > 0.5f) c.set(x, y, hsv((uint8_t)(s * 60 + t * 30), 230, (uint8_t)min(255.0f, s * 180)));
      }
    }
}
static void metaballs(Canvas& c, uint32_t ms) { metaballsImpl(c, ms, false); }
static void lava(Canvas& c, uint32_t ms) { metaballsImpl(c, ms, true); }

static void bounce(Canvas& c, uint32_t ms) {
  const int N = 5;
  if (S.t0 == 0) {
    S.t0 = 1;
    for (int i = 0; i < N; i++) {
      S.px[i] = frand(0, c.w - 1);
      S.py[i] = frand(0, c.h - 1);
      S.vx[i] = frand(-0.5f, 0.5f);
      S.vy[i] = frand(-0.3f, 0.3f);
      S.hue[i] = i * 256 / N;
    }
  }
  if (step(ms, 25)) {
    tFade(c, 150);
    for (int i = 0; i < N; i++) {
      S.vy[i] += 0.03f;
      S.px[i] += S.vx[i];
      S.py[i] += S.vy[i];
      if (S.px[i] < 0) { S.px[i] = 0; S.vx[i] = fabsf(S.vx[i]); }
      if (S.px[i] > c.w - 1) { S.px[i] = c.w - 1; S.vx[i] = -fabsf(S.vx[i]); }
      if (S.py[i] > c.h - 1) {
        S.py[i] = c.h - 1;
        S.vy[i] = -fabsf(S.vy[i]) * 0.92f;
        if (fabsf(S.vy[i]) < 0.25f) S.vy[i] = -frand(0.5f, 0.62f);  // re-kick
      }
      tSet(c, (int)roundf(S.px[i]), (int)roundf(S.py[i]), hsv(S.hue[i]));
    }
  }
  tBlit(c);
}

static void rain(Canvas& c, uint32_t ms) {
  if (step(ms, 45)) {
    tFade(c, 120);
    for (int i = 0; i < NP; i++) {
      if (S.life[i] > 0) {
        S.py[i] += S.vy[i];
        if (S.py[i] >= c.h - 1) {  // splash
          int x = (int)S.px[i];
          tAdd(c, x - 1, c.h - 1, RGB(20, 60, 140));
          tAdd(c, x + 1, c.h - 1, RGB(20, 60, 140));
          S.life[i] = 0;
        } else {
          tSet(c, (int)S.px[i], (int)S.py[i], RGB(60, 120, 255));
        }
      } else if (random(0, 100) < 3) {
        S.px[i] = random(0, c.w);
        S.py[i] = 0;
        S.vy[i] = frand(0.4f, 0.8f);
        S.life[i] = 1;
      }
    }
  }
  tBlit(c);
}

static const uint8_t HEART5[5] = {0b01010, 0b11111, 0b11111, 0b01110, 0b00100};
static const uint8_t HEART7[6] = {0b0110110, 0b1111111, 0b1111111, 0b0111110, 0b0011100, 0b0001000};

static void hearts(Canvas& c, uint32_t ms) {
  const int N = 6;
  if (step(ms, 60)) {
    for (int i = 0; i < N; i++) {
      if (S.life[i] > 0) {
        S.py[i] -= S.vy[i];
        if (S.py[i] < -6) S.life[i] = 0;
      } else if (random(0, 100) < 4) {
        S.px[i] = random(-2, c.w - 3);
        S.py[i] = c.h;
        S.vy[i] = frand(0.08f, 0.2f);
        S.hue[i] = random(0, 100) < 70 ? 250 + random(0, 12) : 210 + random(0, 30);
        S.life[i] = 1;
      }
    }
  }
  for (int i = 0; i < N; i++)
    if (S.life[i] > 0) {
      float wob = sinf(ms / 400.0f + i) * 1.2f;
      drawSprite(c, (int)(S.px[i] + wob), (int)S.py[i], HEART5, 5, 5, hsv(S.hue[i], 230, 255));
    }
}

static void heartbeat(Canvas& c, uint32_t ms) {
  // two pulses per cycle (lub-dub)
  uint32_t p = ms % 1100;
  float k = p < 120 ? p / 120.0f : p < 260 ? 1 - (p - 120) / 140.0f * 0.6f : p < 380 ? 0.4f + (p - 260) / 120.0f * 0.5f
          : p < 700 ? 0.9f - (p - 380) / 320.0f * 0.9f : 0;
  RGB col = RGB(255, 0, 40).scale((uint8_t)(60 + k * 195));
  int x0 = (c.w - 7) / 2, y0 = (c.h - 6) / 2;
  drawSprite(c, x0, y0, HEART7, 7, 6, col);
  if (k > 0.8f) {
    drawSprite(c, x0 - 9, y0 + 1, HEART5, 5, 5, RGB(120, 0, 20));
    drawSprite(c, x0 + 11, y0 + 1, HEART5, 5, 5, RGB(120, 0, 20));
  }
}

static void pacman(Canvas& c, uint32_t ms) {
  // pacman runs right eating dots, chased by a ghost; then the ghost turns blue and flees
  int period = c.w + 24;
  int pos = (ms / 90) % (period * 2);
  bool back = pos >= period;
  int p = back ? period - (pos - period) : pos;
  int px = p - 10;
  int cy = (c.h - 1) / 2;
  // dots
  for (int x = 1; x < c.w; x += 3)
    if (back ? x < px : x > px + 3) c.set(x, cy, RGB(255, 180, 120));
  bool mouth = (ms / 150) & 1;
  for (int y = -3; y <= 3; y++)
    for (int x = -3; x <= 3; x++) {
      if (x * x + y * y > 11) continue;
      int fx = back ? -x : x;
      if (mouth && fx > 0 && abs(y) <= fx) continue;
      c.set(px + x, cy + y, RGB(255, 220, 0));
    }
  // ghost
  static const uint8_t GHOST[7] = {0b0011100, 0b0111110, 0b1101011, 0b1111111, 0b1111111, 0b1111111, 0b1010101};
  int gx = back ? px + 9 : px - 14;
  RGB gcol = back ? RGB(40, 40, 255) : RGB(255, 0, 0);
  drawSprite(c, gx, cy - 3, GHOST, 7, 7, gcol);
  if (!back) {
    c.set(gx + 2, cy - 1, RGB(255, 255, 255));
    c.set(gx + 5, cy - 1, RGB(255, 255, 255));
    c.set(gx + 3, cy - 1, RGB(0, 0, 200));
    c.set(gx + 6, cy - 1, RGB(0, 0, 200));
  }
}

static void invaders(Canvas& c, uint32_t ms) {
  static const uint8_t INV_A[2][6] = {{0b00100100, 0b01111110, 0b11011011, 0b11111111, 0b10100101, 0b00100100},
                                      {0b00100100, 0b01111110, 0b11011011, 0b11111111, 0b01000010, 0b10000001}};
  int frame = (ms / 400) & 1;
  int off = (int)((ms / 250) % 10);
  off = off < 5 ? off : 9 - off;
  for (int i = 0; i < 3; i++) {
    int x = i * 11 + off - 1;
    drawSprite(c, x, 0, INV_A[frame], 8, 6, hsv((uint8_t)(i * 70 + 80), 255, 230));
  }
  // cannon + shot
  int cx = (int)((sinf(ms / 900.0f) + 1) / 2 * (c.w - 3)) + 1;
  c.set(cx, c.h - 1, RGB(0, 255, 0));
  c.set(cx - 1, c.h - 1, RGB(0, 255, 0));
  c.set(cx + 1, c.h - 1, RGB(0, 255, 0));
  int sy = c.h - 2 - (int)((ms / 60) % c.h);
  c.set(cx, sy, RGB(255, 255, 255));
}

static void dna(Canvas& c, uint32_t ms) {
  float t = ms / 400.0f;
  for (int x = 0; x < c.w; x++) {
    float a = x * 0.45f + t;
    float y1 = (c.h - 1) / 2.0f * (1 + sinf(a)), y2 = (c.h - 1) / 2.0f * (1 - sinf(a));
    bool front = cosf(a) > 0;
    if (x % 3 == 0) {
      int ya = (int)roundf(min(y1, y2)), yb = (int)roundf(max(y1, y2));
      for (int y = ya + 1; y < yb; y++) c.set(x, y, RGB(40, 40, 60));
    }
    c.set(x, (int)roundf(front ? y2 : y1), hsv((uint8_t)(x * 8 + ms / 20) + 128, 230, 110));
    c.set(x, (int)roundf(front ? y1 : y2), hsv((uint8_t)(x * 8 + ms / 20), 230, 255));
  }
}

static void equalizer(Canvas& c, uint32_t ms) {
  const int bars = c.w / 2;
  if (step(ms, 60)) {
    for (int i = 0; i < bars && i < NP; i++) {
      float target = (sinf(ms / 300.0f + i * 0.7f) * 0.3f + 0.5f + frand(-0.3f, 0.3f)) * c.h;
      S.px[i] += (target - S.px[i]) * 0.5f;
      S.px[i] = constrain(S.px[i], 0.0f, (float)c.h);
      if (S.px[i] > S.py[i]) S.py[i] = S.px[i];
      else S.py[i] -= 0.15f;
    }
  }
  for (int i = 0; i < bars && i < NP; i++) {
    int h = (int)roundf(S.px[i]);
    for (int y = 0; y < h; y++) {
      RGB col = y < c.h / 2 ? RGB(0, 255, 0) : y < c.h * 3 / 4 ? RGB(255, 200, 0) : RGB(255, 0, 0);
      c.set(i * 2, c.h - 1 - y, col);
    }
    int pk = (int)roundf(S.py[i]);
    if (pk > 0) c.set(i * 2, c.h - pk, RGB(255, 255, 255));
  }
}

static void starfield(Canvas& c, uint32_t ms) {
  const int N = 24;
  float cx = (c.w - 1) / 2.0f, cy = (c.h - 1) / 2.0f;
  if (step(ms, 30)) {
    tFade(c, 90);
    for (int i = 0; i < N; i++) {
      // px,py = direction, vx = distance, vy = speed
      if (S.life[i] == 0 || S.vx[i] > 20) {
        float a = frand(0, 6.2832f);
        S.px[i] = cosf(a);
        S.py[i] = sinf(a) * 0.4f;
        S.vx[i] = frand(0, 3);
        S.vy[i] = frand(1.02f, 1.08f);
        S.life[i] = 1;
      }
      S.vx[i] = S.vx[i] * S.vy[i] + 0.05f;
      int x = (int)roundf(cx + S.px[i] * S.vx[i]), y = (int)roundf(cy + S.py[i] * S.vx[i] * 1.2f);
      if (x < 0 || x >= c.w || y < 0 || y >= c.h) { S.life[i] = 0; continue; }
      uint8_t v = (uint8_t)min(255.0f, 40 + S.vx[i] * 18);
      tSet(c, x, y, RGB(v, v, v));
    }
  }
  tBlit(c);
}

static void confetti(Canvas& c, uint32_t ms) {
  if (step(ms, 25)) {
    tFade(c, 230);
    for (int k = 0; k < 2; k++)
      if (random(0, 100) < 60) tSet(c, random(0, c.w), random(0, c.h), hsv((uint8_t)(ms / 30 + random(0, 64)), 220, 255));
  }
  tBlit(c);
}

static void comet(Canvas& c, uint32_t ms) {
  if (step(ms, 20)) {
    tFade(c, 205);
    float t = ms / 1000.0f;
    float x = fmodf(t * 14, c.w + 10) - 5;
    float y = (c.h - 1) / 2.0f + sinf(t * 3) * (c.h / 2.2f);
    tSet(c, (int)roundf(x), (int)roundf(y), RGB(255, 255, 255));
    tAdd(c, (int)roundf(x) - 1, (int)roundf(y), hsv((uint8_t)(ms / 20), 255, 180));
  }
  tBlit(c);
}

static void scanner(Canvas& c, uint32_t ms) {
  // Knight Rider / larson scanner
  float p = (sinf(ms / 450.0f) + 1) / 2 * (c.w - 1);
  int y0 = c.h / 2 - 1;
  for (int x = 0; x < c.w; x++) {
    float d = fabsf(x - p);
    if (d < 6) {
      RGB col = RGB(255, 0, 0).scale((uint8_t)(255 * (1 - d / 6) * (1 - d / 6)));
      c.set(x, y0, col);
      c.set(x, y0 + 1, col);
    }
  }
}

static void hypno(Canvas& c, uint32_t ms) {
  float cx = (c.w - 1) / 2.0f, cy = (c.h - 1) / 2.0f;
  float t = ms / 150.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float d = sqrtf((x - cx) * (x - cx) * 0.4f + (y - cy) * (y - cy));
      float v = sinf(d * 1.8f - t);
      if (v > 0) c.set(x, y, hsv((uint8_t)(ms / 30), 200, (uint8_t)(v * 255)));
    }
}

static void police(Canvas& c, uint32_t ms) {
  uint32_t p = ms % 1000;
  bool leftPhase = p < 500;
  bool on = (p % 500) < 400 && ((p % 500) / 70) % 2 == 0;
  if (!on) return;
  if (leftPhase) c.fillRect(0, 0, c.w / 2, c.h, RGB(255, 0, 0));
  else c.fillRect(c.w / 2, 0, c.w - c.w / 2, c.h, RGB(0, 40, 255));
}

static void storm(Canvas& c, uint32_t ms) {
  // clouds on top, rain, random lightning
  float t = ms / 2500.0f;
  for (int x = 0; x < c.w; x++) {
    int ch = 2 + (sin8f(x * 0.4f + t) >> 7);
    for (int y = 0; y < ch; y++) c.set(x, y, RGB(40, 40, 50).scale(200 + (sin8f(x * 0.8f + y + t * 3) >> 3)));
  }
  if (step(ms, 45)) {
    tFade(c, 90);
    for (int i = 0; i < NP; i++) {
      if (S.life[i] > 0) {
        S.py[i] += 0.8f;
        S.px[i] -= 0.25f;
        if (S.py[i] >= c.h) S.life[i] = 0;
        else tSet(c, (int)S.px[i], (int)S.py[i], RGB(50, 80, 170));
      } else if (random(0, 100) < 5) {
        S.px[i] = random(0, c.w + 3);
        S.py[i] = 2;
        S.life[i] = 1;
      }
    }
    if (S.f1 <= 0 && random(0, 1000) < 12) {
      S.f1 = 6;
      S.f2 = random(4, c.w - 4);
    }
  }
  tBlit(c);
  if (S.f1 > 0) {
    if ((int)S.f1 % 3 != 1) {
      // bolt
      int x = (int)S.f2;
      for (int y = 1; y < c.h; y++) {
        x += ((y * 7 + (int)S.f2) % 3) - 1;
        c.set(x, y, RGB(255, 255, 200));
      }
      for (int x2 = 0; x2 < c.w; x2++) c.set(x2, 0, RGB(180, 180, 200));
    }
    if (ms - S.t0 > 50) { S.f1--; S.t0 = ms; }
  }
}

// cheap value noise
static float hash2(int x, int y) {
  uint32_t h = (uint32_t)(x * 374761393 + y * 668265263);
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float vnoise(float x, float y) {
  int xi = (int)floorf(x), yi = (int)floorf(y);
  float xf = x - xi, yf = y - yi;
  float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  float a = hash2(xi, yi), b = hash2(xi + 1, yi), c2 = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
  return a + (b - a) * u + (c2 - a) * v + (a - b - c2 + d) * u * v;
}
static void noise(Canvas& c, uint32_t ms) {
  float t = ms / 1500.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float n = vnoise(x * 0.18f + t, y * 0.25f - t * 0.5f) * 0.7f + vnoise(x * 0.4f - t, y * 0.5f) * 0.3f;
      c.set(x, y, hsv((uint8_t)(n * 300 + ms / 60), 240, (uint8_t)(60 + n * 195)));
    }
}

static void clouds(Canvas& c, uint32_t ms) {
  float t = ms / 2500.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float n = vnoise(x * 0.15f + t, y * 0.3f) * 0.65f + vnoise(x * 0.35f + t * 1.7f, y * 0.6f) * 0.35f;
      RGB sky(20, 60 + y * 6, 160);
      int w = max(0, (int)((n - 0.45f) * 500));
      c.set(x, y, RGB(min(255, sky.r + w), min(255, sky.g + w), min(255, sky.b + w)));
    }
}

static void ecg(Canvas& c, uint32_t ms) {
  // scrolling heart monitor
  const int period = 26;
  int shift = ms / 50;
  int base = c.h / 2 + 1;
  int prevY = base;
  for (int x = 0; x < c.w; x++) {
    int ph = (x + shift) % period;
    int y = base;
    if (ph == 10) y = base - 1;
    else if (ph == 12) y = base + 1;
    else if (ph == 13) y = 0;
    else if (ph == 14) y = c.h - 1;
    else if (ph == 16) y = base - 1;
    else if (ph == 19 || ph == 20) y = base - 1;
    int a = min(prevY, y), b = max(prevY, y);
    uint8_t v = x > c.w - 4 ? 255 : 90 + x * 4;
    for (int yy = a; yy <= b; yy++) c.set(x, yy, RGB(0, v, 30));
    prevY = y;
  }
}

static void xmas(Canvas& c, uint32_t ms) {
  // red/green/gold twinkle lights with falling snow
  for (int i = 0; i < 20; i++) {
    uint32_t sd = i * 2654435761u + 12345;
    int x = sd % c.w, y = (sd >> 9) % c.h;
    uint8_t v = sin8f(ms / 500.0f + i * 2.3f);
    RGB col = i % 3 == 0 ? RGB(255, 0, 0) : i % 3 == 1 ? RGB(0, 255, 0) : RGB(255, 160, 0);
    c.set(x, y, col.scale(v));
  }
  snow(c, ms);
}

static void tetris(Canvas& c, uint32_t ms) {
  // blocks (2x2 / 1x3 / 3x1 / L) fall and stack; clears when full
  uint8_t* grid = S.a;  // hue+1 or 0
  static const uint8_t SHAPES[5][3] = {{0b11, 0b11, 0}, {0b111, 0, 0}, {0b1, 0b1, 0b1}, {0b10, 0b11, 0}, {0b11, 0b10, 0}};
  static const uint8_t SW[5] = {2, 3, 1, 2, 2}, SH[5] = {2, 1, 3, 2, 2};
  auto fits = [&](int s, int x, int y) {
    for (int j = 0; j < SH[s]; j++)
      for (int i = 0; i < SW[s]; i++)
        if (SHAPES[s][j] & (1 << (SW[s] - 1 - i))) {
          int gx = x + i, gy = y + j;
          if (gx < 0 || gx >= c.w || gy >= c.h) return false;
          if (gy >= 0 && grid[gy * c.w + gx]) return false;
        }
    return true;
  };
  if (S.life[0] == 0) {  // spawn
    S.n = random(0, 5);
    S.sx[0] = random(0, c.w - SW[S.n] + 1);
    S.sy[0] = -SH[S.n];
    S.hue[0] = random(0, 256);
    S.life[0] = 1;
  }
  if (step(ms, 70)) {
    // drift towards the lowest column
    if (S.sy[0] < 0 && random(0, 100) < 40) {
      int d = random(0, 3) - 1;
      if (fits(S.n, S.sx[0] + d, S.sy[0])) S.sx[0] += d;
    }
    if (fits(S.n, S.sx[0], S.sy[0] + 1)) S.sy[0]++;
    else {
      bool over = S.sy[0] < 0;
      for (int j = 0; j < SH[S.n]; j++)
        for (int i = 0; i < SW[S.n]; i++)
          if (SHAPES[S.n][j] & (1 << (SW[S.n] - 1 - i))) {
            int gy = S.sy[0] + j;
            if (gy >= 0) grid[gy * c.w + S.sx[0] + i] = S.hue[0] | 1;
          }
      // clear full rows
      for (int y = c.h - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < c.w; x++) if (!grid[y * c.w + x]) full = false;
        if (full) {
          memmove(grid + c.w, grid, y * c.w);
          memset(grid, 0, c.w);
          y++;
        }
      }
      if (over) memset(grid, 0, c.w * c.h);
      S.life[0] = 0;
    }
  }
  for (int i = 0; i < c.w * c.h; i++)
    if (grid[i]) c.buf[i] = hsv(grid[i], 230, 200);
  if (S.life[0])
    for (int j = 0; j < SH[S.n]; j++)
      for (int i = 0; i < SW[S.n]; i++)
        if (SHAPES[S.n][j] & (1 << (SW[S.n] - 1 - i))) c.set(S.sx[0] + i, S.sy[0] + j, hsv(S.hue[0], 255, 255));
}

static void bubbles(Canvas& c, uint32_t ms) {
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) c.set(x, y, RGB(0, 10 + y * 3, 30 + y * 6));
  if (step(ms, 60)) {
    for (int i = 0; i < 10; i++) {
      if (S.life[i] > 0) {
        S.py[i] -= S.vy[i];
        if (S.py[i] < -1) S.life[i] = 0;
      } else if (random(0, 100) < 5) {
        S.px[i] = random(0, c.w);
        S.py[i] = c.h;
        S.vy[i] = frand(0.15f, 0.4f);
        S.life[i] = 1;
      }
    }
  }
  for (int i = 0; i < 10; i++)
    if (S.life[i] > 0) c.set((int)(S.px[i] + sinf(ms / 300.0f + i) * 0.8f), (int)S.py[i], RGB(120, 200, 255));
}

static void flagCz(Canvas& c, uint32_t ms) {
  float t = ms / 300.0f;
  for (int x = 0; x < c.w; x++) {
    float wave = sinf(x * 0.3f - t);
    uint8_t shade = (uint8_t)(190 + wave * 60);
    int off = (int)roundf(wave * 0.6f);
    for (int y = 0; y < c.h; y++) {
      int yy = y - off;
      RGB col;
      // blue wedge from the hoist reaching to the middle
      float half = (c.h - 1) / 2.0f;
      float wedge = (c.w * 0.4f) * (1 - fabsf(yy - half) / (half + 0.5f));
      if (x < wedge) col = RGB(17, 69, 126);
      else if (yy < c.h / 2) col = RGB(255, 255, 255);
      else col = RGB(215, 20, 26);
      c.set(x, y, col.scale(shade));
    }
  }
}

static void sinelon(Canvas& c, uint32_t ms) {
  if (step(ms, 20)) {
    tFade(c, 220);
    float t = ms / 1000.0f;
    int x = (int)roundf((sinf(t * 1.3f) + 1) / 2 * (c.w - 1));
    int y = (int)roundf((sinf(t * 2.1f) + 1) / 2 * (c.h - 1));
    tSet(c, x, y, hsv((uint8_t)(ms / 15)));
  }
  tBlit(c);
}

static void juggle(Canvas& c, uint32_t ms) {
  if (step(ms, 20)) {
    tFade(c, 200);
    float t = ms / 1000.0f;
    for (int i = 0; i < 6; i++) {
      int x = (int)roundf((sinf(t * (0.7f + i * 0.35f)) + 1) / 2 * (c.w - 1));
      int y = (int)roundf((cosf(t * (1.1f + i * 0.27f)) + 1) / 2 * (c.h - 1));
      tAdd(c, x, y, hsv(i * 42, 230, 200));
    }
  }
  tBlit(c);
}

static void sunrise(Canvas& c, uint32_t ms) {
  // 20 s sunrise loop: dark blue sky, warm horizon, rising sun
  float f = (ms % 20000) / 20000.0f;
  float sunY = c.h + 2 - f * (c.h + 2);
  float sx = (c.w - 1) / 2.0f;
  for (int y = 0; y < c.h; y++)
    for (int x = 0; x < c.w; x++) {
      float h = (float)y / (c.h - 1);  // 0 top .. 1 bottom
      float warm = f * h;
      RGB col((uint8_t)min(255.0f, 20 + warm * 255), (uint8_t)min(255.0f, 10 + warm * 110 + f * 40), (uint8_t)(60 * (1 - f) + 40 * f * (1 - h)));
      float dx = (x - sx) * 0.5f, dy = y - sunY;
      float d = sqrtf(dx * dx + dy * dy);
      if (d < 2.5f) col = RGB(255, 220, 80);
      else if (d < 4) col = blend(col, RGB(255, 150, 0), (uint8_t)(255 * (4 - d) / 1.5f));
      c.set(x, y, col);
    }
}

static void spiral(Canvas& c, uint32_t ms) {
  // growing / shrinking rectangular spiral path
  int total = c.w * c.h;
  int n = (ms / 12) % (total * 2);
  bool erase = n >= total;
  n %= total;
  int x0 = 0, y0 = 0, x1 = c.w - 1, y1 = c.h - 1, k = 0;
  uint8_t hue = ms / 40;
  while (x0 <= x1 && y0 <= y1) {
    for (int x = x0; x <= x1; x++, k++) c.set(x, y0, (k < n) != erase ? hsv(hue + k) : RGB());
    for (int y = y0 + 1; y <= y1; y++, k++) c.set(x1, y, (k < n) != erase ? hsv(hue + k) : RGB());
    if (y0 != y1) for (int x = x1 - 1; x >= x0; x--, k++) c.set(x, y1, (k < n) != erase ? hsv(hue + k) : RGB());
    if (x0 != x1) for (int y = y1 - 1; y > y0; y--, k++) c.set(x0, y, (k < n) != erase ? hsv(hue + k) : RGB());
    x0++; y0++; x1--; y1--;
  }
}

static void pixelRain(Canvas& c, uint32_t ms) {
  // colourful digital rain falling in columns with long tails
  if (step(ms, 60)) {
    tFade(c, 170);
    for (int x = 0; x < c.w && x < 128; x++) {
      if (S.drops[x] > 0) {
        tSet(c, x, S.drops[x] - 1, hsv((uint8_t)(x * 8 + ms / 30), 200, 255));
        if (++S.drops[x] > c.h) S.drops[x] = 0;
      } else if (random(0, 100) < 5) S.drops[x] = 1;
    }
  }
  tBlit(c);
}

// ------------------------------------------------------------------ table
struct Fx {
  const char* name;
  void (*fn)(Canvas&, uint32_t);
};
static const Fx FX[] = {
  {"rainbow", rainbow},       {"rainbow_diag", rainbowDiag}, {"plasma", plasma},       {"plasma_cloud", plasmaCloud},
  {"fire", fire},             {"matrix", matrixFx},          {"pixel_rain", pixelRain}, {"snow", snow},
  {"sparkle", sparkle},       {"waves", waves},              {"aurora", aurora},       {"stars", stars},
  {"twinkle", twinkle},       {"fireworks", fireworks},      {"ripple", ripple},       {"snake", snake},
  {"pingpong", pingpong},     {"brickbreaker", brickbreaker},{"tetris", tetris},       {"pacman", pacman},
  {"invaders", invaders},     {"radar", radar},              {"checkerboard", checkerboard}, {"theater", theater},
  {"colorwaves", colorwaves}, {"swirl_in", swirlIn},         {"swirl_out", swirlOut},  {"pacifica", pacifica},
  {"moving_line", movingLine},{"fade", fade},                {"life", life},           {"metaballs", metaballs},
  {"lava", lava},             {"bounce", bounce},            {"rain", rain},           {"storm", storm},
  {"clouds", clouds},         {"sunrise", sunrise},          {"bubbles", bubbles},     {"hearts", hearts},
  {"heartbeat", heartbeat},   {"dna", dna},                  {"equalizer", equalizer}, {"starfield", starfield},
  {"confetti", confetti},     {"comet", comet},              {"sinelon", sinelon},     {"juggle", juggle},
  {"scanner", scanner},       {"hypno", hypno},              {"spiral", spiral},       {"noise", noise},
  {"police", police},         {"ecg", ecg},                  {"xmas", xmas},           {"flag_cz", flagCz},
};

void list(JsonArray a) {
  for (auto& f : FX) a.add(f.name);
  Eyes::list(a);
}

bool render(Canvas& c, const String& name, uint32_t ms) {
  if (name.startsWith("eyes")) return Eyes::render(c, name, ms);
  // AWTRIX effect names are CamelCase ("SwirlIn", "TwinklingStars") -> accept them too
  String n = name;
  if (n.length() && isupper((unsigned char)n[0])) {
    String o;
    for (unsigned i = 0; i < n.length(); i++) {
      char ch = n[i];
      if (isupper((unsigned char)ch)) {
        if (i) o += '_';
        o += (char)tolower((unsigned char)ch);
      } else o += ch;
    }
    n = o;
    if (n == "twinkling_stars") n = "twinkle";
    else if (n == "theater_chase") n = "theater";
    else if (n == "brick_breaker") n = "brickbreaker";
    else if (n == "ping_pong") n = "pingpong";
    else if (n == "color_waves") n = "colorwaves";
    else if (n == "looking_eyes") return Eyes::render(c, "eyes", ms);
  }
  for (auto& f : FX) {
    if (n != f.name) continue;
    if (curName != f.name || ms - lastMs > 2000) {  // effect changed / restarted -> fresh state
      memset(&S, 0, sizeof(S));
      curName = f.name;
    }
    lastMs = ms;
    f.fn(c, ms);
    return true;
  }
  return false;
}

}  // namespace Effects
