#include "awtrix.h"
#include <WiFi.h>
#include <memory>
#include "apps.h"
#include "config.h"
#include "display.h"
#include "hw.h"

namespace Awtrix {

static const char* const TRANSITIONS[] = {"None", "Slide", "SlideUp", "Fade"};

// AWTRIX TEFF (0-10) -> own transitions (0 none, 1 slide, 2 slide up, 3 fade)
static uint8_t mapTeff(int t) {
  switch (t) {
    case 2: case 8: case 10: return 3;  // dim / blink / fade
    case 9: return 2;                   // reload
    default: return 1;                  // random, slide, zoom, ...
  }
}

static int32_t colorOf(JsonVariantConst v) {
  if (v.is<JsonArrayConst>() && v.size() >= 3)
    return (constrain(v[0].as<int>(), 0, 255) << 16) | (constrain(v[1].as<int>(), 0, 255) << 8) |
           constrain(v[2].as<int>(), 0, 255);
  if (v.is<int>()) return v.as<int>();
  return parseColor(v.as<String>(), -1);
}

void stats(JsonObject o) {
  o["bat"] = 100;
  o["bat_raw"] = 0;
  o["type"] = 0;
  o["lux"] = Apps::ldrBrightness * 100 / 255;
  o["ldr_raw"] = Hw::ldrRaw();
  o["ram"] = ESP.getFreeHeap();
  o["bri"] = cfg.brightness;
  o["uptime"] = millis() / 1000;
  o["wifi_signal"] = WiFi.RSSI();
  o["messages"] = Apps::queueLength();
  o["version"] = FW_VERSION;
  for (int i = 0; i < NUM_INDICATORS; i++) o["indicator" + String(i + 1)] = Apps::indicatorOn(i);
  o["app"] = Apps::currentName();
  o["uid"] = "awtrix_" + deviceId;
  o["matrix"] = cfg.power;
  o["ip_address"] = WiFi.localIP().toString();
}

void settings(JsonObject o) {
  o["ATIME"] = cfg.appTime;
  o["TEFF"] = cfg.transition == 3 ? 10 : cfg.transition == 0 ? 1 : 1;
  o["TSPEED"] = cfg.transitionMs;
  o["TCOL"] = colorToHex(cfg.textColor);
  o["WD"] = cfg.weekdayBar;
  o["WDCA"] = colorToHex(cfg.weekdayActive);
  o["WDCI"] = colorToHex(cfg.weekdayColor);
  o["BRI"] = cfg.brightness;
  o["ABRI"] = cfg.autoBright;
  o["ATRANS"] = cfg.autoRotate;
  o["UPPERCASE"] = cfg.uppercase;
  o["SOM"] = cfg.mondayFirst;
  o["SSPEED"] = cfg.scrollSpeed * 100 / 25;
  o["MATP"] = cfg.power;
  o["TFORMAT"] = cfg.h24 ? (cfg.blinkColon ? "%H %M" : "%H:%M") : (cfg.blinkColon ? "%l %M" : "%l:%M");
}

void applySettings(JsonVariantConst v) {
  if (!v.is<JsonObjectConst>()) return;
  {
    Lock l;
    if (v["ATIME"].is<int>()) cfg.appTime = constrain(v["ATIME"].as<int>(), 1, 3600);
    if (v["TEFF"].is<int>()) cfg.transition = mapTeff(v["TEFF"].as<int>());
    if (v["TSPEED"].is<int>()) cfg.transitionMs = constrain(v["TSPEED"].as<int>(), 50, 3000);
    if (!v["TCOL"].isNull()) { int32_t c = colorOf(v["TCOL"]); if (c >= 0) cfg.textColor = c; }
    if (v["WD"].is<bool>()) cfg.weekdayBar = v["WD"];
    if (!v["WDCA"].isNull()) { int32_t c = colorOf(v["WDCA"]); if (c >= 0) cfg.weekdayActive = c; }
    if (!v["WDCI"].isNull()) { int32_t c = colorOf(v["WDCI"]); if (c >= 0) cfg.weekdayColor = c; }
    if (v["BRI"].is<int>()) cfg.brightness = constrain(v["BRI"].as<int>(), 1, 255);
    if (v["ABRI"].is<bool>()) cfg.autoBright = v["ABRI"];
    if (v["ATRANS"].is<bool>()) cfg.autoRotate = v["ATRANS"];
    if (v["UPPERCASE"].is<bool>()) cfg.uppercase = v["UPPERCASE"];
    if (v["SOM"].is<bool>()) cfg.mondayFirst = v["SOM"];
    if (v["SSPEED"].is<int>()) cfg.scrollSpeed = constrain(v["SSPEED"].as<int>() * 25 / 100, 5, 120);
    if (v["MATP"].is<bool>()) cfg.power = v["MATP"];
    if (v["TFORMAT"].is<const char*>()) {
      String f = v["TFORMAT"].as<const char*>();
      cfg.h24 = f.indexOf("%H") >= 0;
      cfg.showSeconds = f.indexOf("%S") >= 0;
      cfg.blinkColon = f.indexOf(':') < 0;
    }
  }
  reqSaveConfig = true;
  reqMqttState = true;
}

void screen(JsonArray a) {
  size_t n = cfg.width * cfg.height;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[n * 3]);
  uint32_t fn;
  size_t len = Apps::copyFrame(buf.get(), n * 3, fn);
  for (size_t i = 0; i + 2 < len; i += 3) a.add(((uint32_t)buf[i] << 16) | (buf[i + 1] << 8) | buf[i + 2]);
}

void transitions(JsonArray a) {
  for (auto t : TRANSITIONS) a.add(t);
}

bool power(JsonVariantConst v) {
  bool p = cfg.power;
  if (v.is<bool>()) p = v.as<bool>();
  else if (v["power"].is<bool>()) p = v["power"].as<bool>();
  else if (v.is<int>()) p = v.as<int>() != 0;
  else if (v.is<const char*>()) {
    String s = v.as<const char*>();
    s.trim();
    if (s.equalsIgnoreCase("on") || s == "1" || s.equalsIgnoreCase("true")) p = true;
    else if (s.equalsIgnoreCase("off") || s == "0" || s.equalsIgnoreCase("false")) p = false;
    else if (s.equalsIgnoreCase("toggle")) p = !p;
  }
  {
    Lock l;
    cfg.power = p;
  }
  reqSaveConfig = true;
  reqMqttState = true;
  return p;
}

bool command(const String& cmd, JsonVariantConst v) {
  if (cmd == "notify") Apps::notify(v);
  else if (cmd == "notify/dismiss" || cmd == "dismiss") Apps::dismiss();
  else if (cmd.startsWith("custom/")) Apps::setCustom(cmd.substring(7), v);
  else if (cmd.startsWith("indicator") && cmd.length() == 10) Apps::setIndicator(cmd.substring(9).toInt() - 1, v);
  else if (cmd == "power") power(v);
  else if (cmd == "sleep") {
    JsonDocument off;
    off.set(false);
    power(off.as<JsonVariantConst>());
  }
  else if (cmd == "moodlight") Apps::setMoodlight(v);
  else if (cmd == "switch") Apps::switchApp(v.is<const char*>() ? String(v.as<const char*>()) : String(v["name"] | ""));
  else if (cmd == "nextapp") Apps::next();
  else if (cmd == "previousapp") Apps::prev();
  else if (cmd == "settings") applySettings(v);
  else if (cmd == "reboot") reqReboot = true;
  else if (cmd == "sound" || cmd == "rtttl" || cmd == "doupdate") { /* no buzzer / no online update */ }
  else return false;
  if (cmd == "nextapp" || cmd == "previousapp" || cmd == "switch") reqMqttState = true;
  return true;
}

}  // namespace Awtrix
