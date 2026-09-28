#include "mqtt.h"
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <memory>
#include <vector>
#include "apps.h"
#include "awtrix.h"
#include "config.h"
#include "display.h"
#include "effects.h"
#include "icons.h"
#include "png.h"

namespace Mqtt {

static WiFiClient net;
static PubSubClient mq(net);
static uint32_t lastTry = 0, lastSensors = 0, lastScreen = 0;
static String lastError;
static String resolvedHost;
static String lastApp;
static String lightEffect = "none";
static std::vector<uint8_t> lastScreenBuf;

static const char* const TRANSITION_NAMES[] = {"Žádný", "Posun", "Posun nahoru", "Prolínání"};

static String base() { return cfg.mqttTopic; }
static String t(const String& suffix) { return base() + "/" + suffix; }

static void pub(const String& topic, const String& payload, bool retain = true) {
  mq.publish(topic.c_str(), payload.c_str(), retain);
}

static void pubJson(const String& topic, JsonDocument& doc, bool retain = true) {
  String out;
  serializeJson(doc, out);
  if (!mq.publish(topic.c_str(), (const uint8_t*)out.c_str(), out.length(), retain))
    Serial.printf("[mqtt] publish failed %s (%u B)\n", topic.c_str(), out.length());
}

static String rgbHex(uint32_t c) {
  char b[8];
  snprintf(b, sizeof(b), "#%06X", (unsigned)(c & 0xFFFFFF));
  return b;
}

// ---------------------------------------------------------------- discovery helpers
static void addDevice(JsonDocument& d) {
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add("hapanel_" + deviceId);
  dev["name"] = cfg.hostname;
  dev["mf"] = "HA-Panel";
  dev["mdl"] = String("LED matrix ") + cfg.width + "x" + cfg.height;
  dev["sw"] = FW_VERSION;
  dev["cu"] = "http://" + WiFi.localIP().toString();
  d["avty_t"] = t("status");
}

static void disc(const char* component, const char* objId, JsonDocument& d) {
  d["uniq_id"] = "hapanel_" + deviceId + "_" + objId;
  addDevice(d);
  String topic = cfg.discoveryPrefix + "/" + component + "/hapanel_" + deviceId + "/" + objId + "/config";
  pubJson(topic, d, true);
}

static void discRemove(const char* component, const char* objId) {
  String topic = cfg.discoveryPrefix + "/" + component + "/hapanel_" + deviceId + "/" + objId + "/config";
  mq.publish(topic.c_str(), "", true);
}

static void jsonLight(const char* id, const char* name, const char* icon, const char* cmd, const char* state,
                      bool effects, const char* const* fxList, int fxCount, bool config = false) {
  JsonDocument d;
  d["name"] = name;
  d["schema"] = "json";
  d["cmd_t"] = t(cmd);
  d["stat_t"] = t(state);
  d["brightness"] = true;
  d["brightness_scale"] = 255;
  d["supported_color_modes"].to<JsonArray>().add("rgb");
  if (effects) {
    d["effect"] = true;
    JsonArray fx = d["effect_list"].to<JsonArray>();
    if (fxList) for (int i = 0; i < fxCount; i++) fx.add(fxList[i]);
    else {
      fx.add("none");
      Effects::list(fx);
    }
  }
  d["icon"] = icon;
  if (config) d["ent_cat"] = "config";
  disc("light", id, d);
}

static void number(const char* id, const char* name, const char* icon, const char* key, int mn, int mx, int step,
                   const char* unit) {
  JsonDocument d;
  d["name"] = name;
  d["cmd_t"] = t(String(key) + "/set");
  d["stat_t"] = t("settings/state");
  d["val_tpl"] = String("{{ value_json.") + key + " }}";
  d["min"] = mn;
  d["max"] = mx;
  d["step"] = step;
  if (unit) d["unit_of_meas"] = unit;
  d["icon"] = icon;
  d["ent_cat"] = "config";
  disc("number", id, d);
}

static void sw(const char* id, const char* name, const char* icon, bool settingsTopic) {
  JsonDocument d;
  d["name"] = name;
  d["cmd_t"] = t(String(id) + "/set");
  if (settingsTopic) {
    d["stat_t"] = t("settings/state");
    d["val_tpl"] = String("{{ value_json.") + id + " }}";
  } else {
    d["stat_t"] = t(String(id) + "/state");
  }
  d["icon"] = icon;
  d["ent_cat"] = "config";
  disc("switch", id, d);
}

// ---------------------------------------------------------------- discovery
void publishDiscovery() {
  if (!mq.connected() || !cfg.discovery) return;

  // main display light: on/off, brightness, default text color, effects
  jsonLight("display", "Displej", "mdi:dots-grid", "light/set", "light/state", true, nullptr, 0);
  // mood light (whole matrix one color)
  jsonLight("mood", "Nálada", "mdi:lava-lamp", "moodlight/set", "moodlight/state", false, nullptr, 0);
  // indicators
  static const char* const IND_FX[] = {"none", "blink", "fade"};
  for (int i = 1; i <= NUM_INDICATORS; i++) {
    String id = "indicator" + String(i);
    String name = "Indikátor " + String(i);
    jsonLight(id.c_str(), name.c_str(), "mdi:circle-small", (id + "/set").c_str(), (id + "/state").c_str(), true, IND_FX,
              3);
  }

  {  // page select
    JsonDocument d;
    d["name"] = "Stránka";
    d["cmd_t"] = t("page/set");
    d["stat_t"] = t("page/state");
    d["icon"] = "mdi:book-open-page-variant";
    JsonArray opts = d["options"].to<JsonArray>();
    int n = 0;
    {
      Lock l;
      for (int i = 0; i < cfg.pageCount; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) if (cfg.pages[j].name == cfg.pages[i].name) dup = true;
        if (!dup && cfg.pages[i].name.length()) { opts.add(cfg.pages[i].name); n++; }
      }
    }
    if (n) disc("select", "page", d);
    else discRemove("select", "page");
  }
  {  // transition select
    JsonDocument d;
    d["name"] = "Přechod";
    d["cmd_t"] = t("transition/set");
    d["stat_t"] = t("settings/state");
    d["val_tpl"] = "{{ value_json.transition }}";
    JsonArray opts = d["options"].to<JsonArray>();
    for (auto n : TRANSITION_NAMES) opts.add(n);
    d["icon"] = "mdi:transition";
    d["ent_cat"] = "config";
    disc("select", "transition", d);
  }

  sw("autorotate", "Automatické přepínání", "mdi:autorenew", false);
  sw("autobright", "Automatický jas", "mdi:brightness-auto", false);
  sw("night", "Noční režim", "mdi:weather-night", false);
  sw("uppercase", "Velká písmena", "mdi:format-letter-case-upper", true);
  sw("weekday", "Pruh dní v týdnu", "mdi:calendar-week", true);
  sw("screen", "Obraz do Home Assistantu", "mdi:monitor-screenshot", true);

  {
    JsonDocument d;
    d["name"] = "Jas";
    d["cmd_t"] = t("brightness/set");
    d["stat_t"] = t("light/state");
    d["val_tpl"] = "{{ value_json.brightness }}";
    d["min"] = 1;
    d["max"] = 255;
    d["icon"] = "mdi:brightness-6";
    d["ent_cat"] = "config";
    disc("number", "brightness", d);
  }
  number("scrollspeed", "Rychlost posunu textu", "mdi:speedometer", "scroll", 5, 120, 1, "px/s");
  number("apptime", "Doba zobrazení aplikace", "mdi:timer-outline", "apptime", 1, 300, 1, "s");
  number("transms", "Délka přechodu", "mdi:timer-sand", "transms", 50, 3000, 50, "ms");

  struct Btn { const char* id; const char* name; const char* icon; const char* payload; };
  const Btn buttons[] = {
    {"next", "Další stránka", "mdi:arrow-right-bold", "next"},
    {"prev", "Předchozí stránka", "mdi:arrow-left-bold", "prev"},
    {"select", "Akce stránky", "mdi:gesture-tap", "select"},
    {"dismiss", "Zavřít notifikaci", "mdi:bell-cancel", "dismiss"},
    {"test", "Testovací obrazec", "mdi:grid", "test"},
    {"restart", "Restart", "mdi:restart", "restart"},
  };
  for (auto& b : buttons) {
    JsonDocument d;
    d["name"] = b.name;
    d["cmd_t"] = t("cmd");
    d["pl_prs"] = b.payload;
    d["icon"] = b.icon;
    if (!strcmp(b.id, "restart")) { d["dev_cla"] = "restart"; d["ent_cat"] = "config"; }
    if (!strcmp(b.id, "test")) d["ent_cat"] = "diagnostic";
    disc("button", b.id, d);
  }
  {
    JsonDocument d;
    d["name"] = "Notifikace";
    d["cmd_t"] = t("notify");
    d["icon"] = "mdi:message-text";
    disc("notify", "notify", d);
  }
  {  // quick message text entity
    JsonDocument d;
    d["name"] = "Zpráva";
    d["cmd_t"] = t("message/set");
    d["max"] = 255;
    d["icon"] = "mdi:message-draw";
    disc("text", "message", d);
  }
  {  // current app + info attributes (used by the Lovelace card)
    JsonDocument d;
    d["name"] = "Aplikace";
    d["stat_t"] = t("stats/currentApp");
    d["json_attr_t"] = t("info");
    d["icon"] = "mdi:application-outline";
    disc("sensor", "app", d);
  }
  {  // page configuration (attributes) - used by the Lovelace card to list and edit pages
    JsonDocument d;
    d["name"] = "Stránky";
    d["stat_t"] = t("pages/attr");
    d["val_tpl"] = "{{ value_json.count }}";
    d["json_attr_t"] = t("pages/attr");
    d["icon"] = "mdi:view-carousel";
    disc("sensor", "pages", d);
  }
  {  // live screen image
    JsonDocument d;
    d["name"] = "Obrazovka";
    d["image_topic"] = t("screen/png");
    d["content_type"] = "image/png";
    d["icon"] = "mdi:monitor";
    disc("image", "screen", d);
  }
  // diagnostic sensors
  struct Sn { const char* id; const char* name; const char* tpl; const char* unit; const char* cls; };
  const Sn sensors[] = {
    {"rssi", "WiFi signál", "{{ value_json.rssi }}", "dBm", "signal_strength"},
    {"uptime", "Doba běhu", "{{ value_json.uptime }}", "s", "duration"},
    {"current", "Odhad proudu", "{{ value_json.current }}", "mA", "current"},
  };
  for (auto& s : sensors) {
    JsonDocument d;
    d["name"] = s.name;
    d["stat_t"] = t("sensor");
    d["val_tpl"] = s.tpl;
    d["unit_of_meas"] = s.unit;
    d["dev_cla"] = s.cls;
    d["stat_cla"] = "measurement";
    d["ent_cat"] = "diagnostic";
    disc("sensor", s.id, d);
  }
  if (cfg.ldrPin >= 0) {
    JsonDocument d;
    d["name"] = "Okolní světlo";
    d["stat_t"] = t("sensor");
    d["val_tpl"] = "{{ value_json.light }}";
    d["unit_of_meas"] = "%";
    d["icon"] = "mdi:brightness-5";
    d["stat_cla"] = "measurement";
    disc("sensor", "light", d);
  } else {
    discRemove("sensor", "light");
  }
  const char* btnNames[] = {"Tlačítko vlevo", "Tlačítko střed", "Tlačítko vpravo"};
  for (int i = 0; i < NUM_BUTTONS; i++) {
    char id[12];
    snprintf(id, sizeof(id), "button%d", i + 1);
    if (cfg.btnPins[i] < 0) { discRemove("binary_sensor", id); continue; }
    JsonDocument d;
    d["name"] = btnNames[i];
    d["stat_t"] = t(String(id) + "/state");
    d["icon"] = "mdi:gesture-tap-button";
    disc("binary_sensor", id, d);
  }
}

// ---------------------------------------------------------------- state
static bool isPageName(const String& n) {
  Lock l;
  for (int i = 0; i < cfg.pageCount; i++) if (cfg.pages[i].name == n) return true;
  return false;
}

static void publishInfo() {
  JsonDocument d;
  d["topic"] = cfg.mqttTopic;
  d["ip"] = WiFi.localIP().toString();
  d["hostname"] = cfg.hostname;
  d["version"] = FW_VERSION;
  d["width"] = cfg.width;
  d["height"] = cfg.height;
  {
    Lock l;
    JsonArray pages = d["pages"].to<JsonArray>();
    for (int i = 0; i < cfg.pageCount; i++) pages.add(cfg.pages[i].name);
  }
  Effects::list(d["effects"].to<JsonArray>());
  JsonDocument ic;
  Icons::list(ic.to<JsonArray>());
  JsonArray icons = d["icons"].to<JsonArray>();
  for (JsonObject o : ic.as<JsonArray>()) icons.add(o["name"]);
  pubJson(t("info"), d, true);
}

// full page configuration for the Lovelace card (sensor "Stránky" attributes) + first frame
// of every icon used by the pages so the card can draw them
static void publishPages() {
  JsonDocument d;
  JsonArray pages = d["pages"].to<JsonArray>();
  JsonObject icons = d["icon_data"].to<JsonObject>();
  {
    Lock l;
    pagesToJson(pages);
    d["count"] = cfg.pageCount;
    for (int i = 0; i < cfg.pageCount; i++) {
      const String& n = cfg.pages[i].icon;
      if (n.isEmpty() || icons[n].is<const char*>()) continue;
      JsonDocument ic;
      if (Icons::toJson(n, ic.to<JsonObject>())) icons[n] = ic["frames"][0].as<String>();
    }
  }
  pubJson(t("pages/attr"), d, true);
}

void publishState() {
  if (!mq.connected()) return;
  {
    JsonDocument d;
    d["state"] = cfg.power ? "ON" : "OFF";
    d["brightness"] = cfg.brightness;
    d["color_mode"] = "rgb";
    JsonObject c = d["color"].to<JsonObject>();
    c["r"] = (cfg.textColor >> 16) & 0xFF;
    c["g"] = (cfg.textColor >> 8) & 0xFF;
    c["b"] = cfg.textColor & 0xFF;
    d["effect"] = lightEffect;
    pubJson(t("light/state"), d);
  }
  {
    bool on;
    uint32_t col;
    uint8_t bri;
    Apps::moodState(on, col, bri);
    JsonDocument d;
    d["state"] = on ? "ON" : "OFF";
    d["brightness"] = bri;
    d["color_mode"] = "rgb";
    JsonObject c = d["color"].to<JsonObject>();
    c["r"] = (col >> 16) & 0xFF;
    c["g"] = (col >> 8) & 0xFF;
    c["b"] = col & 0xFF;
    pubJson(t("moodlight/state"), d);
  }
  for (int i = 0; i < NUM_INDICATORS; i++) {
    int32_t col;
    uint16_t blink, fade;
    Apps::indicatorState(i, col, blink, fade);
    JsonDocument d;
    d["state"] = col > 0 ? "ON" : "OFF";
    d["color_mode"] = "rgb";
    uint32_t c = col > 0 ? col : 0xFF0000;
    JsonObject cc = d["color"].to<JsonObject>();
    cc["r"] = (c >> 16) & 0xFF;
    cc["g"] = (c >> 8) & 0xFF;
    cc["b"] = c & 0xFF;
    d["brightness"] = 255;
    d["effect"] = blink ? "blink" : fade ? "fade" : "none";
    pubJson(t("indicator" + String(i + 1) + "/state"), d);
  }
  {
    JsonDocument d;
    d["scroll"] = cfg.scrollSpeed;
    d["apptime"] = cfg.appTime;
    d["transms"] = cfg.transitionMs;
    d["transition"] = TRANSITION_NAMES[cfg.transition < 4 ? cfg.transition : 1];
    d["uppercase"] = cfg.uppercase ? "ON" : "OFF";
    d["weekday"] = cfg.weekdayBar ? "ON" : "OFF";
    d["screen"] = cfg.screenInterval ? "ON" : "OFF";
    pubJson(t("settings/state"), d);
  }
  String app = Apps::currentName();
  lastApp = app;
  pub(t("stats/currentApp"), app);
  if (isPageName(app)) pub(t("page/state"), app);
  pub(t("autorotate/state"), cfg.autoRotate ? "ON" : "OFF");
  pub(t("autobright/state"), cfg.autoBright ? "ON" : "OFF");
  pub(t("night/state"), cfg.nightEnabled ? "ON" : "OFF");
}

static void publishSensors() {
  JsonDocument d;
  d["rssi"] = WiFi.RSSI();
  d["uptime"] = millis() / 1000;
  d["current"] = Display::estimatedCurrent();
  d["brightness"] = Display::currentBrightness();
  d["light"] = Apps::ldrBrightness * 100 / 255;
  d["ip"] = WiFi.localIP().toString();
  d["heap"] = ESP.getFreeHeap();
  pubJson(t("sensor"), d, false);
}

static void publishScreen(bool force) {
  size_t n = (size_t)cfg.width * cfg.height * 3;
  std::vector<uint8_t> buf(n);
  uint32_t fn;
  size_t len = Apps::copyFrame(buf.data(), n, fn);
  if (!force && len == lastScreenBuf.size() && memcmp(buf.data(), lastScreenBuf.data(), len) == 0) return;
  lastScreenBuf = buf;
  std::vector<uint8_t> png;
  encodePng(buf.data(), cfg.width, cfg.height, png);
  String topic = t("screen/png");
  if (!mq.publish(topic.c_str(), png.data(), png.size(), false))
    Serial.printf("[mqtt] screen publish failed (%u B)\n", (unsigned)png.size());
}

void publishButton(int idx, bool pressed) {
  if (!mq.connected()) return;
  pub(t("button" + String(idx + 1) + "/state"), pressed ? "ON" : "OFF", false);
  // AWTRIX style button topics
  static const char* const AW[] = {"stats/buttonLeft", "stats/buttonSelect", "stats/buttonRight"};
  if (idx >= 0 && idx < 3) pub(t(AW[idx]), pressed ? "1" : "0", false);
}

// ---------------------------------------------------------------- commands
static bool onOff(const String& p, bool cur) {
  if (p.equalsIgnoreCase("ON") || p == "1" || p.equalsIgnoreCase("true")) return true;
  if (p.equalsIgnoreCase("OFF") || p == "0" || p.equalsIgnoreCase("false")) return false;
  if (p.equalsIgnoreCase("TOGGLE")) return !cur;
  return cur;
}

// temporary "effect" app activated through the light entity
static void setLightEffect(const String& fx) {
  JsonDocument d;
  lightEffect = (fx.isEmpty() || fx == "none") ? "none" : fx;
  if (lightEffect == "none") {
    Apps::setCustom("_effect", d.as<JsonVariantConst>());
    return;
  }
  d["effect"] = fx;
  d["duration"] = 3600;
  Apps::setCustom("_effect", d.as<JsonVariantConst>());
  Apps::gotoPage("_effect");
}

static int32_t lightColor(JsonVariantConst c, int32_t def) {
  if (!c.is<JsonObjectConst>()) return def;
  return (constrain(c["r"].as<int>(), 0, 255) << 16) | (constrain(c["g"].as<int>(), 0, 255) << 8) |
         constrain(c["b"].as<int>(), 0, 255);
}

static void onMessage(char* topicC, uint8_t* payload, unsigned int len) {
  String topic(topicC);
  String p;
  p.concat((const char*)payload, len);
  String b = base() + "/";
  if (topic == cfg.discoveryPrefix + "/status") {
    if (p == "online") {
      publishDiscovery();
      publishInfo();
      reqMqttState = true;
    }
    return;
  }
  if (!topic.startsWith(b)) return;
  String sub = topic.substring(b.length());

  bool changed = false;
  if (sub == "light/set") {
    JsonDocument d;
    if (deserializeJson(d, p)) return;
    {
      Lock l;
      if (d["state"].is<const char*>()) cfg.power = String(d["state"].as<const char*>()) == "ON";
      if (d["brightness"].is<int>()) {
        cfg.brightness = constrain(d["brightness"].as<int>(), 1, 255);
        cfg.autoBright = false;
      }
      cfg.textColor = lightColor(d["color"], cfg.textColor);
    }
    if (d["effect"].is<const char*>()) setLightEffect(d["effect"].as<const char*>());
    changed = true;
  } else if (sub == "moodlight/set") {
    JsonDocument d;
    if (deserializeJson(d, p)) return;
    bool on;
    uint32_t col;
    uint8_t bri;
    Apps::moodState(on, col, bri);
    JsonDocument m;
    if (String(d["state"] | "ON") == "OFF") {
      Apps::setMoodlight(m.as<JsonVariantConst>());
    } else {
      m["brightness"] = d["brightness"] | (int)bri;
      m["color"] = rgbHex(lightColor(d["color"], col));
      Apps::setMoodlight(m.as<JsonVariantConst>());
    }
    reqMqttState = true;
  } else if (sub.startsWith("indicator") && sub.endsWith("/set")) {
    int idx = sub.substring(9, 10).toInt() - 1;
    JsonDocument d;
    if (deserializeJson(d, p)) return;
    int32_t col;
    uint16_t blink, fade;
    Apps::indicatorState(idx, col, blink, fade);
    JsonDocument v;
    if (String(d["state"] | "ON") == "OFF") {
      Apps::setIndicator(idx, v.as<JsonVariantConst>());
    } else {
      uint32_t c = lightColor(d["color"], col > 0 ? col : 0xFF0000);
      if (d["brightness"].is<int>()) {
        RGB s = RGB(c).scale(constrain(d["brightness"].as<int>(), 1, 255));
        c = ((uint32_t)s.r << 16) | (s.g << 8) | s.b;
      }
      v["color"] = rgbHex(c ? c : 0x010101);
      String fx = d["effect"] | (blink ? "blink" : fade ? "fade" : "none");
      if (fx == "blink") v["blink"] = 500;
      else if (fx == "fade") v["fade"] = 1000;
      Apps::setIndicator(idx, v.as<JsonVariantConst>());
    }
  } else if (sub == "brightness/set") {
    Lock l;
    cfg.brightness = constrain(p.toInt(), 1, 255);
    cfg.autoBright = false;
    changed = true;
  } else if (sub == "page/set") {
    Apps::gotoPage(p);
    reqMqttState = true;
  } else if (sub == "pages/set") {
    // replace the whole page list (sent by the Lovelace card)
    JsonDocument d;
    if (deserializeJson(d, p)) {
      Serial.println("[mqtt] pages/set: invalid JSON");
      return;
    }
    JsonArrayConst arr = d.is<JsonArray>() ? d.as<JsonArrayConst>() : d["pages"].as<JsonArrayConst>();
    if (arr.isNull()) return;
    {
      Lock l;
      pagesFromJson(arr);
      Apps::onPagesChanged();
    }
    reqSavePages = true;
    reqHaResync = true;
    reqMqttRediscover = true;
  } else if (sub == "message/set") {
    JsonDocument d;
    d.set(p);
    Apps::notify(d.as<JsonVariantConst>());
  } else if (sub == "autorotate/set") {
    Lock l;
    cfg.autoRotate = onOff(p, cfg.autoRotate);
    changed = true;
  } else if (sub == "autobright/set") {
    Lock l;
    cfg.autoBright = onOff(p, cfg.autoBright);
    changed = true;
  } else if (sub == "night/set") {
    Lock l;
    cfg.nightEnabled = onOff(p, cfg.nightEnabled);
    changed = true;
  } else if (sub == "uppercase/set") {
    Lock l;
    cfg.uppercase = onOff(p, cfg.uppercase);
    changed = true;
  } else if (sub == "weekday/set") {
    Lock l;
    cfg.weekdayBar = onOff(p, cfg.weekdayBar);
    changed = true;
  } else if (sub == "screen/set") {
    Lock l;
    cfg.screenInterval = onOff(p, cfg.screenInterval != 0) ? 2 : 0;
    changed = true;
  } else if (sub == "scroll/set") {
    Lock l;
    cfg.scrollSpeed = constrain((int)p.toFloat(), 5, 120);
    changed = true;
  } else if (sub == "apptime/set") {
    Lock l;
    cfg.appTime = constrain((int)p.toFloat(), 1, 3600);
    changed = true;
  } else if (sub == "transms/set") {
    Lock l;
    cfg.transitionMs = constrain((int)p.toFloat(), 50, 3000);
    changed = true;
  } else if (sub == "transition/set") {
    Lock l;
    for (int i = 0; i < 4; i++) if (p == TRANSITION_NAMES[i]) cfg.transition = i;
    changed = true;
  } else if (sub == "cmd") {
    if (p == "next") Apps::next();
    else if (p == "prev") Apps::prev();
    else if (p == "dismiss") Apps::dismiss();
    else if (p == "restart") reqReboot = true;
    else if (p == "select") reqSelect = true;
    else if (p == "test") Apps::showTest(10000);
    reqMqttState = true;
  } else if (sub == "sendscreen") {
    JsonDocument d;
    Awtrix::screen(d.to<JsonArray>());
    pubJson(t("screen"), d, false);
  } else {
    // AWTRIX 3 compatible topics: notify, notify/dismiss, custom/<app>, indicator1-3, power, sleep,
    // moodlight, switch, nextapp, previousapp, settings, reboot
    JsonDocument d;
    String q = p;
    q.trim();
    if (q.startsWith("{") || q.startsWith("[")) {
      if (deserializeJson(d, q)) {
        Serial.printf("[mqtt] invalid JSON on %s\n", topic.c_str());
        return;
      }
    } else if (q.length()) {
      d.set(q);
    }
    Awtrix::command(sub, d.as<JsonVariantConst>());
  }
  if (changed) {
    reqSaveConfig = true;
    reqMqttState = true;
  }
}

// ---------------------------------------------------------------- connection
static bool connect() {
  if (resolvedHost.isEmpty()) {
    resolvedHost = cfg.mqttHost;
    if (resolvedHost.endsWith(".local")) {
      IPAddress ip = MDNS.queryHost(resolvedHost.substring(0, resolvedHost.length() - 6), 2500);
      if (ip != IPAddress((uint32_t)0)) resolvedHost = ip.toString();
      else resolvedHost = "";
    }
    if (resolvedHost.isEmpty()) {
      lastError = "cannot resolve host";
      return false;
    }
  }
  mq.setServer(resolvedHost.c_str(), cfg.mqttPort);
  String clientId = "hapanel-" + deviceId;
  String will = t("status");
  bool ok = cfg.mqttUser.length()
                ? mq.connect(clientId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(), will.c_str(), 1, true, "offline")
                : mq.connect(clientId.c_str(), nullptr, nullptr, will.c_str(), 1, true, "offline");
  if (!ok) {
    lastError = "connect failed, rc=" + String(mq.state());
    resolvedHost = "";  // re-resolve next time
    return false;
  }
  lastError = "";
  Serial.println("[mqtt] connected");
  pub(will, "online");
  String b = base();
  const char* subs[] = {"light/set", "moodlight/set", "indicator1/set", "indicator2/set", "indicator3/set",
                        "brightness/set", "page/set", "message/set", "autorotate/set", "autobright/set",
                        "night/set", "uppercase/set", "weekday/set", "screen/set", "scroll/set", "apptime/set",
                        "transms/set", "transition/set", "cmd", "pages/set",
                        // AWTRIX 3 compatible topics
                        "power", "notify", "dismiss", "custom/+", "indicator1", "indicator2", "indicator3",
                        "notify/dismiss", "sleep", "moodlight", "switch", "nextapp", "previousapp", "settings",
                        "reboot", "sendscreen", "sound", "rtttl"};
  for (auto s : subs) mq.subscribe((b + "/" + s).c_str());
  mq.subscribe((cfg.discoveryPrefix + "/status").c_str());
  publishDiscovery();
  publishInfo();
  publishPages();
  publishState();
  publishSensors();
  if (cfg.screenInterval) publishScreen(true);
  return true;
}

void begin() {
  mq.setBufferSize(16384);  // page configuration can be several kB
  mq.setCallback(onMessage);
  mq.setKeepAlive(30);
  mq.setSocketTimeout(5);
}

void reconfigure() {
  if (mq.connected()) {
    pub(t("status"), "offline");
    mq.disconnect();
  }
  resolvedHost = "";
  lastTry = 0;
}

void loop() {
  if (!cfg.mqttEnabled || cfg.mqttHost.isEmpty() || WiFi.status() != WL_CONNECTED) {
    if (mq.connected()) mq.disconnect();
    return;
  }
  if (!mq.connected()) {
    if (lastTry && millis() - lastTry < 10000) return;
    lastTry = millis();
    connect();
    return;
  }
  mq.loop();
  if (reqMqttRediscover) {
    reqMqttRediscover = false;
    publishDiscovery();
    publishInfo();
    publishPages();
  }
  if (reqMqttState) {
    reqMqttState = false;
    publishState();
  }
  uint32_t now = millis();
  if (now - lastSensors > 15000) {
    lastSensors = now;
    publishSensors();
    JsonDocument st;
    Awtrix::stats(st.to<JsonObject>());
    pubJson(t("stats"), st, false);
  }
  if (cfg.screenInterval && now - lastScreen > cfg.screenInterval * 1000UL) {
    lastScreen = now;
    publishScreen(false);
  }
  // app changes
  static uint32_t lastAppCheck = 0;
  if (now - lastAppCheck > 500) {
    lastAppCheck = now;
    String app = Apps::currentName();
    if (app != lastApp) {
      lastApp = app;
      pub(t("stats/currentApp"), app);
      if (isPageName(app)) pub(t("page/state"), app);
    }
  }
}

bool connected() { return mq.connected(); }

void statusJson(JsonObject o) {
  o["enabled"] = cfg.mqttEnabled;
  o["connected"] = mq.connected();
  o["error"] = lastError;
  o["topic"] = cfg.mqttTopic;
}

}  // namespace Mqtt
