#include "web.h"
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <memory>
#include "apps.h"
#include "config.h"
#include "display.h"
#include "effects.h"
#include "ha_client.h"
#include "hw.h"
#include "icons.h"
#include "mqtt.h"
#include "web_index.h"

namespace Web {

static AsyncWebServer server(80);
static AsyncWebSocket wsPreview("/ws");
static volatile uint32_t pendingChanges = 0;
static uint8_t* frameBuf = nullptr;
static size_t frameLen = 0;
static bool otaError = false;

static bool auth(AsyncWebServerRequest* r) {
  if (cfg.webPass.isEmpty()) return true;
  if (r->authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) return true;
  r->requestAuthentication("HA-Panel");
  return false;
}

static void sendDoc(AsyncWebServerRequest* r, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  r->send(code, "application/json", out);
}

static void sendOk(AsyncWebServerRequest* r, bool ok = true, const char* msg = nullptr) {
  JsonDocument d;
  d["ok"] = ok;
  if (msg) d["message"] = msg;
  sendDoc(r, d, ok ? 200 : 400);
}

static AsyncCallbackJsonWebHandler* jsonPost(const char* uri, ArJsonRequestHandlerFunction fn, size_t maxLen = 16384) {
  auto* h = new AsyncCallbackJsonWebHandler(uri, fn);
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(maxLen);
  server.addHandler(h);
  return h;
}

static void statusJson(JsonObject o) {
  o["name"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["build"] = __DATE__ " " __TIME__;
  o["id"] = deviceId;
  o["hostname"] = cfg.hostname;
  o["chip"] = ESP.getChipModel();
  o["cores"] = ESP.getChipCores();
  o["flash"] = ESP.getFlashChipSize();
  o["sketch_size"] = ESP.getSketchSize();
  o["sketch_free"] = ESP.getFreeSketchSpace();
  o["heap"] = ESP.getFreeHeap();
  o["heap_min"] = ESP.getMinFreeHeap();
  o["psram"] = ESP.getFreePsram();
  o["uptime"] = millis() / 1000;
  o["fs_used"] = LittleFS.usedBytes();
  o["fs_total"] = LittleFS.totalBytes();
  JsonObject w = o["wifi"].to<JsonObject>();
  w["connected"] = WiFi.status() == WL_CONNECTED;
  w["ssid"] = WiFi.SSID();
  w["rssi"] = WiFi.RSSI();
  w["ip"] = Net::ip();
  w["mac"] = WiFi.macAddress();
  w["ap"] = Net::apMode();
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  o["time"] = buf;
  o["time_valid"] = now > 1600000000;
  JsonObject d = o["display"].to<JsonObject>();
  d["width"] = cfg.width;
  d["height"] = cfg.height;
  d["power"] = cfg.power;
  d["brightness"] = cfg.brightness;
  d["auto_bright"] = cfg.autoBright;
  d["auto_rotate"] = cfg.autoRotate;
  d["night_enabled"] = cfg.nightEnabled;
  d["current_brightness"] = Display::currentBrightness();
  d["current_ma"] = Display::estimatedCurrent();
  d["fps"] = Display::fps();
  d["led_pin"] = cfg.ledPin;
  d["leds"] = Display::ledCount();
  d["ldr_raw"] = Hw::ldrRaw();
  d["text_color"] = colorToHex(cfg.textColor);
  Apps::statusJson(o["apps"].to<JsonObject>());
  HA::statusJson(o["ha"].to<JsonObject>());
  Mqtt::statusJson(o["mqtt"].to<JsonObject>());
}

static void handleControl(JsonObjectConst j) {
  bool changed = false;
  {
    Lock l;
    if (j["power"].is<bool>()) { cfg.power = j["power"]; changed = true; }
    if (j["brightness"].is<int>()) { cfg.brightness = constrain(j["brightness"].as<int>(), 1, 255); changed = true; }
    if (j["auto_bright"].is<bool>()) { cfg.autoBright = j["auto_bright"]; changed = true; }
    if (j["auto_rotate"].is<bool>()) { cfg.autoRotate = j["auto_rotate"]; changed = true; }
    if (j["night"].is<bool>()) { cfg.nightEnabled = j["night"]; changed = true; }
    if (j["text_color"].is<const char*>()) {
      cfg.textColor = parseColor(j["text_color"].as<const char*>(), cfg.textColor);
      changed = true;
    }
  }
  if (j["next"] | false) Apps::next();
  if (j["prev"] | false) Apps::prev();
  if (j["select"] | false) reqSelect = true;  // executed by the main loop (talks to HA)
  if (j["dismiss"] | false) Apps::dismiss();
  if (j["page"].is<const char*>() || j["page"].is<int>()) Apps::gotoPage(j["page"].as<String>());
  if (changed) reqSaveConfig = true;
  reqMqttState = true;
}

// ---------------------------------------------------------------- OTA
static void onUpdateUpload(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len,
                           bool final) {
  if (index == 0) {
    if (!cfg.webPass.isEmpty() && !r->authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) {
      otaError = true;
      return;
    }
    otaError = false;
    Serial.printf("[ota] start %s (%u bytes)\n", filename.c_str(), (unsigned)r->contentLength());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Update.printError(Serial);
      otaError = true;
      return;
    }
    Apps::setOtaProgress(0);
  }
  if (otaError) return;
  if (len && Update.write(data, len) != len) {
    Update.printError(Serial);
    otaError = true;
    Update.abort();
    Apps::setOtaProgress(-1);
    return;
  }
  size_t total = r->contentLength();
  if (total) Apps::setOtaProgress(min(99, (int)((index + len) * 100 / total)));
  if (final) {
    if (Update.end(true)) {
      Serial.printf("[ota] done, %u bytes\n", (unsigned)(index + len));
      Apps::setOtaProgress(100);
    } else {
      Update.printError(Serial);
      otaError = true;
      Apps::setOtaProgress(-1);
    }
  }
}

// ---------------------------------------------------------------- routes
static void routes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    statusJson(d.to<JsonObject>());
    sendDoc(r, d);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    Lock l;
    configToJson(d.to<JsonObject>(), r->hasParam("secrets"));
    sendDoc(r, d);
  });

  jsonPost("/api/config", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    if (!j.is<JsonObject>()) return sendOk(r, false, "object expected");
    uint32_t chg;
    {
      Lock l;
      chg = configFromJson(j.as<JsonObjectConst>());
    }
    reqSaveConfig = true;
    pendingChanges |= chg;
    JsonDocument d;
    d["ok"] = true;
    d["reboot"] = (chg & CFG_CHG_REBOOT) != 0;
    sendDoc(r, d);
  });

  server.on("/api/pages", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    Lock l;
    pagesToJson(d.to<JsonArray>());
    sendDoc(r, d);
  });

  jsonPost(
      "/api/pages",
      [](AsyncWebServerRequest* r, JsonVariant& j) {
        if (!auth(r)) return;
        if (!j.is<JsonArray>()) return sendOk(r, false, "array expected");
        {
          Lock l;
          pagesFromJson(j.as<JsonArrayConst>());
          Apps::onPagesChanged();
        }
        reqSavePages = true;
        reqHaResync = true;
        reqMqttRediscover = true;
        sendOk(r);
      },
      48 * 1024);

  server.on("/api/icons", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    Icons::list(d.to<JsonArray>());
    sendDoc(r, d);
  });

  server.on("/api/icon", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (!r->hasParam("name")) return sendOk(r, false, "name missing");
    JsonDocument d;
    bool ok;
    {
      Lock l;
      ok = Icons::toJson(r->getParam("name")->value(), d.to<JsonObject>());
    }
    if (!ok) return r->send(404, "application/json", "{\"ok\":false}");
    sendDoc(r, d);
  });

  jsonPost("/api/icon", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    String err;
    bool ok = Icons::save(j["name"] | "", j["delay"] | 200, j["frames"].as<JsonArrayConst>(), err);
    sendOk(r, ok, ok ? nullptr : err.c_str());
  });

  server.on("/api/icon", HTTP_DELETE, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (!r->hasParam("name")) return sendOk(r, false, "name missing");
    sendOk(r, Icons::remove(r->getParam("name")->value()));
  });

  server.on("/api/effects", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    Effects::list(d.to<JsonArray>());
    sendDoc(r, d);
  });

  jsonPost("/api/notify", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    Apps::notify(j);
    sendOk(r);
  });

  server.on("/api/dismiss", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    sendOk(r, Apps::dismiss());
  });

  // custom page: POST /api/custom?name=x with JSON body; empty body / {} removes
  jsonPost("/api/custom", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    if (!r->hasParam("name")) return sendOk(r, false, "name missing");
    Apps::setCustom(r->getParam("name")->value(), j);
    sendOk(r);
  });
  server.on("/api/custom", HTTP_DELETE, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (!r->hasParam("name")) return sendOk(r, false, "name missing");
    JsonDocument d;
    Apps::setCustom(r->getParam("name")->value(), d.as<JsonVariantConst>());
    sendOk(r);
  });
  server.on("/api/custom", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    Apps::customList(d.to<JsonArray>());
    sendDoc(r, d);
  });

  jsonPost("/api/indicator", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    int n = r->hasParam("n") ? r->getParam("n")->value().toInt() : (j["n"] | 1);
    Apps::setIndicator(n - 1, j);
    sendOk(r);
  });

  jsonPost("/api/control", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    handleControl(j.as<JsonObjectConst>());
    sendOk(r);
  });

  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (r->hasParam("raw")) Display::rawTest(10000);
    else Apps::showTest(15000);
    sendOk(r);
  });

  server.on("/api/frame", HTTP_GET, [](AsyncWebServerRequest* r) {
    uint32_t n;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[frameLen]);
    size_t len = Apps::copyFrame(buf.get(), frameLen, n);
    AsyncResponseStream* rs = r->beginResponseStream("application/octet-stream");
    rs->write(buf.get(), len);
    r->send(rs);
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    int st = Net::scanState();
    if (st < 0) {
      if (st == -2 || r->hasParam("refresh")) Net::scanStart();
      return r->send(202, "application/json", "{\"scanning\":true}");
    }
    JsonDocument d;
    JsonArray a = d["networks"].to<JsonArray>();
    for (int i = 0; i < st; i++) {
      JsonObject n = a.add<JsonObject>();
      n["ssid"] = WiFi.SSID(i);
      n["rssi"] = WiFi.RSSI(i);
      n["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    if (r->hasParam("refresh")) Net::scanStart();
    sendDoc(r, d);
  });

  server.on("/api/ha/entities", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    if (!cfg.haEnabled || cfg.haToken.isEmpty()) return r->send(400, "text/plain", "HA not configured");
    HA::JobState st = HA::entitiesState();
    if (st == HA::JOB_DONE) {
      Lock l;
      return r->send(200, "text/plain; charset=utf-8", HA::entitiesData());
    }
    if (st == HA::JOB_ERROR) {
      String err;
      {
        Lock l;
        err = HA::entitiesData();
      }
      HA::requestEntities();
      return r->send(502, "text/plain", err);
    }
    HA::requestEntities();
    r->send(202, "text/plain", "pending");
  });

  jsonPost("/api/ha/render", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!auth(r)) return;
    if (!cfg.haEnabled || cfg.haToken.isEmpty()) return sendOk(r, false, "HA not configured");
    HA::requestRender(j["template"] | "");
    r->send(202, "application/json", "{\"ok\":true}");
  });
  server.on("/api/ha/render", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    JsonDocument d;
    HA::JobState st = HA::renderState();
    d["state"] = st == HA::JOB_DONE ? "done" : st == HA::JOB_ERROR ? "error" : st == HA::JOB_IDLE ? "idle" : "pending";
    d["result"] = HA::renderResult();
    sendDoc(r, d);
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    sendOk(r);
    reqReboot = true;
  });

  server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!auth(r)) return;
    LittleFS.remove("/config.json");
    LittleFS.remove("/pages.json");
    if (r->hasParam("icons")) {
      File dir = LittleFS.open("/icons");
      std::vector<String> names;
      File f;
      while (dir && (f = dir.openNextFile())) {
        names.push_back(String("/icons/") + f.name());
        f.close();
      }
      for (auto& n : names) LittleFS.remove(n);
    }
    sendOk(r);
    reqReboot = true;
  });

  server.on(
      "/api/update", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        if (!auth(r)) return;
        bool ok = !otaError && !Update.hasError() && Update.isFinished();
        sendOk(r, ok, ok ? "Firmware nahrán, restartuji…" : Update.errorString());
        if (ok) reqReboot = true;
        else Apps::setOtaProgress(-1);
      },
      onUpdateUpload);

  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->method() == HTTP_OPTIONS) return r->send(204);
    // captive portal: redirect everything to the setup page
    if (Net::apMode() && r->host() != WiFi.softAPIP().toString()) {
      return r->redirect("http://" + WiFi.softAPIP().toString() + "/");
    }
    r->send(404, "text/plain", "Not found");
  });
}

void begin() {
  frameLen = cfg.width * cfg.height * 3;
  frameBuf = (uint8_t*)malloc(frameLen);
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  wsPreview.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      char hdr[32];
      snprintf(hdr, sizeof(hdr), "{\"w\":%d,\"h\":%d}", cfg.width, cfg.height);
      c->text(hdr);
    }
  });
  server.addHandler(&wsPreview);
  routes();
  server.begin();
}

void loop() {
  static uint32_t lastFrame = 0, lastClean = 0, lastFrameNo = 0;
  uint32_t now = millis();
  if (wsPreview.count() > 0 && now - lastFrame > 80) {
    lastFrame = now;
    uint32_t n;
    size_t len = Apps::copyFrame(frameBuf, frameLen, n);
    if (n != lastFrameNo && wsPreview.availableForWriteAll()) {
      lastFrameNo = n;
      wsPreview.binaryAll(frameBuf, len);
    }
  }
  if (now - lastClean > 2000) {
    lastClean = now;
    wsPreview.cleanupClients(3);
  }
  uint32_t chg = pendingChanges;
  if (chg) {
    pendingChanges = 0;
    if (chg & CFG_CHG_HA) {
      HA::reconfigure();
      reqHaResync = true;
    }
    if (chg & CFG_CHG_MQTT) Mqtt::reconfigure();
    if (chg & CFG_CHG_TIME) Net::applyTime();
    if (chg & (CFG_CHG_DISPLAY | CFG_CHG_MQTT)) reqMqttState = true;
  }
}

}  // namespace Web
