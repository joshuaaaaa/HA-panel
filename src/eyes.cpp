#include "eyes.h"
#include <math.h>

namespace Eyes {

// ------------------------------------------------------------------ styles
enum Shape : uint8_t { SH_ELLIPSE, SH_RECT };
enum Pupil : uint8_t { PU_ROUND, PU_SLIT, PU_HEART, PU_SPIRAL, PU_SQUARE, PU_STAR };
enum Anim : uint8_t { AN_LOOK, AN_SLEEPY, AN_WINK, AN_CRAZY, AN_MOOD, AN_SCAN, AN_SUSPICIOUS, AN_STARE };

struct Palette {
  uint32_t scleraIn, scleraOut, irisIn, irisOut, pupil, glint;
};

struct Expr {
  float open;    // 0 closed .. 1 open
  float tilt;    // + angry (inner corners down), - sad
  float happy;   // 0..1 lower lid arc (^ ^)
  float irisR;   // iris radius (px)
  float pupilR;  // pupil radius (px)
  float gazeY;   // preferred vertical gaze (-1 up .. 1 down)
};

struct Style {
  const char* name;
  const char* label;
  Palette pal;
  Shape shape;
  Pupil pupil;
  Expr expr;
  Anim anim;
  bool cyclops;
};

static const Palette NATURAL = {0xE8E8EC, 0x6A6A78, 0x50A8FF, 0x0A3090, 0x000000, 0xFFFFFF};
static const Palette BROWN = {0xE8E8EC, 0x6A6A78, 0xC08040, 0x402008, 0x000000, 0xFFFFFF};
static const Palette GREEN = {0xE8E8EC, 0x6A6A78, 0x70E060, 0x0A5010, 0x000000, 0xFFFFFF};
static const Palette EVIL = {0xFF60C8, 0x6000A0, 0xFFF040, 0xFF8000, 0x500000, 0xFFFFC0};
static const Palette DEMON = {0x300000, 0x100000, 0xFF2000, 0x800000, 0xFFE000, 0xFFFFFF};
static const Palette CAT = {0xB8E830, 0x3A7000, 0xC8F040, 0x508000, 0x000000, 0xFFFFFF};
static const Palette ROBOT = {0x002838, 0x001018, 0x00FFFF, 0x0060FF, 0x001020, 0xFFFFFF};
static const Palette LOVE = {0xFFE8F0, 0x805060, 0xFF2050, 0xA00020, 0xFF0030, 0xFFFFFF};
static const Palette DIZZY = {0xF0F0FF, 0x7070A0, 0xB040FF, 0x300070, 0x000000, 0xFFFFFF};
static const Palette ICE = {0xE0F8FF, 0x4080A0, 0xA0F0FF, 0x0080C0, 0x001030, 0xFFFFFF};
static const Palette GOLD = {0x201000, 0x080400, 0xFFD040, 0xC06000, 0x100000, 0xFFFFFF};

//                      open  tilt   happy irisR pupilR gazeY
static const Expr E_NEUTRAL = {1.00f, 0.00f, 0.0f, 2.6f, 1.2f, 0.0f};
static const Expr E_ANGRY = {0.78f, 0.45f, 0.0f, 2.5f, 1.2f, 0.1f};
static const Expr E_SAD = {0.80f, -0.35f, 0.0f, 2.6f, 1.3f, 0.6f};
static const Expr E_HAPPY = {1.00f, 0.00f, 0.9f, 2.6f, 1.2f, -0.2f};
static const Expr E_SLEEPY = {0.50f, 0.05f, 0.0f, 2.6f, 1.2f, 0.7f};
static const Expr E_SURPRISED = {1.00f, -0.10f, 0.0f, 1.8f, 0.8f, 0.0f};
static const Expr E_SUSPICIOUS = {0.45f, 0.12f, 0.0f, 2.4f, 1.2f, 0.0f};
static const Expr E_LOVE = {1.00f, 0.00f, 0.3f, 2.8f, 1.2f, 0.0f};
static const Expr E_EVIL = {0.88f, 0.35f, 0.0f, 3.0f, 1.0f, 0.1f};
static const Expr E_BIG = {1.00f, 0.00f, 0.0f, 3.2f, 1.6f, 0.0f};

static const Style STYLES[] = {
  {"eyes", "Oči – rozhlížení", NATURAL, SH_ELLIPSE, PU_ROUND, E_NEUTRAL, AN_LOOK, false},
  {"eyes_mood", "Oči – střídání nálad", NATURAL, SH_ELLIPSE, PU_ROUND, E_NEUTRAL, AN_MOOD, false},
  {"eyes_evil", "Oči – zlé (růžové)", EVIL, SH_ELLIPSE, PU_ROUND, E_EVIL, AN_LOOK, false},
  {"eyes_demon", "Oči – démon", DEMON, SH_ELLIPSE, PU_SLIT, E_ANGRY, AN_STARE, false},
  {"eyes_angry", "Oči – naštvané", BROWN, SH_ELLIPSE, PU_ROUND, E_ANGRY, AN_LOOK, false},
  {"eyes_happy", "Oči – veselé", NATURAL, SH_ELLIPSE, PU_ROUND, E_HAPPY, AN_LOOK, false},
  {"eyes_sad", "Oči – smutné", NATURAL, SH_ELLIPSE, PU_ROUND, E_SAD, AN_LOOK, false},
  {"eyes_sleepy", "Oči – ospalé", BROWN, SH_ELLIPSE, PU_ROUND, E_SLEEPY, AN_SLEEPY, false},
  {"eyes_surprised", "Oči – překvapené", NATURAL, SH_ELLIPSE, PU_ROUND, E_SURPRISED, AN_LOOK, false},
  {"eyes_suspicious", "Oči – podezíravé", GREEN, SH_ELLIPSE, PU_ROUND, E_SUSPICIOUS, AN_SUSPICIOUS, false},
  {"eyes_love", "Oči – zamilované", LOVE, SH_ELLIPSE, PU_HEART, E_LOVE, AN_LOOK, false},
  {"eyes_wink", "Oči – mrkající", NATURAL, SH_ELLIPSE, PU_ROUND, E_NEUTRAL, AN_WINK, false},
  {"eyes_dizzy", "Oči – omámené (spirála)", DIZZY, SH_ELLIPSE, PU_SPIRAL, E_NEUTRAL, AN_STARE, false},
  {"eyes_crazy", "Oči – šilhající", NATURAL, SH_ELLIPSE, PU_ROUND, E_BIG, AN_CRAZY, false},
  {"eyes_cat", "Oči – kočičí", CAT, SH_ELLIPSE, PU_SLIT, E_NEUTRAL, AN_LOOK, false},
  {"eyes_robot", "Oči – robot", ROBOT, SH_RECT, PU_SQUARE, E_NEUTRAL, AN_LOOK, false},
  {"eyes_scan", "Oči – skenující", ROBOT, SH_RECT, PU_SQUARE, E_NEUTRAL, AN_SCAN, false},
  {"eyes_ice", "Oči – ledové", ICE, SH_ELLIPSE, PU_ROUND, E_NEUTRAL, AN_STARE, false},
  {"eyes_star", "Oči – hvězdičky", GOLD, SH_ELLIPSE, PU_STAR, E_BIG, AN_LOOK, false},
  {"eyes_cyclops", "Oko – kyklop", EVIL, SH_ELLIPSE, PU_ROUND, E_NEUTRAL, AN_LOOK, true},
  {"eyes_cyclops_robot", "Oko – robot kyklop", ROBOT, SH_RECT, PU_SQUARE, E_NEUTRAL, AN_SCAN, true},
};
static const int NSTYLES = sizeof(STYLES) / sizeof(STYLES[0]);

static const Expr* const MOODS[] = {&E_NEUTRAL, &E_HAPPY, &E_ANGRY, &E_SAD, &E_SURPRISED, &E_SUSPICIOUS, &E_SLEEPY, &E_LOVE};
static const int NMOODS = sizeof(MOODS) / sizeof(MOODS[0]);

void list(JsonArray a) {
  for (auto& s : STYLES) a.add(s.name);
}

// ------------------------------------------------------------------ animation state
struct State {
  const Style* style = nullptr;
  float gx = 0, gy = 0;          // current gaze
  float tx = 0, ty = 0;          // target gaze
  float gx2 = 0, gy2 = 0, tx2 = 0, ty2 = 0;  // second eye (crazy)
  uint32_t nextGaze = 0, nextBlink = 0, blinkStart = 0, lastMs = 0;
  bool blinking = false;
  Expr expr = E_NEUTRAL;
  int mood = 0;
  uint32_t moodStart = 0;
};
static State st;

static float frand(float a, float b) { return a + (b - a) * (esp_random() % 10000) / 10000.0f; }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static RGB mix(uint32_t a, uint32_t b, float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return blend(RGB(a), RGB(b), (uint8_t)(t * 255));
}
static Expr lerpExpr(const Expr& a, const Expr& b, float t) {
  return {lerp(a.open, b.open, t), lerp(a.tilt, b.tilt, t), lerp(a.happy, b.happy, t),
          lerp(a.irisR, b.irisR, t), lerp(a.pupilR, b.pupilR, t), lerp(a.gazeY, b.gazeY, t)};
}

// heart implicit function, true inside
static bool inHeart(float x, float y) {
  y = -y;
  float a = x * x + y * y - 1;
  return a * a * a - x * x * y * y * y <= 0;
}

static bool inStar(float x, float y, float r) {
  float ang = atan2f(y, x);
  float d = sqrtf(x * x + y * y);
  float k = 0.55f + 0.45f * cosf(5 * ang);
  return d <= r * k;
}

// one eye with 3x3 supersampling
static void drawEye(Canvas& c, float cx, float cy, float rx, float ry, float side, const Expr& e, float gx, float gy,
                    float open, const Style& s, uint32_t ms) {
  const Palette& p = s.pal;
  int x0 = max(0, (int)floorf(cx - rx - 1)), x1 = min(c.w - 1, (int)ceilf(cx + rx + 1));
  float irisR = s.pupil == PU_SLIT ? ry * 1.8f : e.irisR;
  float icx = gx, icy = gy;  // iris center relative to eye center
  bool closedLine = open < 0.12f;
  float lineY = cy + ry * 0.35f;
  for (int y = 0; y < c.h; y++) {
    for (int x = x0; x <= x1; x++) {
      float r = 0, g = 0, b = 0;
      for (int sy = -1; sy <= 1; sy++) {
        for (int sx = -1; sx <= 1; sx++) {
          float px = x + sx / 3.0f, py = y + sy / 3.0f;
          float dx = px - cx, dy = py - cy;
          float nx = dx / rx, ny = dy / ry;
          bool inside;
          if (s.shape == SH_RECT) {
            float ax = fabsf(dx) - (rx - 1.2f), ay = fabsf(dy) - (ry - 1.2f);
            inside = fabsf(dx) <= rx && fabsf(dy) <= ry && !(ax > 0 && ay > 0 && ax * ax + ay * ay > 1.44f);
          } else {
            inside = nx * nx + ny * ny <= 1.0f;
          }
          if (!inside) continue;
          RGB col;
          if (closedLine) {
            // closed eye: thin lid line
            if (fabsf(py - (lineY + e.tilt * dx * side * 0.5f)) > 0.45f) continue;
            col = RGB(p.scleraOut);
          } else {
            float top = cy - ry + 2 * ry * (1 - open) * 0.9f + e.tilt * dx * side;
            float bot = cy + ry - 2 * ry * (1 - open) * 0.1f;
            if (e.happy > 0) bot = min(bot, cy + ry - e.happy * ry * 1.35f * (1 - nx * nx));
            if (py < top || py > bot) continue;
            float ix = dx - icx, iy = dy - icy;
            float d = sqrtf(ix * ix + iy * iy);
            // sclera
            float t = sqrtf(nx * nx + ny * ny);
            col = mix(p.scleraIn, p.scleraOut, t * t);
            bool inIris = s.pupil == PU_SQUARE ? (fabsf(ix) <= irisR * 0.85f && fabsf(iy) <= irisR * 0.85f) : d <= irisR;
            if (s.pupil == PU_HEART) {
              float hs = irisR * (0.78f + 0.08f * sinf(ms / 180.0f));
              if (inHeart(ix / hs, iy / hs)) col = mix(p.irisIn, p.irisOut, d / (irisR * 1.2f));
            } else if (s.pupil == PU_STAR) {
              float rot = ms / 900.0f;
              float rx2 = ix * cosf(rot) - iy * sinf(rot), ry2 = ix * sinf(rot) + iy * cosf(rot);
              if (inStar(rx2, ry2, irisR * 1.15f)) col = mix(p.irisIn, p.irisOut, d / irisR);
            } else if (s.pupil == PU_SPIRAL) {
              if (d <= irisR * 1.25f) {
                float ang = atan2f(iy, ix);
                float v = sinf(ang + d * 2.4f - ms / 140.0f);
                col = v > 0 ? mix(p.irisIn, p.irisOut, d / irisR) : RGB(p.scleraIn);
              }
            } else if (inIris) {
              col = mix(p.irisIn, p.irisOut, d / irisR);
              bool inPupil;
              if (s.pupil == PU_SLIT) inPupil = fabsf(ix) <= e.pupilR * 0.45f && fabsf(iy) <= ry * 1.1f;
              else if (s.pupil == PU_SQUARE) inPupil = fabsf(ix) <= e.pupilR * 0.8f && fabsf(iy) <= e.pupilR * 0.8f;
              else inPupil = d <= e.pupilR;
              if (inPupil) col = RGB(p.pupil);
            }
            // glint (reflection)
            float gdx = ix + irisR * 0.42f, gdy = iy + irisR * 0.42f;
            if (s.pupil != PU_SPIRAL && gdx * gdx + gdy * gdy < 0.32f) col = RGB(p.glint);
          }
          r += col.r;
          g += col.g;
          b += col.b;
        }
      }
      if (r + g + b > 0) {
        RGB o((uint8_t)(r / 9), (uint8_t)(g / 9), (uint8_t)(b / 9));
        RGB cur = c.get(x, y);
        c.set(x, y, RGB(max(cur.r, o.r), max(cur.g, o.g), max(cur.b, o.b)));
      }
    }
  }
}

static void pickGaze(float maxX, float maxY, float prefY, float& tx, float& ty) {
  // favour looking straight, left/right and a few diagonals like a real eye
  int r = esp_random() % 10;
  if (r < 3) tx = 0;
  else tx = frand(-maxX, maxX);
  ty = constrain(prefY * maxY + frand(-maxY, maxY) * 0.7f, -maxY, maxY);
}

bool render(Canvas& c, const String& name, uint32_t ms) {
  const Style* s = nullptr;
  for (auto& x : STYLES) if (name == x.name) s = &x;
  if (!s) return false;
  if (st.style != s || ms - st.lastMs > 2000) {  // (re)start
    st = State();
    st.style = s;
    st.expr = s->expr;
    st.nextGaze = ms + 600;
    st.nextBlink = ms + 1500;
    st.moodStart = ms;
  }
  float dt = min(0.1f, (ms - st.lastMs) / 1000.0f);
  st.lastMs = ms;

  const bool cyc = s->cyclops;
  const float W = c.w, H = c.h;
  float cy = (H - 1) / 2.0f;
  float ry = H / 2.0f - 0.15f;
  float rx = cyc ? W * 0.30f : W / 4.0f - 1.8f;
  float cxL = cyc ? (W - 1) / 2.0f : W / 4.0f - 0.5f;
  float cxR = 3 * W / 4.0f - 0.5f;

  // expression (mood cycling)
  Expr e = st.expr;
  if (s->anim == AN_MOOD) {
    uint32_t per = 6000;
    uint32_t el = ms - st.moodStart;
    int m = (el / per) % NMOODS;
    float f = (el % per) / (float)per;
    const Expr& a = *MOODS[m];
    const Expr& b = *MOODS[(m + 1) % NMOODS];
    e = f > 0.85f ? lerpExpr(a, b, (f - 0.85f) / 0.15f) : a;
  }

  float maxX = rx - e.irisR - 0.3f;
  float maxY = max(0.4f, ry - e.irisR * 0.8f);
  if (cyc) maxX = rx - e.irisR - 1;

  // gaze targets
  if ((int32_t)(ms - st.nextGaze) >= 0) {
    switch (s->anim) {
      case AN_STARE:
        st.tx = frand(-0.6f, 0.6f);
        st.ty = frand(-0.3f, 0.3f);
        st.nextGaze = ms + (uint32_t)frand(2500, 6000);
        break;
      case AN_SUSPICIOUS:
        st.tx = (st.tx > 0 ? -1 : 1) * maxX;
        st.ty = 0.2f;
        st.nextGaze = ms + (uint32_t)frand(1800, 3500);
        break;
      case AN_SLEEPY:
        st.tx = frand(-maxX, maxX) * 0.5f;
        st.ty = maxY * 0.8f;
        st.nextGaze = ms + (uint32_t)frand(3000, 7000);
        break;
      case AN_CRAZY:
        pickGaze(maxX, maxY, 0, st.tx, st.ty);
        pickGaze(maxX, maxY, 0, st.tx2, st.ty2);
        st.nextGaze = ms + (uint32_t)frand(400, 1500);
        break;
      default:
        pickGaze(maxX, maxY, e.gazeY, st.tx, st.ty);
        st.nextGaze = ms + (uint32_t)frand(700, 3200);
        break;
    }
  }
  if (s->anim == AN_SCAN) {
    st.tx = maxX * sinf(ms / 700.0f);
    st.ty = 0;
  }
  // smooth saccade
  float k = min(1.0f, dt * (s->anim == AN_SCAN ? 30 : 14));
  st.gx += (st.tx - st.gx) * k;
  st.gy += (st.ty - st.gy) * k;
  st.gx2 += (st.tx2 - st.gx2) * k;
  st.gy2 += (st.ty2 - st.gy2) * k;

  // blinking
  float open = e.open;
  if (!st.blinking && (int32_t)(ms - st.nextBlink) >= 0) {
    st.blinking = true;
    st.blinkStart = ms;
  }
  float blinkLen = s->anim == AN_SLEEPY ? 900 : 220;
  bool leftClosedOnly = false;
  if (st.blinking) {
    float f = (ms - st.blinkStart) / blinkLen;
    if (f >= 1) {
      st.blinking = false;
      bool dbl = (esp_random() % 5) == 0;
      st.nextBlink = ms + (dbl ? 150 : (uint32_t)frand(s->anim == AN_SLEEPY ? 1500 : 2200, 6000));
    } else {
      float lid = f < 0.5f ? f * 2 : (1 - f) * 2;  // 0 -> 1 -> 0
      if (s->anim == AN_WINK) leftClosedOnly = true;
      else open = open * (1 - lid);
    }
  }
  if (s->anim == AN_SLEEPY) open *= 0.75f + 0.25f * sinf(ms / 1300.0f);

  if (cyc) {
    drawEye(c, cxL, cy, rx, ry, 1, e, st.gx, st.gy, open, *s, ms);
    return true;
  }
  float openL = open, openR = open;
  if (leftClosedOnly) {
    float f = (ms - st.blinkStart) / 450.0f;
    openR = e.open * (1 - (f < 0.5f ? f * 2 : (1 - f) * 2));
  }
  drawEye(c, cxL, cy, rx, ry, 1, e, st.gx, st.gy, openL, *s, ms);
  if (s->anim == AN_CRAZY) drawEye(c, cxR, cy, rx, ry, -1, e, st.gx2, st.gy2, openR, *s, ms);
  else drawEye(c, cxR, cy, rx, ry, -1, e, st.gx, st.gy, openR, *s, ms);

  // tear for the sad eyes
  if (s->pal.irisIn == NATURAL.irisIn && e.tilt < -0.2f) {
    uint32_t ph = ms % 3000;
    if (ph < 1500) {
      int ty = (int)(cy + ry * 0.2f + ph / 1500.0f * (H - cy));
      c.set((int)(cxL + rx * 0.5f), ty, RGB(60, 140, 255));
    }
  }
  return true;
}

}  // namespace Eyes
