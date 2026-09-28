#include "config.h"
#include <LittleFS.h>

Config cfg;
String deviceId;
SemaphoreHandle_t gLock = nullptr;
volatile bool reqSaveConfig = false, reqSavePages = false, reqReboot = false,
              reqHaResync = false, reqMqttRediscover = false, reqMqttState = false,
              reqSelect = false;

const char* const PAGE_TYPE_NAMES[PT_COUNT] = {"clock", "date", "entity", "template", "text", "effect"};

static const char* CONFIG_FILE = "/config.json";
static const char* PAGES_FILE = "/pages.json";
static const char* SECRET_MASK = "********";

// ---------------------------------------------------------------- colors
struct NamedColor { const char* name; uint32_t c; };
static const NamedColor NAMED[] = {
  {"white", 0xFFFFFF}, {"black", 0x000000}, {"red", 0xFF0000}, {"green", 0x00FF00},
  {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"orange", 0xFF8000}, {"cyan", 0x00FFFF},
  {"magenta", 0xFF00FF}, {"purple", 0x8000FF}, {"pink", 0xFF40A0}, {"gray", 0x808080},
  {"grey", 0x808080}, {"lime", 0x80FF00}, {"teal", 0x008080}, {"gold", 0xFFC000},
  {"cervena", 0xFF0000}, {"zelena", 0x00FF00}, {"modra", 0x0000FF}, {"zluta", 0xFFFF00},
  {"oranzova", 0xFF8000}, {"bila", 0xFFFFFF}, {"fialova", 0x8000FF}, {"ruzova", 0xFF40A0},
};

int32_t parseColor(const String& in, int32_t def) {
  String s = in;
  s.trim();
  if (s.isEmpty()) return def;
  if (s[0] == '#') s.remove(0, 1);
  if (s.length() == 6) {
    bool hex = true;
    for (char ch : s) if (!isxdigit((unsigned char)ch)) { hex = false; break; }
    if (hex) return (int32_t)strtoul(s.c_str(), nullptr, 16);
  }
  if (s.length() == 3) {  // #RGB
    bool hex = true;
    for (char ch : s) if (!isxdigit((unsigned char)ch)) { hex = false; break; }
    if (hex) {
      uint32_t v = strtoul(s.c_str(), nullptr, 16);
      uint32_t r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
      return (int32_t)((r * 17) << 16 | (g * 17) << 8 | (b * 17));
    }
  }
  // "r,g,b" or "[r, g, b]"
  s.replace("[", ""); s.replace("]", ""); s.replace("(", ""); s.replace(")", "");
  int c1 = s.indexOf(','), c2 = s.lastIndexOf(',');
  if (c1 > 0 && c2 > c1) {
    int r = constrain(s.substring(0, c1).toInt(), 0, 255);
    int g = constrain(s.substring(c1 + 1, c2).toInt(), 0, 255);
    int b = constrain(s.substring(c2 + 1).toInt(), 0, 255);
    return (r << 16) | (g << 8) | b;
  }
  s.toLowerCase();
  for (auto& n : NAMED) if (s == n.name) return (int32_t)n.c;
  return def;
}

String colorToHex(int32_t c) {
  if (c < 0) return "";
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06X", (unsigned)(c & 0xFFFFFF));
  return buf;
}

// ---------------------------------------------------------------- helpers
template <typename T>
static bool upd(T& dst, JsonVariantConst v, uint32_t& chg, uint32_t flag) {
  if (v.isNull() || !v.is<T>()) return false;
  T nv = v.as<T>();
  if (nv != dst) { dst = nv; chg |= flag; return true; }
  return false;
}
static bool updStr(String& dst, JsonVariantConst v, uint32_t& chg, uint32_t flag, bool secret = false) {
  if (v.isNull()) return false;
  String nv = v.as<String>();
  if (secret && nv == SECRET_MASK) return false;
  if (nv != dst) { dst = nv; chg |= flag; return true; }
  return false;
}
static bool updColor(int32_t& dst, JsonVariantConst v, uint32_t& chg, uint32_t flag) {
  if (v.isNull()) return false;
  int32_t nv = v.is<int>() ? v.as<int>() : parseColor(v.as<String>(), -1);
  if (nv != dst) { dst = nv; chg |= flag; return true; }
  return false;
}
static String secret(const String& s, bool withSecrets) {
  if (withSecrets || s.isEmpty()) return s;
  return SECRET_MASK;
}

void configInitDefaults() {
  cfg = Config();
  cfg.mqttTopic = "awtrix_" + deviceId;  // same prefix scheme as AWTRIX 3
  cfg.hostname = "hapanel-" + deviceId;
#if defined(HW_AWTRIX) || defined(HW_ULANZI)
  // AWTRIX 3 DIY wiring: matrix GPIO32, layout 0 (rows, zigzag), buttons 26 / 27 / 14
  cfg.vertical = false;
  cfg.serpentine = true;
  cfg.btnPins[0] = 26;
  cfg.btnPins[1] = 27;
  cfg.btnPins[2] = 14;
#endif
#ifdef HW_ULANZI
  // Ulanzi TC001 (AWTRIX hardware): row-wise zigzag matrix, LDR and 3 buttons
  cfg.vertical = false;
  cfg.serpentine = true;
  cfg.ldrPin = 35;
  cfg.autoBright = true;
  cfg.btnPins[0] = 26;
  cfg.btnPins[1] = 27;
  cfg.btnPins[2] = 14;
#endif
  // default pages
  cfg.pageCount = 2;
  cfg.pages[0] = PageCfg();
  cfg.pages[0].name = "Hodiny";
  cfg.pages[0].type = PT_CLOCK;
  cfg.pages[0].style = 1;
  cfg.pages[0].duration = 10;
  cfg.pages[1] = PageCfg();
  cfg.pages[1].name = "Datum";
  cfg.pages[1].type = PT_DATE;
  cfg.pages[1].duration = 5;
}

// ---------------------------------------------------------------- config JSON
void configToJson(JsonObject o, bool s) {
  JsonObject n = o["net"].to<JsonObject>();
  n["hostname"] = cfg.hostname;
  n["ssid"] = cfg.wifiSsid;
  n["pass"] = secret(cfg.wifiPass, s);
  n["static"] = cfg.staticIp;
  n["ip"] = cfg.ip; n["gateway"] = cfg.gateway; n["subnet"] = cfg.subnet; n["dns"] = cfg.dns;
  n["ap_pass"] = secret(cfg.apPass, s);
  n["web_user"] = cfg.webUser;
  n["web_pass"] = secret(cfg.webPass, s);

  JsonObject h = o["ha"].to<JsonObject>();
  h["enabled"] = cfg.haEnabled;
  h["url"] = cfg.haUrl;
  h["token"] = secret(cfg.haToken, s);

  JsonObject m = o["mqtt"].to<JsonObject>();
  m["enabled"] = cfg.mqttEnabled;
  m["host"] = cfg.mqttHost; m["port"] = cfg.mqttPort;
  m["user"] = cfg.mqttUser; m["pass"] = secret(cfg.mqttPass, s);
  m["topic"] = cfg.mqttTopic;
  m["discovery"] = cfg.discovery; m["prefix"] = cfg.discoveryPrefix;
  m["screen"] = cfg.screenInterval;

  JsonObject x = o["matrix"].to<JsonObject>();
  x["pin"] = cfg.ledPin; x["width"] = cfg.width; x["height"] = cfg.height;
  x["vertical"] = cfg.vertical; x["serpentine"] = cfg.serpentine;
  x["start_right"] = cfg.startRight; x["start_bottom"] = cfg.startBottom; x["tiled"] = cfg.tiled;
  x["order"] = cfg.colorOrder; x["max_current"] = cfg.maxCurrent; x["gamma"] = cfg.gamma;

  JsonObject d = o["display"].to<JsonObject>();
  d["power"] = cfg.power; d["brightness"] = cfg.brightness;
  d["auto_bright"] = cfg.autoBright; d["ldr_pin"] = cfg.ldrPin; d["ldr_invert"] = cfg.ldrInvert;
  d["min_bright"] = cfg.minBright; d["max_bright"] = cfg.maxBright;
  d["text_color"] = colorToHex(cfg.textColor);
  d["scroll_speed"] = cfg.scrollSpeed; d["transition"] = cfg.transition; d["transition_ms"] = cfg.transitionMs;
  d["text_y"] = cfg.textY; d["auto_rotate"] = cfg.autoRotate; d["uppercase"] = cfg.uppercase;
  d["app_time"] = cfg.appTime;

  JsonObject t = o["time"].to<JsonObject>();
  t["ntp"] = cfg.ntp; t["tz"] = cfg.tz; t["h24"] = cfg.h24; t["seconds"] = cfg.showSeconds;
  t["blink"] = cfg.blinkColon; t["weekday_bar"] = cfg.weekdayBar; t["monday_first"] = cfg.mondayFirst;
  t["date_format"] = cfg.dateFormat;
  t["weekday_color"] = colorToHex(cfg.weekdayColor); t["weekday_active"] = colorToHex(cfg.weekdayActive);

  JsonObject g = o["night"].to<JsonObject>();
  g["enabled"] = cfg.nightEnabled; g["start"] = cfg.nightStart; g["end"] = cfg.nightEnd;
  g["brightness"] = cfg.nightBright; g["clock_only"] = cfg.nightClockOnly;
  g["color"] = colorToHex(cfg.nightColor);

  JsonObject b = o["buttons"].to<JsonObject>();
  JsonArray bp = b["pins"].to<JsonArray>();
  for (int i = 0; i < NUM_BUTTONS; i++) bp.add(cfg.btnPins[i]);
  b["active_low"] = cfg.btnActiveLow;

  JsonArray ind = o["indicators"].to<JsonArray>();
  for (int i = 0; i < NUM_INDICATORS; i++) ind.add(cfg.indTpl[i]);
}

uint32_t configFromJson(JsonObjectConst o) {
  uint32_t c = 0;
  const uint32_t R = CFG_CHG_REBOOT;
  JsonObjectConst n = o["net"];
  if (!n.isNull()) {
    updStr(cfg.hostname, n["hostname"], c, R);
    updStr(cfg.wifiSsid, n["ssid"], c, R);
    updStr(cfg.wifiPass, n["pass"], c, R, true);
    upd(cfg.staticIp, n["static"], c, R);
    updStr(cfg.ip, n["ip"], c, R); updStr(cfg.gateway, n["gateway"], c, R);
    updStr(cfg.subnet, n["subnet"], c, R); updStr(cfg.dns, n["dns"], c, R);
    updStr(cfg.apPass, n["ap_pass"], c, R, true);
    updStr(cfg.webUser, n["web_user"], c, 0);
    updStr(cfg.webPass, n["web_pass"], c, 0, true);
    if (cfg.apPass.length() > 0 && cfg.apPass.length() < 8) cfg.apPass = "hapanel1";
  }
  JsonObjectConst h = o["ha"];
  if (!h.isNull()) {
    upd(cfg.haEnabled, h["enabled"], c, CFG_CHG_HA);
    updStr(cfg.haUrl, h["url"], c, CFG_CHG_HA);
    updStr(cfg.haToken, h["token"], c, CFG_CHG_HA, true);
    while (cfg.haUrl.endsWith("/")) cfg.haUrl.remove(cfg.haUrl.length() - 1);
  }
  JsonObjectConst m = o["mqtt"];
  if (!m.isNull()) {
    upd(cfg.mqttEnabled, m["enabled"], c, CFG_CHG_MQTT);
    updStr(cfg.mqttHost, m["host"], c, CFG_CHG_MQTT);
    upd(cfg.mqttPort, m["port"], c, CFG_CHG_MQTT);
    updStr(cfg.mqttUser, m["user"], c, CFG_CHG_MQTT);
    updStr(cfg.mqttPass, m["pass"], c, CFG_CHG_MQTT, true);
    updStr(cfg.mqttTopic, m["topic"], c, CFG_CHG_MQTT);
    upd(cfg.discovery, m["discovery"], c, CFG_CHG_MQTT);
    updStr(cfg.discoveryPrefix, m["prefix"], c, CFG_CHG_MQTT);
    upd(cfg.screenInterval, m["screen"], c, 0);
    while (cfg.mqttTopic.endsWith("/")) cfg.mqttTopic.remove(cfg.mqttTopic.length() - 1);
    if (cfg.mqttTopic.isEmpty()) cfg.mqttTopic = "awtrix_" + deviceId;
  }
  JsonObjectConst x = o["matrix"];
  if (!x.isNull()) {
    upd(cfg.ledPin, x["pin"], c, R);
    upd(cfg.width, x["width"], c, R);
    upd(cfg.height, x["height"], c, R);
    upd(cfg.vertical, x["vertical"], c, CFG_CHG_DISPLAY);
    upd(cfg.serpentine, x["serpentine"], c, CFG_CHG_DISPLAY);
    upd(cfg.startRight, x["start_right"], c, CFG_CHG_DISPLAY);
    upd(cfg.startBottom, x["start_bottom"], c, CFG_CHG_DISPLAY);
    upd(cfg.tiled, x["tiled"], c, CFG_CHG_DISPLAY);
    updStr(cfg.colorOrder, x["order"], c, R);
    upd(cfg.maxCurrent, x["max_current"], c, CFG_CHG_DISPLAY);
    upd(cfg.gamma, x["gamma"], c, CFG_CHG_DISPLAY);
    cfg.width = constrain(cfg.width, 8, 128);
    cfg.height = constrain(cfg.height, 8, 32);
    while ((int)cfg.width * cfg.height > MAX_LEDS) cfg.width /= 2;
  }
  JsonObjectConst d = o["display"];
  if (!d.isNull()) {
    const uint32_t D = CFG_CHG_DISPLAY;
    upd(cfg.power, d["power"], c, D);
    upd(cfg.brightness, d["brightness"], c, D);
    upd(cfg.autoBright, d["auto_bright"], c, D);
    upd(cfg.ldrPin, d["ldr_pin"], c, R);
    upd(cfg.ldrInvert, d["ldr_invert"], c, D);
    upd(cfg.minBright, d["min_bright"], c, D);
    upd(cfg.maxBright, d["max_bright"], c, D);
    updColor(cfg.textColor, d["text_color"], c, D);
    if (cfg.textColor < 0) cfg.textColor = 0xFFFFFF;
    upd(cfg.scrollSpeed, d["scroll_speed"], c, D);
    upd(cfg.transition, d["transition"], c, D);
    upd(cfg.transitionMs, d["transition_ms"], c, D);
    upd(cfg.textY, d["text_y"], c, D);
    upd(cfg.autoRotate, d["auto_rotate"], c, D);
    upd(cfg.uppercase, d["uppercase"], c, D);
    upd(cfg.appTime, d["app_time"], c, D);
    cfg.appTime = constrain(cfg.appTime, 1, 3600);
    cfg.scrollSpeed = constrain(cfg.scrollSpeed, 5, 120);
  }
  JsonObjectConst t = o["time"];
  if (!t.isNull()) {
    updStr(cfg.ntp, t["ntp"], c, CFG_CHG_TIME);
    updStr(cfg.tz, t["tz"], c, CFG_CHG_TIME);
    upd(cfg.h24, t["h24"], c, CFG_CHG_DISPLAY);
    upd(cfg.showSeconds, t["seconds"], c, CFG_CHG_DISPLAY);
    upd(cfg.blinkColon, t["blink"], c, CFG_CHG_DISPLAY);
    upd(cfg.weekdayBar, t["weekday_bar"], c, CFG_CHG_DISPLAY);
    upd(cfg.mondayFirst, t["monday_first"], c, CFG_CHG_DISPLAY);
    upd(cfg.dateFormat, t["date_format"], c, CFG_CHG_DISPLAY);
    updColor(cfg.weekdayColor, t["weekday_color"], c, CFG_CHG_DISPLAY);
    updColor(cfg.weekdayActive, t["weekday_active"], c, CFG_CHG_DISPLAY);
  }
  JsonObjectConst g = o["night"];
  if (!g.isNull()) {
    upd(cfg.nightEnabled, g["enabled"], c, CFG_CHG_DISPLAY);
    updStr(cfg.nightStart, g["start"], c, CFG_CHG_DISPLAY);
    updStr(cfg.nightEnd, g["end"], c, CFG_CHG_DISPLAY);
    upd(cfg.nightBright, g["brightness"], c, CFG_CHG_DISPLAY);
    upd(cfg.nightClockOnly, g["clock_only"], c, CFG_CHG_DISPLAY);
    updColor(cfg.nightColor, g["color"], c, CFG_CHG_DISPLAY);
  }
  JsonObjectConst b = o["buttons"];
  if (!b.isNull()) {
    JsonArrayConst bp = b["pins"];
    for (int i = 0; i < NUM_BUTTONS && i < (int)bp.size(); i++) upd(cfg.btnPins[i], bp[i], c, R);
    upd(cfg.btnActiveLow, b["active_low"], c, R);
  }
  JsonArrayConst ind = o["indicators"];
  if (!ind.isNull()) {
    for (int i = 0; i < NUM_INDICATORS && i < (int)ind.size(); i++) updStr(cfg.indTpl[i], ind[i], c, CFG_CHG_HA);
  }
  return c;
}

// ---------------------------------------------------------------- pages JSON
void pageToJson(const PageCfg& p, JsonObject o) {
  o["name"] = p.name;
  o["type"] = PAGE_TYPE_NAMES[p.type < PT_COUNT ? p.type : 0];
  o["enabled"] = p.enabled;
  o["icon"] = p.icon;
  o["entity"] = p.entity;
  o["decimals"] = p.decimals;
  o["unit"] = p.unit;
  o["text"] = p.text;
  o["color_tpl"] = p.colorTpl;
  o["icon_tpl"] = p.iconTpl;
  o["visible_tpl"] = p.visibleTpl;
  o["progress_tpl"] = p.progressTpl;
  o["color"] = colorToHex(p.color);
  o["progress_color"] = colorToHex(p.progressColor);
  o["duration"] = p.duration;
  o["effect"] = p.effect;
  o["rainbow"] = p.rainbow;
  o["style"] = p.style;
  o["action"] = p.action;
  o["action_entity"] = p.actionEntity;
  o["action_service"] = p.actionService;
}

void pageFromJson(PageCfg& p, JsonObjectConst o) {
  p = PageCfg();
  p.name = o["name"] | "";
  String t = o["type"] | "clock";
  for (uint8_t i = 0; i < PT_COUNT; i++) if (t == PAGE_TYPE_NAMES[i]) p.type = i;
  p.enabled = o["enabled"] | true;
  p.icon = o["icon"] | "";
  p.entity = o["entity"] | "";
  p.entity.trim();
  p.decimals = o["decimals"] | -1;
  p.unit = o["unit"] | "";
  p.text = o["text"] | "";
  p.colorTpl = o["color_tpl"] | "";
  p.iconTpl = o["icon_tpl"] | "";
  p.visibleTpl = o["visible_tpl"] | "";
  p.progressTpl = o["progress_tpl"] | "";
  p.color = parseColor(o["color"] | "", -1);
  p.progressColor = parseColor(o["progress_color"] | "", -1);
  p.duration = constrain((int)(o["duration"] | 8), 1, 3600);
  p.effect = o["effect"] | "";
  p.rainbow = o["rainbow"] | false;
  p.style = o["style"] | 0;
  p.action = o["action"] | "";
  p.actionEntity = o["action_entity"] | "";
  p.actionService = o["action_service"] | "";
  if (p.name.isEmpty()) p.name = PAGE_TYPE_NAMES[p.type];
}

void pagesToJson(JsonArray a) {
  for (int i = 0; i < cfg.pageCount; i++) pageToJson(cfg.pages[i], a.add<JsonObject>());
}

void pagesFromJson(JsonArrayConst a) {
  uint8_t n = 0;
  for (JsonObjectConst o : a) {
    if (n >= MAX_PAGES) break;
    pageFromJson(cfg.pages[n++], o);
  }
  for (uint8_t i = n; i < MAX_PAGES; i++) cfg.pages[i] = PageCfg();
  cfg.pageCount = n;
}

// ---------------------------------------------------------------- files
static bool writeJsonFile(const char* path, JsonDocument& doc) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

bool configLoad() {
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) { Serial.printf("[cfg] parse error: %s\n", e.c_str()); return false; }
  configFromJson(doc.as<JsonObjectConst>());
  return true;
}

bool configSave() {
  JsonDocument doc;
  { Lock l; configToJson(doc.to<JsonObject>(), true); }
  bool ok = writeJsonFile(CONFIG_FILE, doc);
  Serial.printf("[cfg] saved config: %d\n", ok);
  return ok;
}

bool pagesLoad() {
  File f = LittleFS.open(PAGES_FILE, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e || !doc.is<JsonArray>()) return false;
  pagesFromJson(doc.as<JsonArrayConst>());
  return true;
}

bool pagesSave() {
  JsonDocument doc;
  { Lock l; pagesToJson(doc.to<JsonArray>()); }
  bool ok = writeJsonFile(PAGES_FILE, doc);
  Serial.printf("[cfg] saved pages: %d\n", ok);
  return ok;
}
