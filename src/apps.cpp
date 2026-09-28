#include "apps.h"
#include <time.h>
#include "config.h"
#include "display.h"
#include "effects.h"
#include "ha_client.h"
#include "icons.h"
#include <vector>

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

// A notification or a custom app pushed over MQTT / HTTP (AWTRIX 3 compatible format)
struct Frag {
  String t;
  int32_t c;
  Frag(const String& t_ = String(), int32_t c_ = -1) : t(t_), c(c_) {}
};

struct DrawCmd {
  uint8_t op = 0;  // 1 dp, 2 dl, 3 dr, 4 df, 5 dc, 6 dfc, 7 dt, 8 db
  int16_t v[4] = {0, 0, 0, 0};
  int32_t col = 0xFFFFFF;
  String text;
  std::vector<uint32_t> bmp;
};

struct AppMsg {
  bool used = false;
  String name;
  std::vector<Frag> text;
  String icon, effect;
  int32_t color = -1, background = -1, progressColor = -1, progressBg = -1, barBg = -1;
  int32_t grad1 = -1, grad2 = -1;
  bool rainbow = false, hold = false, wakeup = false, noScroll = false, center = true, topText = false;
  bool autoscale = true;
  uint8_t textCase = 0;  // 0 global, 1 upper, 2 as sent
  int16_t textOffset = 0;
  uint16_t blinkText = 0, fadeText = 0, scrollPct = 100;
  int repeat = -1;
  uint32_t durationMs = 0;  // 0 = default
  int progress = -1;
  std::vector<int16_t> bar, line;
  std::vector<DrawCmd> draw;
  uint32_t expires = 0;      // millis, 0 = never
  uint8_t lifetimeMode = 0;  // 0 delete, 1 mark stale
  uint32_t lifetimeMs = 0;
  bool stale = false;

  String plainText() const {
    String s;
    for (auto& f : text) s += f.t;
    return s;
  }
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
static const int MAX_CUSTOM = 16;

static PageRt rt[MAX_PAGES];
static AppMsg queue[MAX_NOTIF];
static int qLen = 0;
static AppMsg curNotif;
static AppMsg custom[MAX_CUSTOM];

struct Mood {
  bool on = false;
  RGB color = RGB(255, 180, 100);
  uint8_t bri = 100;
};
static Mood mood;
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
    if (custom[i].lifetimeMode == 1) {
      custom[i].stale = true;  // keep it, draw a red frame
    } else {
      custom[i] = AppMsg();
      return false;
    }
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
  const AppMsg* msg = nullptr;  // extra AWTRIX features (fragments, charts, drawing...)
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
    case SK_CUSTOM:
    case SK_NOTIF: {
      const AppMsg& m = s.kind == SK_CUSTOM ? custom[s.idx] : curNotif;
      ct.msg = &m;
      ct.icon = m.icon;
      ct.text = m.plainText();
      ct.effect = m.effect;
      ct.color = colorOr(m.color, defColor);
      ct.rainbow = m.rainbow;
      ct.progress = m.progress;
      if (m.progressColor >= 0) ct.progressColor = RGB((uint32_t)m.progressColor);
      break;
    }
    case SK_FALLBACK:
    default:
      ct.big = true;
      clockText(ct, true);
      ct.color = RGB((uint32_t)defColor);
      ct.weekday = cfg.weekdayBar;
      break;
  }
  uint8_t tc = ct.msg ? ct.msg->textCase : 0;
  if (!ct.big && (tc == 1 || (tc == 0 && cfg.uppercase))) ct.text = toUpperUtf8(ct.text);
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

// charts: AWTRIX style bar graph / line chart in the area x0..x1
static void drawChart(Canvas& c, const AppMsg& m, int x0, int x1, bool line) {
  const std::vector<int16_t>& v = line ? m.line : m.bar;
  if (v.empty()) return;
  int area = x1 - x0 + 1, H = c.h;
  int n = min<int>(v.size(), line ? area : (area + 1) / 2);
  const int16_t* d = v.data() + (v.size() - n);  // newest values
  int vmax = 1, vmin = 0;
  for (int i = 0; i < n; i++) { vmax = max<int>(vmax, d[i]); vmin = min<int>(vmin, d[i]); }
  auto scale = [&](int val) {
    if (!m.autoscale) return constrain(val, 0, H);
    return (int)((long)(val - vmin) * H / max(1, vmax - vmin));
  };
  RGB col = RGB((uint32_t)(m.color >= 0 ? m.color : cfg.textColor));
  if (line) {
    int px = -1, py = -1;
    for (int i = 0; i < n; i++) {
      int x = x0 + (n > 1 ? (long)i * (area - 1) / (n - 1) : 0);
      int y = H - 1 - min(H - 1, scale(d[i]));
      if (px >= 0) drawLine(c, px, py, x, y, col);
      else c.set(x, y, col);
      px = x;
      py = y;
    }
  } else {
    int bw = max(1, (area - (n - 1)) / n);
    for (int i = 0; i < n; i++) {
      int x = x0 + i * (bw + 1);
      int h = scale(d[i]);
      if (m.barBg >= 0) c.fillRect(x, 0, bw, H, RGB((uint32_t)m.barBg));
      c.fillRect(x, H - h, bw, h, col);
    }
  }
}

static void drawCommands(Canvas& c, const AppMsg& m) {
  for (const DrawCmd& d : m.draw) {
    RGB col((uint32_t)d.col);
    switch (d.op) {
      case 1: c.set(d.v[0], d.v[1], col); break;
      case 2: drawLine(c, d.v[0], d.v[1], d.v[2], d.v[3], col); break;
      case 3: drawRect(c, d.v[0], d.v[1], d.v[2], d.v[3], col); break;
      case 4: c.fillRect(d.v[0], d.v[1], d.v[2], d.v[3], col); break;
      case 5: drawCircle(c, d.v[0], d.v[1], d.v[2], col, false); break;
      case 6: drawCircle(c, d.v[0], d.v[1], d.v[2], col, true); break;
      case 7: drawText(c, d.v[0], d.v[1], d.text, col, {0, c.w - 1}); break;
      case 8:
        for (int i = 0; i < (int)d.bmp.size() && i < d.v[2] * d.v[3]; i++)
          c.set(d.v[0] + i % d.v[2], d.v[1] + i / d.v[2], RGB(d.bmp[i]));
        break;
    }
  }
}

static RenderInfo renderContent(Canvas& c, const Slot& s, uint32_t now) {
  RenderInfo info;
  Content ct;
  buildContent(s, ct);
  const AppMsg* m = ct.msg;
  c.clear();
  uint32_t elapsed = now - s.start;
  if (m && m->background >= 0) c.fillRect(0, 0, c.w, c.h, RGB((uint32_t)m->background));
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

  bool chart = m && (!m->bar.empty() || !m->line.empty());
  if (chart) {
    drawChart(c, *m, x0, x1, m->bar.empty());
  } else if (ct.big) {
    int tw = bigTextWidth(ct.text);
    int y = bottomBar ? 0 : (c.h - 7) / 2;
    drawBigText(c, x0 + (area - tw + 1) / 2, y, ct.text, ct.color, {x0, x1}, ct.colonMask);
  } else if (ct.text.length()) {
    int tw = textWidth(ct.text);
    int y = (m && m->topText) ? 0 : cfg.textY + (c.h - 8) / 2;
    uint8_t hue = now / 10;
    int x;
    int offset = m ? m->textOffset : 0;
    bool noScroll = m && m->noScroll;
    if (tw <= area || noScroll) {
      bool center = !m || m->center;
      x = center && tw <= area ? x0 + (area - tw + 1) / 2 : x0 + offset;
    } else {
      info.scrolling = true;
      const uint32_t pause = 1000;
      uint32_t speed = (uint32_t)cfg.scrollSpeed * (m ? m->scrollPct : 100) / 100;
      int32_t phase = elapsed > pause ? (int32_t)((uint64_t)(elapsed - pause) * max<uint32_t>(1, speed) / 1000) : 0;
      if (phase < tw) {
        x = x0 + offset - phase;
      } else {
        int32_t q = (phase - tw) % (tw + area);
        info.passes = 1 + (phase - tw) / (tw + area);
        x = x0 + area - q;
      }
    }
    // text visibility effects
    bool visible = true;
    uint8_t fade = 255;
    if (m && m->blinkText) visible = (now / m->blinkText) % 2 == 0;
    if (m && m->fadeText) {
      uint32_t ph = now % (2 * m->fadeText);
      fade = ph < m->fadeText ? 255 * ph / m->fadeText : 255 * (2 * m->fadeText - ph) / m->fadeText;
    }
    if (visible) {
      Clip clip = {x0, x1};
      bool fragColors = false;
      if (m) for (auto& f : m->text) if (f.c >= 0) fragColors = true;
      if (m && fragColors) {
        // colored text fragments
        int fx = x;
        for (auto& f : m->text) {
          String t = (m->textCase == 1 || (m->textCase == 0 && cfg.uppercase)) ? toUpperUtf8(f.t) : f.t;
          if (t.isEmpty()) continue;
          RGB fc = (f.c >= 0 ? RGB((uint32_t)f.c) : ct.color).scale(fade == 255 ? 255 : fade);
          int w = drawText(c, fx, y, t, fc, clip);
          fx += w + 1;
        }
      } else if (m && m->grad1 >= 0 && m->grad2 >= 0) {
        int nch = max(1, charCount(ct.text) - 1);
        RGB a((uint32_t)m->grad1), b((uint32_t)m->grad2);
        drawTextFn(c, x, y, ct.text, [&](int n) { return blend(a, b, 255 * n / nch).scale(fade); }, clip);
      } else if (ct.rainbow) {
        drawText(c, x, y, ct.text, ct.color, clip, true, hue);
      } else {
        drawText(c, x, y, ct.text, fade == 255 ? ct.color : ct.color.scale(fade), clip);
      }
    }
  }

  if (ct.progress >= 0) {
    int y = c.h - 1;
    int filled = (area * min(ct.progress, 100) + 50) / 100;
    RGB bg = (m && m->progressBg >= 0) ? RGB((uint32_t)m->progressBg) : ct.progressColor.scale(40);
    for (int x = 0; x < area; x++) c.set(x0 + x, y, x < filled ? ct.progressColor : bg);
  } else if (ct.weekday && c.h >= 8) {
    drawWeekday(c, x0, x1);
  }
  if (m && !m->draw.empty()) drawCommands(c, *m);
  if (m && m->stale) drawRect(c, 0, 0, c.w, c.h, RGB(255, 0, 0));
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
    case SK_CUSTOM: {
      const AppMsg& m = custom[s.idx];
      if (m.repeat > 0 && info.scrolling) return info.passes >= m.repeat;
      durMs = m.durationMs ? m.durationMs : cfg.appTime * 1000UL;
      break;
    }
    case SK_NOTIF:
      if (notifDismissed) return true;
      if (curNotif.hold) return false;
      if (curNotif.repeat > 0 && info.scrolling) return info.passes >= curNotif.repeat;
      durMs = curNotif.durationMs ? curNotif.durationMs : 5000;
      if (info.scrolling) durMs = 0;  // scrolling notification: show the whole text once
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
  queue[qLen] = AppMsg();
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
    } else if (mood.on && qLen == 0 && cur.kind != SK_NOTIF) {
      cvOut.fillRect(0, 0, cvOut.w, cvOut.h, mood.color);
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
    bright = (mood.on && cfg.power && qLen == 0 && cur.kind != SK_NOTIF) ? mood.bri : targetBrightness();
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

// ---------------------------------------------------------------- message parsing (AWTRIX 3 format + own keys)
static int32_t colorKey(JsonVariantConst v, const char* a, const char* b = nullptr) {
  int32_t c = variantColor(v[a], -1);
  if (c < 0 && b) c = variantColor(v[b], -1);
  return c;
}

static void parseMsg(JsonVariantConst v, AppMsg& m, bool isNotif, bool& stack) {
  stack = true;
  m.used = true;
  if (v.is<const char*>()) {
    m.text.push_back({v.as<const char*>(), -1});
    return;
  }
  // text: string or array of fragments [{"t":"..","c":"FF0000"}]
  JsonVariantConst t = v["text"];
  if (t.isNull()) t = v["message"];  // HA notify payload
  if (t.is<JsonArrayConst>()) {
    for (JsonVariantConst f : t.as<JsonArrayConst>()) {
      Frag fr;
      fr.t = f["t"] | "";
      fr.c = variantColor(f["c"], -1);
      m.text.push_back(fr);
    }
  } else if (!t.isNull()) {
    m.text.push_back({t.as<String>(), -1});
  }
  const char* title = v["title"] | (const char*)nullptr;
  if (title && *title && !m.text.empty()) m.text[0].t = String(title) + ": " + m.text[0].t;

  if (v["icon"].is<int>()) m.icon = String(v["icon"].as<int>());
  else m.icon = v["icon"] | "";
  m.effect = v["effect"] | "";
  m.color = colorKey(v, "color");
  m.background = colorKey(v, "background");
  m.progressColor = colorKey(v, "progressC", "progress_color");
  m.progressBg = colorKey(v, "progressBC");
  m.barBg = colorKey(v, "barBC");
  JsonArrayConst g = v["gradient"];
  if (g.size() >= 2) {
    m.grad1 = variantColor(g[0], -1);
    m.grad2 = variantColor(g[1], -1);
  }
  m.rainbow = v["rainbow"] | false;
  m.hold = v["hold"] | false;
  m.wakeup = v["wakeup"] | false;
  m.noScroll = v["noScroll"] | false;
  m.center = v["center"] | true;
  m.topText = v["topText"] | false;
  m.autoscale = v["autoscale"] | true;
  m.textCase = v["textCase"] | 0;
  m.textOffset = v["textOffset"] | 0;
  m.blinkText = v["blinkText"] | 0;
  m.fadeText = v["fadeText"] | 0;
  m.scrollPct = constrain((int)(v["scrollSpeed"] | 100), 10, 500);
  m.repeat = v["repeat"] | -1;
  float dur = v["duration"] | 0.0f;
  m.durationMs = dur > 0 ? (uint32_t)(dur * 1000) : 0;
  m.progress = v["progress"] | -1;
  if (m.progress > 100) m.progress = 100;
  for (JsonVariantConst x : v["bar"].as<JsonArrayConst>()) m.bar.push_back(x.as<int>());
  for (JsonVariantConst x : v["line"].as<JsonArrayConst>()) m.line.push_back(x.as<int>());
  // drawing instructions
  for (JsonObjectConst o : v["draw"].as<JsonArrayConst>()) {
    for (JsonPairConst kv : o) {
      static const char* const OPS[] = {"", "dp", "dl", "dr", "df", "dc", "dfc", "dt", "db"};
      DrawCmd d;
      for (uint8_t k = 1; k <= 8; k++) if (!strcmp(kv.key().c_str(), OPS[k])) d.op = k;
      JsonArrayConst a = kv.value().as<JsonArrayConst>();
      if (!d.op || a.isNull()) continue;
      int nNum = d.op == 1 ? 2 : (d.op == 5 || d.op == 6) ? 3 : d.op == 7 ? 2 : 4;
      for (int k = 0; k < nNum && k < 4; k++) d.v[k] = a[k] | 0;
      if (d.op == 7) {
        d.text = a[2] | "";
        d.col = variantColor(a[3], 0xFFFFFF);
      } else if (d.op == 8) {
        for (JsonVariantConst px : a[4].as<JsonArrayConst>()) d.bmp.push_back(px.as<uint32_t>());
      } else {
        d.col = variantColor(a[nNum], 0xFFFFFF);
      }
      if (m.draw.size() < 64) m.draw.push_back(d);
    }
  }
  if (!isNotif) {
    uint32_t life = v["lifetime"] | 0;
    m.lifetimeMs = life * 1000;
    m.lifetimeMode = v["lifetimeMode"] | 0;
    if (life) m.expires = millis() + m.lifetimeMs;
  }
  stack = v["stack"] | true;
  // HA notify service: {"message": "...", "data": {...}}
  JsonVariantConst d = v["data"];
  if (d.is<JsonObjectConst>()) {
    if (m.icon.isEmpty()) m.icon = d["icon"] | "";
    if (m.color < 0) m.color = variantColor(d["color"], -1);
    if (d["duration"].is<float>()) m.durationMs = (uint32_t)(d["duration"].as<float>() * 1000);
    m.rainbow = d["rainbow"] | m.rainbow;
    m.hold = d["hold"] | m.hold;
    m.wakeup = d["wakeup"] | m.wakeup;
    m.repeat = d["repeat"] | m.repeat;
  }
}

static bool msgEmpty(const AppMsg& m) {
  return m.plainText().isEmpty() && m.icon.isEmpty() && m.effect.isEmpty() && m.bar.empty() && m.line.empty() &&
         m.draw.empty() && m.progress < 0 && m.background < 0;
}

static void enqueue(const AppMsg& n, bool stack) {
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
  AppMsg n;
  bool stack;
  parseMsg(v, n, true, stack);
  if (msgEmpty(n)) return;
  enqueue(n, stack);
}

void systemMessage(const String& text, const String& icon, uint32_t durationMs, bool wakeup) {
  AppMsg n;
  n.used = true;
  n.text.push_back({text, -1});
  n.icon = icon;
  n.durationMs = durationMs;
  n.wakeup = wakeup;
  n.repeat = 1;
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

static void setOneCustom(const String& name, JsonVariantConst v) {
  int slot = -1;
  for (int i = 0; i < MAX_CUSTOM; i++) if (custom[i].used && custom[i].name == name) slot = i;
  if (slot < 0) {
    for (int i = 0; i < MAX_CUSTOM; i++) if (!custom[i].used) { slot = i; break; }
    if (slot < 0) {  // full: replace the oldest one
      slot = 0;
    }
  }
  AppMsg& c = custom[slot];
  c = AppMsg();
  bool stack;
  parseMsg(v, c, false, stack);
  c.name = name;
}

void setCustom(const String& name, JsonVariantConst v) {
  Lock l;
  bool empty = v.isNull() || (v.is<const char*>() && strlen(v.as<const char*>()) == 0) ||
               (v.is<JsonObjectConst>() && v.as<JsonObjectConst>().size() == 0) ||
               (v.is<JsonArrayConst>() && v.as<JsonArrayConst>().size() == 0);
  if (empty) {
    // AWTRIX: deletes the app and all apps starting with the name (name0, name1, ...)
    for (int i = 0; i < MAX_CUSTOM; i++)
      if (custom[i].used && custom[i].name.startsWith(name)) custom[i] = AppMsg();
    return;
  }
  if (v.is<JsonArrayConst>()) {
    int k = 0;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) setOneCustom(name + String(k++), item);
    return;
  }
  setOneCustom(name, v);
}

void customList(JsonArray a) {
  Lock l;
  for (int i = 0; i < MAX_CUSTOM; i++) {
    if (!custom[i].used) continue;
    JsonObject o = a.add<JsonObject>();
    o["name"] = custom[i].name;
    o["text"] = custom[i].plainText();
    o["icon"] = custom[i].icon;
  }
}

// ---------------------------------------------------------------- moodlight
static RGB kelvinToRgb(int k) {
  float t = constrain(k, 1000, 40000) / 100.0f;
  float r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.47f * logf(t) - 161.12f;
    b = t <= 19 ? 0 : 138.52f * logf(t - 10) - 305.04f;
  } else {
    r = 329.7f * powf(t - 60, -0.1332f);
    g = 288.12f * powf(t - 60, -0.0755f);
    b = 255;
  }
  return RGB(constrain((int)r, 0, 255), constrain((int)g, 0, 255), constrain((int)b, 0, 255));
}

void setMoodlight(JsonVariantConst v) {
  Lock l;
  if (v.isNull() || !v.is<JsonObjectConst>() || v.as<JsonObjectConst>().size() == 0) {
    mood.on = false;
    return;
  }
  mood.on = true;
  mood.bri = v["brightness"] | 100;
  if (v["kelvin"].is<int>()) mood.color = kelvinToRgb(v["kelvin"].as<int>());
  else mood.color = RGB((uint32_t)variantColor(v["color"], 0xFFB464));
}

bool moodlightOn() { return mood.on; }

// ---------------------------------------------------------------- AWTRIX helpers
bool switchApp(const String& name) {
  // AWTRIX built-in app names map onto the first page of that type
  int type = -1;
  if (name.equalsIgnoreCase("Time")) type = PT_CLOCK;
  else if (name.equalsIgnoreCase("Date")) type = PT_DATE;
  if (type >= 0) {
    Lock l;
    for (int i = 0; i < cfg.pageCount; i++)
      if (cfg.pages[i].type == type && pageShown(i)) { switchTo(slotFromPos(i)); return true; }
  }
  return gotoPage(name);
}

void loopJson(JsonObject o) {
  Lock l;
  int n = 0;
  for (int i = 0; i < cfg.pageCount; i++) if (pageShown(i)) o[cfg.pages[i].name] = n++;
  for (int i = 0; i < MAX_CUSTOM; i++) if (customShown(i)) o[custom[i].name] = n++;
}

bool indicatorOn(int idx) {
  if (idx < 0 || idx >= NUM_INDICATORS) return false;
  Lock l;
  return ind[idx].mqttColor > 0 || ind[idx].tplColor > 0;
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
  if (d.mqttColor == 0) d.mqttColor = -1;  // black / "0" hides the indicator
  d.blink = v["blink"] | 0;
  if (!d.blink) d.blink = v["fade"] | 0;
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
