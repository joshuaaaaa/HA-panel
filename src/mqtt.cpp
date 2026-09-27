#include "mqtt.h"
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "apps.h"
#include "config.h"
#include "display.h"
#include "effects.h"

namespace Mqtt {

static WiFiClient net;
static PubSubClient mq(net);
static uint32_t lastTry = 0, lastSensors = 0;
static bool configured = false;
static String lastError;
static String resolvedHost;
static String lastPage;

static String base() { return cfg.mqttTopic; }
static String t(const char* suffix) { return base() + "/" + suffix; }

static void pub(const String& topic, const String& payload, bool retain = true) {
  mq.publish(topic.c_str(), payload.c_str(), retain);
}

static void pubJson(const String& topic, JsonDocument& doc, bool retain = true) {
  String out;
  serializeJson(doc, out);
  mq.publish(topic.c_str(), (const uint8_t*)out.c_str(), out.length(), retain);
}

// ---------------------------------------------------------------- discovery
static void addDevice(JsonDocument& d) {
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add("hapanel_" + deviceId);
  dev["name"] = cfg.hostname;
  dev["mf"] = "DIY";
  dev["mdl"] = String("HA-Panel ") + cfg.width + "x" + cfg.height;
  dev["sw"] = FW_VERSION;
  dev["cu"] = "http://" + WiFi.localIP().toString();
  d["avty_t"] = t("status");
}

static void disc(const char* component, const char* objId, JsonDocument& d) {
  String uid = "hapanel_" + deviceId + "_" + objId;
  d["uniq_id"] = uid;
  addDevice(d);
  String topic = cfg.discoveryPrefix + "/" + component + "/hapanel_" + deviceId + "/" + objId + "/config";
  pubJson(topic, d, true);
}

static void discRemove(const char* component, const char* objId) {
  String topic = cfg.discoveryPrefix + "/" + component + "/hapanel_" + deviceId + "/" + objId + "/config";
  mq.publish(topic.c_str(), "", true);
}

void publishDiscovery() {
  if (!mq.connected() || !cfg.discovery) return;
  {
    JsonDocument d;
    d["name"] = "Displej";
    d["schema"] = "json";
    d["cmd_t"] = t("light/set");
    d["stat_t"] = t("light/state");
    d["brightness"] = true;
    d["brightness_scale"] = 255;
    JsonArray modes = d["supported_color_modes"].to<JsonArray>();
    modes.add("rgb");
    d["effect"] = true;
    JsonArray fx = d["effect_list"].to<JsonArray>();
    fx.add("none");
    Effects::list(fx);
    d["icon"] = "mdi:dots-grid";
    disc("light", "display", d);
  }
  {
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
  struct Sw { const char* id; const char* name; const char* icon; };
  const Sw switches[] = {
    {"autorotate", "Automatické přepínání", "mdi:autorenew"},
    {"autobright", "Automatický jas", "mdi:brightness-auto"},
    {"night", "Noční režim", "mdi:weather-night"},
  };
  for (auto& s : switches) {
    JsonDocument d;
    d["name"] = s.name;
    d["cmd_t"] = t((String(s.id) + "/set").c_str());
    d["stat_t"] = t((String(s.id) + "/state").c_str());
    d["icon"] = s.icon;
    d["ent_cat"] = "config";
    disc("switch", s.id, d);
  }
  struct Btn { const char* id; const char* name; const char* icon; const char* payload; };
  const Btn buttons[] = {
    {"next", "Další stránka", "mdi:arrow-right-bold", "next"},
    {"prev", "Předchozí stránka", "mdi:arrow-left-bold", "prev"},
    {"dismiss", "Zavřít notifikaci", "mdi:bell-cancel", "dismiss"},
    {"restart", "Restart", "mdi:restart", "restart"},
  };
  for (auto& b : buttons) {
    JsonDocument d;
    d["name"] = b.name;
    d["cmd_t"] = t("cmd");
    d["pl_prs"] = b.payload;
    d["icon"] = b.icon;
    if (!strcmp(b.id, "restart")) { d["dev_cla"] = "restart"; d["ent_cat"] = "config"; }
    disc("button", b.id, d);
  }
  {
    JsonDocument d;
    d["name"] = "Notifikace";
    d["cmd_t"] = t("notify");
    d["icon"] = "mdi:message-text";
    disc("notify", "notify", d);
  }
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
    d["stat_t"] = t((String(id) + "/state").c_str());
    d["icon"] = "mdi:gesture-tap-button";
    disc("binary_sensor", id, d);
  }
}

// ---------------------------------------------------------------- state
void publishState() {
  if (!mq.connected()) return;
  JsonDocument d;
  d["state"] = cfg.power ? "ON" : "OFF";
  d["brightness"] = cfg.brightness;
  d["color_mode"] = "rgb";
  JsonObject c = d["color"].to<JsonObject>();
  c["r"] = (cfg.textColor >> 16) & 0xFF;
  c["g"] = (cfg.textColor >> 8) & 0xFF;
  c["b"] = cfg.textColor & 0xFF;
  d["effect"] = "none";
  pubJson(t("light/state"), d);
  lastPage = Apps::currentName();
  pub(t("page/state"), lastPage);
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

void publishButton(int idx, bool pressed) {
  if (!mq.connected()) return;
  char id[32];
  snprintf(id, sizeof(id), "button%d/state", idx + 1);
  pub(t(id), pressed ? "ON" : "OFF", false);
}

// ---------------------------------------------------------------- commands
static bool onOff(const String& p, bool cur) {
  if (p.equalsIgnoreCase("ON") || p == "1" || p.equalsIgnoreCase("true")) return true;
  if (p.equalsIgnoreCase("OFF") || p == "0" || p.equalsIgnoreCase("false")) return false;
  if (p.equalsIgnoreCase("TOGGLE")) return !cur;
  return cur;
}

// temporary "effect" page activated through the light entity
static void setLightEffect(const String& fx) {
  JsonDocument d;
  if (fx.isEmpty() || fx == "none") {
    Apps::setCustom("_effect", d.as<JsonVariantConst>());
    return;
  }
  d["effect"] = fx;
  d["duration"] = 3600;
  Apps::setCustom("_effect", d.as<JsonVariantConst>());
  Apps::gotoPage("_effect");
}

static void onMessage(char* topicC, uint8_t* payload, unsigned int len) {
  String topic(topicC);
  String p;
  p.concat((const char*)payload, len);
  String b = base() + "/";
  if (topic == cfg.discoveryPrefix + "/status") {
    if (p == "online") { publishDiscovery(); reqMqttState = true; }
    return;
  }
  if (!topic.startsWith(b)) return;
  String sub = topic.substring(b.length());

  bool changed = false;
  if (sub == "light/set") {
    JsonDocument d;
    if (deserializeJson(d, p)) return;
    Lock l;
    if (d["state"].is<const char*>()) { cfg.power = String(d["state"].as<const char*>()) == "ON"; changed = true; }
    if (d["brightness"].is<int>()) { cfg.brightness = constrain(d["brightness"].as<int>(), 1, 255); cfg.autoBright = false; changed = true; }
    if (d["color"].is<JsonObject>()) {
      cfg.textColor = (d["color"]["r"].as<int>() << 16) | (d["color"]["g"].as<int>() << 8) | d["color"]["b"].as<int>();
      changed = true;
    }
    if (d["effect"].is<const char*>()) setLightEffect(d["effect"].as<const char*>());
  } else if (sub == "brightness/set") {
    Lock l;
    cfg.brightness = constrain(p.toInt(), 1, 255);
    cfg.autoBright = false;
    changed = true;
  } else if (sub == "page/set") {
    Apps::gotoPage(p);
    reqMqttState = true;
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
  } else if (sub == "power") {
    Lock l;
    cfg.power = onOff(p, cfg.power);
    changed = true;
  } else if (sub == "cmd") {
    if (p == "next") Apps::next();
    else if (p == "prev") Apps::prev();
    else if (p == "dismiss") Apps::dismiss();
    else if (p == "restart") reqReboot = true;
    else if (p == "select") Apps::buttonAction();
    reqMqttState = true;
  } else if (sub == "notify") {
    JsonDocument d;
    if (p.startsWith("{") && !deserializeJson(d, p)) Apps::notify(d.as<JsonVariantConst>());
    else {
      d.set(p);
      Apps::notify(d.as<JsonVariantConst>());
    }
  } else if (sub == "dismiss") {
    Apps::dismiss();
  } else if (sub.startsWith("custom/")) {
    String name = sub.substring(7);
    JsonDocument d;
    if (p.length() && p.startsWith("{")) {
      if (deserializeJson(d, p)) return;
    } else if (p.length()) {
      d.set(p);
    }
    Apps::setCustom(name, d.as<JsonVariantConst>());
  } else if (sub.startsWith("indicator")) {
    int idx = sub.substring(9).toInt() - 1;
    JsonDocument d;
    if (p.startsWith("{")) deserializeJson(d, p);
    else if (p.length()) d.set(p);
    Apps::setIndicator(idx, d.as<JsonVariantConst>());
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
    if (resolvedHost.isEmpty()) { lastError = "cannot resolve host"; return false; }
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
  const char* subs[] = {"light/set", "brightness/set", "page/set", "autorotate/set", "autobright/set", "night/set",
                        "power", "cmd", "notify", "dismiss", "custom/+", "indicator1", "indicator2", "indicator3"};
  for (auto s : subs) mq.subscribe((b + "/" + s).c_str());
  mq.subscribe((cfg.discoveryPrefix + "/status").c_str());
  publishDiscovery();
  publishState();
  publishSensors();
  return true;
}

void begin() {
  mq.setBufferSize(2048);
  mq.setCallback(onMessage);
  mq.setKeepAlive(30);
  mq.setSocketTimeout(5);
  configured = true;
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
  }
  if (reqMqttState) {
    reqMqttState = false;
    publishState();
  }
  if (millis() - lastSensors > 60000) {
    lastSensors = millis();
    publishSensors();
  }
  // page changes
  static uint32_t lastPageCheck = 0;
  if (millis() - lastPageCheck > 1000) {
    lastPageCheck = millis();
    if (Apps::currentName() != lastPage) {
      lastPage = Apps::currentName();
      pub(t("page/state"), lastPage);
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
