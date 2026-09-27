#include "apps.h"
#include <time.h>
#include "config.h"
#include "display.h"
#include "effects.h"
#include "ha_client.h"
#include "icons.h"

namespace Apps {

volatile uint8_t ldrBrightness = 40;

// ================================================================ state
struct PageRt {
  String text;
  bool textSet = false;
  bool textError = false;
  int32_t color = -1;
  String icon;
  bool visible = true;
  int progress = -1;
};

struct Notif {
  String text, icon, effect;
  int32_t color = -1, progressColor = -1;
  uint32_t durationMs = 5000;
  uint8_t repeat = 1;
  bool rainbow = false, hold = false, wakeup = false;
  int progress = -1;
};

struct CustomPage {
  bool used = false;
  String name, text, icon, effect;
  int32_t color = -1, progressColor = -1;
  int progress = -1;
  uint16_t duration = 8;
  uint32_t expires = 0;  // millis, 0 = never
  bool rainbow = false;
};

struct Indicator {
  int32_t tplColor = -1;
  int32_t mqttColor = -1;
  uint16_t blink = 0;
  uint32_t expires = 0;
};

enum SlotKind : uint8_t { SK_NONE, SK_PAGE, SK_CUSTOM, SK_NOTIF, SK_FALLBACK };

struct Slot {
  SlotKind kind = SK_NONE;
  int idx = 0;
  uint32_t start = 0;
  bool durReached = false;
  int passesAtDur = 0;
};

struct RenderInfo {
  bool scrolling = false;
  int passes = 0;
};

static const int MAX_NOTIF = 10;
static const int MAX_CUSTOM = 8;

static PageRt rt[MAX_PAGES];
static Notif queue[MAX_NOTIF];
static int qLen = 0;
static Notif curNotif;
static CustomPage custom[MAX_CUSTOM];
static Indicator ind[NUM_INDICATORS];

static Slot cur, resumeSlot;
static RenderInfo curInfo;
static bool transActive = false;
static uint32_t transStart = 0;
static uint8_t transType = 0;
static int8_t transDir = 1;

static Canvas cvA, cvB, cvOut, cvLast;
static uint8_t* preview = nullptr;
static size_t previewLen = 0;
static uint32_t frameCounter = 0;

static uint32_t testUntil = 0;
static int otaPct = -1;
static bool wakeActive = false;
static bool notifDismissed = false;

// ================================================================ helpers
static bool timeValid() { return time(nullptr) > 1600000000; }

static int parseHM(const String& s) {
  int c = s.indexOf(':');
  if (c < 0) return -1;
  return s.substring(0, c).toInt() * 60 + s.substring(c + 1).toInt();
}

bool isNight() {
  if (!cfg.nightEnabled || !timeValid()) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min;
  int s = parseHM(cfg.nightStart), e = parseHM(cfg.nightEnd);
  if (s < 0 || e < 0 || s == e) return false;
  return s < e ? (m >= s && m < e) : (m >= s || m < e);
}

uint8_t targetBrightness() {
  if (otaPct >= 0 || testUntil > millis()) return max<uint8_t>(cfg.brightness, 20);
  if (!cfg.power) return wakeActive ? max<uint8_t>(cfg.brightness, 10) : 0;
  if (isNight()) return cfg.nightBright;
  if (cfg.autoBright && cfg.ldrPin >= 0) return ldrBrightness;
  return cfg.brightness;
}

static int32_t variantColor(JsonVariantConst v, int32_t def) {
  if (v.isNull()) return def;
  if (v.is<JsonArrayConst>()) {
    JsonArrayConst a = v.as<JsonArrayConst>();
    if (a.size() >= 3) return (constrain(a[0].as<int>(), 0, 255) << 16) | (constrain(a[1].as<int>(), 0, 255) << 8) |
                              constrain(a[2].as<int>(), 0, 255);
    return def;
  }
  if (v.is<int>()) return v.as<int>() & 0xFFFFFF;
  return parseColor(v.as<String>(), def);
}

static bool pageShown(int i) {
  if (i < 0 || i >= cfg.pageCount) return false;
  const PageCfg& p = cfg.pages[i];
  if (!p.enabled) return false;
  if (!p.visibleTpl.isEmpty() && !rt[i].visible) return false;
  if (isNight() && cfg.nightClockOnly && p.type != PT_CLOCK) return false;
  return true;
}

static bool customShown(int i) {
  if (i < 0 || i >= MAX_CUSTOM || !custom[i].used) return false;
  if (custom[i].expires && (int32_t)(millis() - custom[i].expires) >= 0) {
    custom[i].used = false;
    return false;
  }
  if (isNight() && cfg.nightClockOnly) return false;
  return true;
}

// rotation order: pages[0..n) then custom[0..MAX_CUSTOM)
static int rotLen() { return cfg.pageCount + MAX_CUSTOM; }
static bool rotValid(int pos) { return pos < cfg.pageCount ? pageShown(pos) : customShown(pos - cfg.pageCount); }
static int slotPos(const Slot& s) {
  if (s.kind == SK_PAGE) return s.idx;
  if (s.kind == SK_CUSTOM) return cfg.pageCount + s.idx;
  return -1;
}

static Slot slotFromPos(int pos) {
  Slot s;
  if (pos < 0) { s.kind = SK_FALLBACK; return s; }
  if (pos < cfg.pageCount) { s.kind = SK_PAGE; s.idx = pos; }
  else { s.kind = SK_CUSTOM; s.idx = pos - cfg.pageCount; }
  return s;
}

static int findNext(int from, int dir) {
  int n = rotLen();
  for (int k = 1; k <= n; k++) {
    int pos = ((from + dir * k) % n + n) % n;
    if (rotValid(pos)) return pos;
  }
  return -1;
}

static void switchTo(const Slot& s, int8_t dir = 1) {
  if (cvLast.buf) cvB.copyFrom(cvLast);
  transActive = cfg.transition != 0 && cur.kind != SK_NONE;
  transStart = millis();
  transType = cfg.transition;
  transDir = dir;
  cur = s;
  cur.start = millis();
  cur.durReached = false;
  cur.passesAtDur = 0;
  curInfo = RenderInfo();
}

static bool slotValid(const Slot& s) {
  switch (s.kind) {
    case SK_PAGE: return pageShown(s.idx);
    case SK_CUSTOM: return customShown(s.idx);
    case SK_NOTIF: return true;
    case SK_FALLBACK: return findNext(-1, 1) < 0;  // only while nothing else can be shown
    default: return false;
  }
}

// ================================================================ content
struct Content {
  String icon, text, effect;
  RGB color;
  bool rainbow = false;
  int progress = -1;
  RGB progressColor = RGB(0, 160, 255);
  bool big = false;
  uint8_t colonMask = 0xFF;
  bool weekday = false;
  bool calendar = false;
  int calendarDay = 0;
};

static RGB colorOr(int32_t c, int32_t def) { return RGB((uint32_t)(c >= 0 ? c : def)); }

static void clockText(Content& ct, bool big) {
  if (!timeValid()) {
    ct.text = big ? "--:--" : "--:--";
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int h = t.tm_hour;
  if (!cfg.h24) { h %= 12; if (h == 0) h = 12; }
  bool colonOn = !cfg.blinkColon || (t.tm_sec % 2 == 0);
  char buf[16];
  bool seconds = cfg.showSeconds && !big;
  if (big) {
    snprintf(buf, sizeof(buf), cfg.h24 ? "%02d:%02d" : "%d:%02d", h, t.tm_min);
    ct.colonMask = colonOn ? 0xFF : 0x00;
  } else {
    const char* colon = colonOn ? ":" : "\x01";
    if (seconds) snprintf(buf, sizeof(buf), cfg.h24 ? "%02d%s%02d%s%02d" : "%d%s%02d%s%02d", h, colon, t.tm_min, colon, t.tm_sec);
    else snprintf(buf, sizeof(buf), cfg.h24 ? "%02d%s%02d" : "%d%s%02d", h, colon, t.tm_min);
  }
  ct.text = buf;
}

static void dateText(Content& ct) {
  if (!timeValid()) { ct.text = "--.--."; return; }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[16];
  switch (cfg.dateFormat) {
    case 1: snprintf(buf, sizeof(buf), "%02d.%02d.%02d", t.tm_mday, t.tm_mon + 1, t.tm_year % 100); break;
    case 2: snprintf(buf, sizeof(buf), "%02d/%02d", t.tm_mon + 1, t.tm_mday); break;
    case 3: snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday); break;
    case 4: snprintf(buf, sizeof(buf), "%d.%d.", t.tm_mday, t.tm_mon + 1); break;
    default: snprintf(buf, sizeof(buf), "%02d.%02d.", t.tm_mday, t.tm_mon + 1); break;
  }
  ct.text = buf;
  ct.calendarDay = t.tm_mday;
}

static void buildContent(const Slot& s, Content& ct) {
  int32_t defColor = cfg.textColor;
  switch (s.kind) {
    case SK_PAGE: {
      const PageCfg& p = cfg.pages[s.idx];
      const PageRt& r = rt[s.idx];
      ct.icon = r.icon.length() ? r.icon : p.icon;
      ct.color = colorOr(r.color >= 0 ? r.color : p.color, defColor);
      ct.rainbow = p.rainbow;
      ct.progress = r.progress;
      if (p.progressColor >= 0) ct.progressColor = RGB((uint32_t)p.progressColor);
      ct.effect = p.effect;
      switch (p.type) {
        case PT_CLOCK:
          ct.big = p.style == 1;
          clockText(ct, ct.big);
          ct.weekday = cfg.weekdayBar;
          ct.effect = p.effect;
          break;
        case PT_DATE:
          dateText(ct);
          ct.weekday = cfg.weekdayBar;
          if (ct.icon.isEmpty()) ct.calendar = timeValid();
          break;
        case PT_ENTITY:
        case PT_TEMPLATE:
          if (!cfg.haEnabled) ct.text = "HA vyp";
          else if (r.textError) ct.text = "chyba";
          else if (!r.textSet) ct.text = HA::connected() ? "..." : "HA?";
          else ct.text = r.text;
          break;
        case PT_TEXT:
          ct.text = p.text;
          break;
        case PT_EFFECT:
          ct.text = p.text;
          if (ct.effect.isEmpty()) ct.effect = "rainbow";
          break;
      }
      break;
    }
    case SK_CUSTOM: {
      const CustomPage& c = custom[s.idx];
      ct.icon = c.icon;
      ct.text = c.text;
      ct.effect = c.effect;
      ct.color = colorOr(c.color, defColor);
      ct.rainbow = c.rainbow;
      ct.progress = c.progress;
      if (c.progressColor >= 0) ct.progressColor = RGB((uint32_t)c.progressColor);
      break;
    }
    case SK_NOTIF:
      ct.icon = curNotif.icon;
      ct.text = curNotif.text;
      ct.effect = curNotif.effect;
      ct.color = colorOr(curNotif.color, defColor);
      ct.rainbow = curNotif.rainbow;
      ct.progress = curNotif.progress;
      if (curNotif.progressColor >= 0) ct.progressColor = RGB((uint32_t)curNotif.progressColor);
      break;
    case SK_FALLBACK:
    default:
      ct.big = true;
      clockText(ct, true);
      ct.color = RGB((uint32_t)defColor);
      ct.weekday = cfg.weekdayBar;
      break;
  }
  if (cfg.uppercase && !ct.big) ct.text = toUpperUtf8(ct.text);
  if (isNight() && cfg.nightColor >= 0) {
    ct.color = RGB((uint32_t)cfg.nightColor);
    ct.rainbow = false;
  }
}

static void drawCalendar(Canvas& c, int x, int y, int day, uint32_t ms) {
  (void)ms;
  c.fillRect(x, y, 8, 2, RGB(220, 0, 0));
  c.fillRect(x, y + 2, 8, 6, RGB(230, 230, 230));
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", day);
  String s(buf);
  int w = textWidth(s);
  drawText(c, x + (8 - w + 1) / 2, y + 2, s, RGB(0, 0, 0), {x, x + 7});
}

static void drawWeekday(Canvas& c, int x0, int x1) {
  if (!timeValid()) return;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int today = cfg.mondayFirst ? (t.tm_wday + 6) % 7 : t.tm_wday;
  int area = x1 - x0 + 1;
  int seg = max(1, (area - 6) / 7);
  int total = seg * 7 + 6;
  int start = x0 + (area - total) / 2;
  int y = c.h - 1;
  for (int d = 0; d < 7; d++) {
    RGB col = RGB((uint32_t)(d == today ? cfg.weekdayActive : cfg.weekdayColor));
    for (int k = 0; k < seg; k++) c.set(start + d * (seg + 1) + k, y, col);
  }
}

static RenderInfo renderContent(Canvas& c, const Slot& s, uint32_t now) {
  RenderInfo info;
  Content ct;
  buildContent(s, ct);
  c.clear();
  uint32_t elapsed = now - s.start;
  if (ct.effect.length()) Effects::render(c, ct.effect, now);

  int x0 = 0;
  bool hasIcon = false;
  if (ct.calendar) {
    drawCalendar(c, 0, 0, ct.calendarDay, now);
    hasIcon = true;
  } else if (ct.icon.length()) {
    hasIcon = Icons::draw(c, 0, (c.h - 8) / 2, ct.icon, now);
  }
  if (hasIcon) x0 = 9;
  int x1 = c.w - 1;
  int area = x1 - x0 + 1;
  bool bottomBar = ct.progress >= 0 || (ct.weekday && c.h >= 8);

  // keep text readable on top of an animated background
  if (ct.effect.length() && ct.text.length()) {
    int y0 = ct.big ? 0 : cfg.textY + (c.h - 8) / 2 - 1;
    int y1 = ct.big ? 6 : y0 + 6;
    for (int y = max(0, y0); y <= min(c.h - 1, y1); y++)
      for (int x = x0; x <= x1; x++) c.set(x, y, c.get(x, y).scale(50));
  }

  if (ct.big) {
    int tw = bigTextWidth(ct.text);
    int y = bottomBar ? 0 : (c.h - 7) / 2;
    drawBigText(c, x0 + (area - tw + 1) / 2, y, ct.text, ct.color, {x0, x1}, ct.colonMask);
  } else if (ct.text.length()) {
    int tw = textWidth(ct.text);
    int y = cfg.textY + (c.h - 8) / 2;
    uint8_t hue = now / 10;
    if (tw <= area) {
      drawText(c, x0 + (area - tw + 1) / 2, y, ct.text, ct.color, {x0, x1}, ct.rainbow, hue);
    } else {
      info.scrolling = true;
      const uint32_t pause = 1000;
      int32_t phase = elapsed > pause ? (int32_t)((uint64_t)(elapsed - pause) * cfg.scrollSpeed / 1000) : 0;
      int x;
      if (phase < tw) {
        x = x0 - phase;
      } else {
        int32_t q = (phase - tw) % (tw + area);
        info.passes = 1 + (phase - tw) / (tw + area);
        x = x0 + area - q;
      }
      drawText(c, x, y, ct.text, ct.color, {x0, x1}, ct.rainbow, hue);
    }
  }

  if (ct.progress >= 0) {
    int y = c.h - 1;
    int filled = (area * min(ct.progress, 100) + 50) / 100;
    for (int x = 0; x < area; x++) c.set(x0 + x, y, x < filled ? ct.progressColor : ct.progressColor.scale(40));
  } else if (ct.weekday && c.h >= 8) {
    drawWeekday(c, x0, x1);
  }
  return info;
}

static void drawIndicators(Canvas& c, uint32_t now) {
  const int W = c.w, H = c.h;
  for (int i = 0; i < NUM_INDICATORS; i++) {
    Indicator& d = ind[i];
    if (d.expires && (int32_t)(now - d.expires) >= 0) { d.mqttColor = -1; d.expires = 0; }
    int32_t col = d.mqttColor >= 0 ? d.mqttColor : d.tplColor;
    if (col <= 0) continue;
    if (d.blink && (now / d.blink) % 2) continue;
    RGB rc((uint32_t)col);
    if (i == 0) { c.set(W - 1, 0, rc); c.set(W - 2, 0, rc); c.set(W - 1, 1, rc); }
    else if (i == 1) { c.set(W - 1, H / 2 - 1, rc); c.set(W - 1, H / 2, rc); }
    else { c.set(W - 1, H - 1, rc); c.set(W - 2, H - 1, rc); c.set(W - 1, H - 2, rc); }
  }
}

static void drawTest(Canvas& c, uint32_t now) {
  c.clear();
  int col = (now / 150) % c.w;
  for (int y = 0; y < c.h; y++) c.set(col, y, RGB(60, 60, 0));
  int row = (now / 400) % c.h;
  for (int x = 0; x < c.w; x++) if (!c.get(x, row).r) c.set(x, row, RGB(0, 30, 60));
  c.set(0, 0, RGB(255, 0, 0));
  c.set(c.w - 1, 0, RGB(0, 255, 0));
  c.set(0, c.h - 1, RGB(0, 0, 255));
  c.set(c.w - 1, c.h - 1, RGB(255, 255, 255));
  drawText(c, 3, 1, "TEST", RGB(255, 80, 0), {0, c.w - 1});
}

static void drawOta(Canvas& c) {
  c.clear();
  drawText(c, 1, 1, "OTA", RGB(0, 150, 255), {0, c.w - 1});
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", otaPct);
  String s(buf);
  drawText(c, c.w - textWidth(s) - 1, 1, s, RGB(255, 255, 255), {0, c.w - 1});
  int filled = c.w * otaPct / 100;
  for (int x = 0; x < c.w; x++) c.set(x, c.h - 1, x < filled ? RGB(0, 255, 0) : RGB(0, 30, 0));
}

// ================================================================ state machine
static bool slotDone(Slot& s, const RenderInfo& info, uint32_t now) {
  uint32_t durMs;
  int minPasses = 1;
  switch (s.kind) {
    case SK_PAGE: durMs = cfg.pages[s.idx].duration * 1000UL; break;
    case SK_CUSTOM: durMs = custom[s.idx].duration * 1000UL; break;
    case SK_NOTIF:
      if (notifDismissed) return true;
      if (curNotif.hold) return false;
      durMs = curNotif.durationMs;
      minPasses = max<int>(1, curNotif.repeat);
      if (info.scrolling) durMs = 0;  // scrolling notifications end after `repeat` passes
      break;
    default: durMs = 10000; break;
  }
  if (now - s.start < durMs) return false;
  if (!info.scrolling) return true;
  if (!s.durReached) {
    s.durReached = true;
    s.passesAtDur = info.passes;
  }
  if (s.kind == SK_NOTIF) return info.passes >= minPasses;
  return info.passes > s.passesAtDur || (s.passesAtDur == 0 && info.passes >= 1);
}

static void popNotif() {
  notifDismissed = false;
  curNotif = queue[0];
  for (int i = 1; i < qLen; i++) queue[i - 1] = queue[i];
  qLen--;
  queue[qLen] = Notif();
}

static void advance(uint32_t now) {
  // start pending notification
  if (qLen > 0 && cur.kind != SK_NOTIF) {
    if (cur.kind == SK_PAGE || cur.kind == SK_CUSTOM || cur.kind == SK_FALLBACK) resumeSlot = cur;
    popNotif();
    wakeActive = curNotif.wakeup;
    Slot s;
    s.kind = SK_NOTIF;
    switchTo(s);
    return;
  }
  bool done = cur.kind != SK_NONE && slotDone(cur, curInfo, now);
  if (cur.kind == SK_NOTIF) {
    if (!done) return;
    if (qLen > 0) {
      popNotif();
      wakeActive = curNotif.wakeup;
      Slot s;
      s.kind = SK_NOTIF;
      switchTo(s);
      return;
    }
    wakeActive = false;
    Slot back = resumeSlot;
    if (!slotValid(back)) back = slotFromPos(findNext(slotPos(back), 1));
    switchTo(back);
    return;
  }
  if (!slotValid(cur)) {
    switchTo(slotFromPos(findNext(slotPos(cur), 1)));
    return;
  }
  if (done && cfg.autoRotate) {
    int nxt = findNext(slotPos(cur), 1);
    if (nxt >= 0 && nxt != slotPos(cur)) switchTo(slotFromPos(nxt));
    else { cur.start = now; cur.durReached = false; }
  }
}

static void composite(uint32_t now) {
  if (!transActive) {
    cvOut.copyFrom(cvA);
    return;
  }
  uint32_t dt = now - transStart;
  uint16_t tms = max<uint16_t>(cfg.transitionMs, 50);
  if (dt >= tms) {
    transActive = false;
    cvOut.copyFrom(cvA);
    return;
  }
  // ease in-out
  float p = (float)dt / tms;
  p = p < 0.5f ? 2 * p * p : 1 - (-2 * p + 2) * (-2 * p + 2) / 2;
  const int W = cvA.w, H = cvA.h;
  switch (transType) {
    case 1: {  // slide horizontally
      int off = (int)(p * W);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          int sx = transDir > 0 ? x + off : x - off;
          RGB v;
          if (transDir > 0) v = sx < W ? cvB.get(sx, y) : cvA.get(sx - W, y);
          else v = sx >= 0 ? cvB.get(sx, y) : cvA.get(sx + W, y);
          cvOut.set(x, y, v);
        }
      break;
    }
    case 2: {  // slide up
      int off = (int)(p * H);
      for (int y = 0; y < H; y++) {
        int sy = y + off;
        for (int x = 0; x < W; x++) cvOut.set(x, y, sy < H ? cvB.get(x, sy) : cvA.get(x, sy - H));
      }
      break;
    }
    default: {  // fade
      uint8_t t = (uint8_t)(p * 255);
      for (int i = 0; i < W * H; i++) cvOut.buf[i] = blend(cvB.buf[i], cvA.buf[i], t);
      break;
    }
  }
}

void begin() {
  cvA.alloc(cfg.width, cfg.height);
  cvB.alloc(cfg.width, cfg.height);
  cvOut.alloc(cfg.width, cfg.height);
  cvLast.alloc(cfg.width, cfg.height);
  previewLen = cfg.width * cfg.height * 3;
  preview = (uint8_t*)calloc(previewLen, 1);
  cur = Slot();
  onPagesChanged();
}

void frame() {
  uint32_t now = millis();
  uint8_t bright;
  {
    Lock l;
    if (otaPct >= 0) {
      drawOta(cvOut);
    } else if ((int32_t)(testUntil - now) > 0) {
      drawTest(cvOut, now);
    } else {
      advance(now);
      if (cur.kind == SK_NONE) switchTo(slotFromPos(findNext(-1, 1)));
      curInfo = renderContent(cvA, cur, now);
      cvLast.copyFrom(cvA);
      composite(now);
      drawIndicators(cvOut, now);
    }
    memcpy(preview, cvOut.buf, min(previewLen, (size_t)cvOut.w * cvOut.h * 3));
    frameCounter++;
    bright = targetBrightness();
  }
  Display::show(cvOut, bright);
}

// ================================================================ public API
void next() {
  Lock l;
  if (cur.kind == SK_NOTIF) { dismiss(); return; }
  int n = findNext(slotPos(cur), 1);
  if (n >= 0) switchTo(slotFromPos(n), 1);
}

void prev() {
  Lock l;
  if (cur.kind == SK_NOTIF) { dismiss(); return; }
  int n = findNext(slotPos(cur) < 0 ? 0 : slotPos(cur), -1);
  if (n >= 0) switchTo(slotFromPos(n), -1);
}

bool gotoPage(const String& key) {
  Lock l;
  for (int i = 0; i < cfg.pageCount; i++) {
    if (cfg.pages[i].name == key) { switchTo(slotFromPos(i)); return true; }
  }
  for (int i = 0; i < MAX_CUSTOM; i++) {
    if (custom[i].used && custom[i].name == key) { switchTo(slotFromPos(cfg.pageCount + i)); return true; }
  }
  bool numeric = key.length() > 0;
  for (char ch : key) if (!isdigit((unsigned char)ch)) numeric = false;
  if (numeric) {
    int i = key.toInt();
    if (i >= 0 && i < cfg.pageCount) { switchTo(slotFromPos(i)); return true; }
  }
  return false;
}

String currentName() {
  Lock l;
  const Slot& s = (cur.kind == SK_NOTIF) ? resumeSlot : cur;
  if (s.kind == SK_PAGE && s.idx < cfg.pageCount) return cfg.pages[s.idx].name;
  if (s.kind == SK_CUSTOM) return custom[s.idx].name;
  return "";
}

void buttonAction() {
  String domain, service, entity;
  {
    Lock l;
    if (cur.kind == SK_NOTIF) { dismiss(); return; }
    if (cur.kind != SK_PAGE) { next(); return; }
    const PageCfg& p = cfg.pages[cur.idx];
    entity = p.actionEntity.length() ? p.actionEntity : p.entity;
    if (p.action == "toggle" && entity.length()) {
      domain = "homeassistant";
      service = "toggle";
    } else if (p.action == "service" && p.actionService.indexOf('.') > 0) {
      domain = p.actionService.substring(0, p.actionService.indexOf('.'));
      service = p.actionService.substring(p.actionService.indexOf('.') + 1);
    } else {
      next();
      return;
    }
  }
  bool ok = HA::callService(domain, service, entity);
  systemMessage(ok ? "OK" : "HA?", ok ? "check" : "error", 1200, false);
}

static void parseNotif(JsonVariantConst v, Notif& n, bool& stack) {
  stack = true;
  if (v.is<const char*>()) {
    n.text = v.as<const char*>();
    return;
  }
  const char* txt = v["text"] | (const char*)nullptr;
  if (!txt) txt = v["message"] | "";
  n.text = txt;
  n.icon = v["icon"] | "";
  n.effect = v["effect"] | "";
  n.color = variantColor(v["color"], -1);
  n.progressColor = variantColor(v["progress_color"], -1);
  n.durationMs = (uint32_t)((v["duration"] | 5.0f) * 1000);
  n.repeat = v["repeat"] | 1;
  n.rainbow = v["rainbow"] | false;
  n.hold = v["hold"] | false;
  n.wakeup = v["wakeup"] | false;
  n.progress = v["progress"] | -1;
  stack = v["stack"] | true;
  // HA notify service sends {"message": "...", "title": "...", "data": {...}}
  JsonVariantConst d = v["data"];
  if (d.is<JsonObjectConst>()) {
    if (n.icon.isEmpty()) n.icon = d["icon"] | "";
    if (n.color < 0) n.color = variantColor(d["color"], -1);
    if (d["duration"].is<float>()) n.durationMs = (uint32_t)(d["duration"].as<float>() * 1000);
    n.rainbow = d["rainbow"] | n.rainbow;
    n.hold = d["hold"] | n.hold;
    n.wakeup = d["wakeup"] | n.wakeup;
    n.repeat = d["repeat"] | n.repeat;
  }
  const char* title = v["title"] | (const char*)nullptr;
  if (title && *title) n.text = String(title) + ": " + n.text;
}

static void enqueue(const Notif& n, bool stack) {
  Lock l;
  if (!stack) {
    qLen = 0;
    if (cur.kind == SK_NOTIF) notifDismissed = true;
  }
  if (qLen >= MAX_NOTIF) {
    for (int i = 1; i < qLen; i++) queue[i - 1] = queue[i];
    qLen--;
  }
  queue[qLen++] = n;
}

void notify(JsonVariantConst v) {
  Notif n;
  bool stack;
  parseNotif(v, n, stack);
  if (n.text.isEmpty() && n.icon.isEmpty() && n.effect.isEmpty()) return;
  enqueue(n, stack);
}

void systemMessage(const String& text, const String& icon, uint32_t durationMs, bool wakeup) {
  Notif n;
  n.text = text;
  n.icon = icon;
  n.durationMs = durationMs;
  n.wakeup = wakeup;
  enqueue(n, true);
}

bool dismiss() {
  Lock l;
  if (cur.kind != SK_NOTIF) return false;
  notifDismissed = true;
  return true;
}

int queueLength() {
  Lock l;
  return qLen + (cur.kind == SK_NOTIF ? 1 : 0);
}

void setCustom(const String& name, JsonVariantConst v) {
  Lock l;
  int slot = -1;
  for (int i = 0; i < MAX_CUSTOM; i++) if (custom[i].used && custom[i].name == name) slot = i;
  bool empty = v.isNull() || (v.is<const char*>() && strlen(v.as<const char*>()) == 0) ||
               (v.is<JsonObjectConst>() && v.as<JsonObjectConst>().size() == 0);
  if (empty) {
    if (slot >= 0) custom[slot] = CustomPage();
    return;
  }
  if (slot < 0) {
    for (int i = 0; i < MAX_CUSTOM; i++) if (!custom[i].used) { slot = i; break; }
    if (slot < 0) {  // replace the one expiring soonest
      slot = 0;
    }
  }
  CustomPage& c = custom[slot];
  c = CustomPage();
  c.used = true;
  c.name = name;
  if (v.is<const char*>()) {
    c.text = v.as<const char*>();
  } else {
    c.text = v["text"] | "";
    c.icon = v["icon"] | "";
    c.effect = v["effect"] | "";
    c.color = variantColor(v["color"], -1);
    c.progressColor = variantColor(v["progress_color"], -1);
    c.progress = v["progress"] | -1;
    c.duration = constrain((int)(v["duration"] | 8), 1, 3600);
    c.rainbow = v["rainbow"] | false;
    uint32_t life = v["lifetime"] | 0;
    if (life) c.expires = millis() + life * 1000;
  }
}

void customList(JsonArray a) {
  Lock l;
  for (int i = 0; i < MAX_CUSTOM; i++) {
    if (!custom[i].used) continue;
    JsonObject o = a.add<JsonObject>();
    o["name"] = custom[i].name;
    o["text"] = custom[i].text;
    o["icon"] = custom[i].icon;
  }
}

void setIndicator(int idx, JsonVariantConst v) {
  if (idx < 0 || idx >= NUM_INDICATORS) return;
  Lock l;
  Indicator& d = ind[idx];
  if (v.isNull() || (v.is<const char*>() && !strlen(v.as<const char*>()))) {
    d.mqttColor = -1;
    d.blink = 0;
    d.expires = 0;
    return;
  }
  if (v.is<const char*>()) {
    d.mqttColor = parseColor(v.as<const char*>(), -1);
    d.blink = 0;
    d.expires = 0;
    return;
  }
  d.mqttColor = variantColor(v["color"], -1);
  d.blink = v["blink"] | 0;
  uint32_t life = v["lifetime"] | 0;
  d.expires = life ? millis() + life * 1000 : 0;
}

static bool truthy(String v) {
  v.trim();
  v.toLowerCase();
  return v == "true" || v == "on" || v == "1" || v == "yes" || v == "home" || v == "open";
}

void setTemplateResult(uint8_t page, uint8_t field, const String& value, bool error) {
  if (page >= MAX_PAGES) return;
  Lock l;
  PageRt& r = rt[page];
  switch (field) {
    case F_TEXT:
      r.text = value;
      r.text.trim();
      r.text.replace("\n", " ");
      r.textSet = true;
      r.textError = error;
      break;
    case F_COLOR: r.color = error ? -1 : parseColor(value, -1); break;
    case F_ICON: {
      String v = value;
      v.trim();
      r.icon = error ? "" : v;
      break;
    }
    case F_VISIBLE: r.visible = !error && truthy(value); break;
    case F_PROGRESS: {
      String v = value;
      v.trim();
      bool num = v.length() > 0 && (isdigit((unsigned char)v[0]) || v[0] == '-' || v[0] == '.');
      r.progress = (!error && num) ? constrain((int)(v.toFloat() + 0.5f), 0, 100) : -1;
      if (!error && num && v.toFloat() < 0) r.progress = -1;
      break;
    }
  }
}

void setIndicatorTemplate(uint8_t idx, const String& value) {
  if (idx >= NUM_INDICATORS) return;
  Lock l;
  String v = value;
  v.trim();
  ind[idx].tplColor = parseColor(v, -1);
}

void onPagesChanged() {
  Lock l;
  for (int i = 0; i < MAX_PAGES; i++) {
    rt[i] = PageRt();
    // pages with a visibility template stay hidden until HA reports
    if (i < cfg.pageCount && !cfg.pages[i].visibleTpl.isEmpty()) rt[i].visible = false;
  }
  for (int i = 0; i < NUM_INDICATORS; i++) ind[i].tplColor = -1;
  if (cur.kind == SK_PAGE && cur.idx >= cfg.pageCount) cur = Slot();
  if (resumeSlot.kind == SK_PAGE && resumeSlot.idx >= cfg.pageCount) resumeSlot = Slot();
}

void showTest(uint32_t durationMs) {
  Lock l;
  testUntil = millis() + durationMs;
}

void setOtaProgress(int pct) {
  Lock l;
  otaPct = pct;
}

size_t copyFrame(uint8_t* out, size_t maxLen, uint32_t& frameNo) {
  Lock l;
  size_t n = min(maxLen, previewLen);
  memcpy(out, preview, n);
  frameNo = frameCounter;
  return n;
}

void statusJson(JsonObject o) {
  Lock l;
  const Slot& s = (cur.kind == SK_NOTIF) ? resumeSlot : cur;
  o["page"] = (s.kind == SK_PAGE && s.idx < cfg.pageCount) ? cfg.pages[s.idx].name
              : (s.kind == SK_CUSTOM) ? custom[s.idx].name : String("");
  o["page_index"] = s.kind == SK_PAGE ? s.idx : -1;
  o["notification"] = cur.kind == SK_NOTIF;
  o["queue"] = qLen;
  o["night"] = isNight();
  o["target_brightness"] = targetBrightness();
  JsonArray pr = o["pages"].to<JsonArray>();
  for (int i = 0; i < cfg.pageCount; i++) {
    JsonObject p = pr.add<JsonObject>();
    p["text"] = rt[i].text;
    p["visible"] = pageShown(i);
    p["error"] = rt[i].textError;
  }
}

}  // namespace Apps
