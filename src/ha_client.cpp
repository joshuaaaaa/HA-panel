#include "ha_client.h"
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>
#include <vector>
#include "apps.h"
#include "config.h"

namespace HA {

enum ConnState : uint8_t { ST_DISABLED, ST_NO_WIFI, ST_CONNECTING, ST_AUTH_FAILED, ST_CONNECTED };

static WebSocketsClient ws;
static bool started = false;
static bool authed = false;
static ConnState state = ST_DISABLED;
static uint32_t msgId = 1;
static uint32_t lastConnectTry = 0;
static String lastError;
static uint32_t connectedSince = 0;

// parsed URL
static bool useSsl = false;
static String host, resolvedHost;
static uint16_t port = 8123;
static String basePath;

struct Sub {
  uint32_t id;
  uint8_t kind;  // 0 page, 1 indicator
  uint8_t idx;
  uint8_t field;
};
static std::vector<Sub> subs;
static bool needResub = false;

// jobs
static JobState entState = JOB_IDLE, rndState = JOB_IDLE;
static String entData, rndTemplate, rndResult;
static uint32_t entTime = 0;

// ---------------------------------------------------------------- URL helpers
static bool parseUrl(const String& url) {
  String u = url;
  u.trim();
  useSsl = false;
  if (u.startsWith("https://")) { useSsl = true; u = u.substring(8); }
  else if (u.startsWith("http://")) u = u.substring(7);
  else if (u.startsWith("wss://")) { useSsl = true; u = u.substring(6); }
  else if (u.startsWith("ws://")) u = u.substring(5);
  int slash = u.indexOf('/');
  basePath = slash >= 0 ? u.substring(slash) : "";
  while (basePath.endsWith("/")) basePath.remove(basePath.length() - 1);
  if (basePath == "/api/websocket" || basePath == "/api") basePath = "";
  if (slash >= 0) u = u.substring(0, slash);
  int colon = u.lastIndexOf(':');
  if (colon > 0) {
    host = u.substring(0, colon);
    port = u.substring(colon + 1).toInt();
  } else {
    host = u;
    port = useSsl ? 443 : 8123;
  }
  return host.length() > 0 && port > 0;
}

static String resolveHost(const String& h) {
  if (h.endsWith(".local")) {
    IPAddress ip = MDNS.queryHost(h.substring(0, h.length() - 6), 2500);
    if (ip != IPAddress((uint32_t)0)) return ip.toString();
  }
  return h;
}

static String baseUrl() {
  return String(useSsl ? "https://" : "http://") + resolvedHost + ":" + port + basePath;
}

// ---------------------------------------------------------------- templates
static String entityTemplate(const PageCfg& p) {
  const String& e = p.entity;
  String t = "{% set s = states('" + e + "') %}{% if s in ['unavailable','unknown'] %}--{% elif s|is_number %}";
  if (p.decimals >= 0) t += "{{ '%." + String(p.decimals) + "f'|format(s|float) }}";
  else t += "{{ s }}";
  t += "{% else %}{{ s }}{% endif %}";
  if (p.unit == "-") {
    // no unit
  } else if (p.unit.length()) {
    t += "{% if s|is_number %}" + p.unit + "{% endif %}";
  } else {
    t += "{% if s|is_number %}{{ state_attr('" + e + "','unit_of_measurement') or '' }}{% endif %}";
  }
  return t;
}

static void sendJson(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  ws.sendTXT(out);
}

static void subscribeTemplate(const String& tpl, uint8_t kind, uint8_t idx, uint8_t field) {
  JsonDocument doc;
  uint32_t id = msgId++;
  doc["id"] = id;
  doc["type"] = "render_template";
  doc["template"] = tpl;
  doc["report_errors"] = true;
  sendJson(doc);
  subs.push_back({id, kind, idx, field});
}

struct PendingTpl {
  String tpl;
  uint8_t kind, idx, field;
};

static void subscribeAll() {
  std::vector<PendingTpl> list;
  {
    Lock l;
    for (int i = 0; i < cfg.pageCount; i++) {
      const PageCfg& p = cfg.pages[i];
      if (p.type == PT_ENTITY && p.entity.length()) list.push_back({entityTemplate(p), 0, (uint8_t)i, Apps::F_TEXT});
      if (p.type == PT_TEMPLATE && p.text.length()) list.push_back({p.text, 0, (uint8_t)i, Apps::F_TEXT});
      if (p.colorTpl.length()) list.push_back({p.colorTpl, 0, (uint8_t)i, Apps::F_COLOR});
      if (p.iconTpl.length()) list.push_back({p.iconTpl, 0, (uint8_t)i, Apps::F_ICON});
      if (p.visibleTpl.length()) list.push_back({p.visibleTpl, 0, (uint8_t)i, Apps::F_VISIBLE});
      if (p.progressTpl.length()) list.push_back({p.progressTpl, 0, (uint8_t)i, Apps::F_PROGRESS});
    }
    for (int i = 0; i < NUM_INDICATORS; i++)
      if (cfg.indTpl[i].length()) list.push_back({cfg.indTpl[i], 1, (uint8_t)i, 0});
  }
  for (auto& t : list) subscribeTemplate(t.tpl, t.kind, t.idx, t.field);
  Serial.printf("[ha] subscribed %u templates\n", (unsigned)list.size());
}

static void unsubscribeAll() {
  for (auto& s : subs) {
    JsonDocument doc;
    doc["id"] = msgId++;
    doc["type"] = "unsubscribe_events";
    doc["subscription"] = s.id;
    sendJson(doc);
  }
  subs.clear();
}

static Sub* findSub(uint32_t id) {
  for (auto& s : subs) if (s.id == id) return &s;
  return nullptr;
}

static void deliver(const Sub& s, const String& value, bool error) {
  if (s.kind == 0) Apps::setTemplateResult(s.idx, s.field, value, error);
  else Apps::setIndicatorTemplate(s.idx, error ? "" : value);
}

// ---------------------------------------------------------------- websocket
static void onMessage(uint8_t* payload, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[ha] json error %s\n", err.c_str());
    return;
  }
  const char* type = doc["type"] | "";
  if (!strcmp(type, "auth_required")) {
    JsonDocument a;
    a["type"] = "auth";
    a["access_token"] = cfg.haToken;
    sendJson(a);
  } else if (!strcmp(type, "auth_ok")) {
    authed = true;
    state = ST_CONNECTED;
    connectedSince = millis();
    lastError = "";
    ws.setReconnectInterval(5000);
    subs.clear();
    Serial.println("[ha] authenticated");
    subscribeAll();
    needResub = false;
  } else if (!strcmp(type, "auth_invalid")) {
    authed = false;
    state = ST_AUTH_FAILED;
    lastError = doc["message"] | "auth_invalid";
    Serial.printf("[ha] auth failed: %s\n", lastError.c_str());
    ws.setReconnectInterval(60000);
    ws.disconnect();
  } else if (!strcmp(type, "result")) {
    uint32_t id = doc["id"] | 0;
    bool success = doc["success"] | false;
    Sub* s = findSub(id);
    if (!success) {
      String msg = doc["error"]["message"] | "error";
      Serial.printf("[ha] request %u failed: %s\n", id, msg.c_str());
      if (s) deliver(*s, msg, true);
      lastError = msg;
    }
  } else if (!strcmp(type, "event")) {
    uint32_t id = doc["id"] | 0;
    Sub* s = findSub(id);
    if (!s) return;
    JsonVariantConst ev = doc["event"];
    if (ev["error"].is<const char*>()) {
      String level = ev["level"] | "ERROR";
      if (level == "ERROR") {
        Serial.printf("[ha] template error: %s\n", ev["error"].as<const char*>());
        deliver(*s, ev["error"].as<String>(), true);
      }
      return;
    }
    JsonVariantConst r = ev["result"];
    String v;
    if (r.isNull()) v = "";
    else if (r.is<const char*>()) v = r.as<const char*>();
    else if (r.is<bool>()) v = r.as<bool>() ? "true" : "false";
    else serializeJson(r, v);
    deliver(*s, v, false);
  }
}

static void onEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[ha] websocket connected");
      msgId = 1;
      authed = false;
      subs.clear();
      break;
    case WStype_DISCONNECTED:
      if (authed) Serial.println("[ha] websocket disconnected");
      authed = false;
      subs.clear();
      if (state != ST_AUTH_FAILED && state != ST_DISABLED) state = ST_CONNECTING;
      break;
    case WStype_TEXT:
      onMessage(payload, len);
      break;
    case WStype_ERROR:
      lastError = "websocket error";
      break;
    default:
      break;
  }
}

static void start() {
  if (!parseUrl(cfg.haUrl)) {
    lastError = "invalid URL";
    return;
  }
  resolvedHost = resolveHost(host);
  String path = basePath + "/api/websocket";
  Serial.printf("[ha] connecting %s://%s:%u%s\n", useSsl ? "wss" : "ws", resolvedHost.c_str(), port, path.c_str());
  ws.onEvent(onEvent);
  ws.setExtraHeaders("");
  if (useSsl) ws.beginSSL(resolvedHost.c_str(), port, path.c_str(), "", "");
  else ws.begin(resolvedHost.c_str(), port, path.c_str(), "");
  ws.setReconnectInterval(5000);
  ws.enableHeartbeat(20000, 5000, 2);
  started = true;
  state = ST_CONNECTING;
}

static void stop() {
  if (started) ws.disconnect();
  started = false;
  authed = false;
  subs.clear();
}

void begin() {}

void reconfigure() {
  stop();
  state = cfg.haEnabled ? ST_CONNECTING : ST_DISABLED;
  lastConnectTry = 0;
  entState = JOB_IDLE;
  entData = String();
}

void resubscribe() { needResub = true; }

bool connected() { return authed; }

String stateText() {
  switch (state) {
    case ST_DISABLED: return "disabled";
    case ST_NO_WIFI: return "no_wifi";
    case ST_CONNECTING: return "connecting";
    case ST_AUTH_FAILED: return "auth_failed";
    case ST_CONNECTED: return "connected";
  }
  return "?";
}

bool callService(const String& domain, const String& service, const String& entity) {
  if (!authed || domain.isEmpty() || service.isEmpty()) return false;
  JsonDocument doc;
  doc["id"] = msgId++;
  doc["type"] = "call_service";
  doc["domain"] = domain;
  doc["service"] = service;
  if (entity.length()) doc["target"]["entity_id"] = entity;
  sendJson(doc);
  return true;
}

// ---------------------------------------------------------------- REST jobs
class LimitedPrint : public Stream {
 public:
  String& out;
  size_t max;
  bool truncated = false;
  LimitedPrint(String& o, size_t m) : out(o), max(m) {}
  size_t write(uint8_t c) override {
    if (out.length() >= max) { truncated = true; return 1; }
    out += (char)c;
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    size_t room = out.length() < max ? max - out.length() : 0;
    if (n > room) truncated = true;
    if (room) out.concat((const char*)buf, min(n, room));
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static int renderRest(const String& tpl, String& out, size_t maxLen) {
  if (resolvedHost.isEmpty()) {
    parseUrl(cfg.haUrl);
    resolvedHost = resolveHost(host);
  }
  std::unique_ptr<WiFiClient> client;
  if (useSsl) {
    auto* c = new WiFiClientSecure();
    c->setInsecure();
    client.reset(c);
  } else {
    client.reset(new WiFiClient());
  }
  HTTPClient http;
  http.setTimeout(10000);
  http.setConnectTimeout(4000);
  if (!http.begin(*client, baseUrl() + "/api/template")) return -1;
  http.addHeader("Authorization", "Bearer " + cfg.haToken);
  http.addHeader("Content-Type", "application/json");
  JsonDocument doc;
  doc["template"] = tpl;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  out = "";
  if (code > 0) {
    out.reserve(min<size_t>(maxLen, 1024));
    LimitedPrint lp(out, maxLen);
    http.writeToStream(&lp);
  }
  http.end();
  return code;
}

static void runJobs() {
  if (entState == JOB_PENDING) {
    entState = JOB_RUNNING;
    String tpl =
        "{% for s in states|sort(attribute='entity_id') %}{{ s.entity_id ~ '\\t' ~ (s.name|replace('\\n',' ')) ~ '\\n' "
        "}}{% endfor %}";
    String out;
    int code = renderRest(tpl, out, 64 * 1024);
    Lock l;
    if (code == 200) {
      entData = std::move(out);
      entState = JOB_DONE;
      entTime = millis();
    } else {
      entData = "HTTP " + String(code);
      entState = JOB_ERROR;
    }
    Serial.printf("[ha] entity list: code %d, %u bytes\n", code, entData.length());
  }
  if (rndState == JOB_PENDING) {
    String tpl;
    {
      Lock l;
      rndState = JOB_RUNNING;
      tpl = rndTemplate;
    }
    String out;
    int code = renderRest(tpl, out, 2048);
    Lock l;
    if (code == 200) {
      rndResult = out;
      rndState = JOB_DONE;
    } else {
      rndResult = code > 0 ? out : ("HTTP " + String(code));
      rndState = JOB_ERROR;
    }
  }
}

void requestEntities() {
  Lock l;
  if (entState == JOB_RUNNING || entState == JOB_PENDING) return;
  if (entState == JOB_DONE && millis() - entTime < 60000) return;
  entState = JOB_PENDING;
}
JobState entitiesState() { return entState; }
const String& entitiesData() { return entData; }

void requestRender(const String& tpl) {
  Lock l;
  rndTemplate = tpl;
  rndResult = "";
  rndState = JOB_PENDING;
}
JobState renderState() { return rndState; }
String renderResult() {
  Lock l;
  return rndResult;
}

// ---------------------------------------------------------------- loop
void loop() {
  if (!cfg.haEnabled || cfg.haToken.isEmpty()) {
    if (started) stop();
    state = ST_DISABLED;
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (started) stop();
    state = ST_NO_WIFI;
    return;
  }
  if (!started) {
    if (millis() - lastConnectTry < 3000 && lastConnectTry) return;
    lastConnectTry = millis();
    start();
  }
  ws.loop();
  if (authed && needResub) {
    needResub = false;
    unsubscribeAll();
    subscribeAll();
  }
  runJobs();
}

void statusJson(JsonObject o) {
  o["state"] = stateText();
  o["connected"] = authed;
  o["error"] = lastError;
  o["subscriptions"] = (int)subs.size();
  o["host"] = resolvedHost;
  if (authed) o["uptime"] = (millis() - connectedSince) / 1000;
}

}  // namespace HA
