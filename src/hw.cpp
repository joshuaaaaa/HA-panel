#include "hw.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include "apps.h"
#include "config.h"
#include "mqtt.h"

// ================================================================ buttons + LDR
namespace Hw {

struct Button {
  bool state = false, lastRaw = false;
  uint32_t changed = 0, pressedAt = 0;
  bool longFired = false;
};
static Button btn[NUM_BUTTONS];
static float ldrAvg = -1;
static int ldrLast = 0;

void begin() {
#ifdef HW_ULANZI
  pinMode(15, OUTPUT);  // silence the on-board buzzer
  digitalWrite(15, LOW);
#endif
  for (int i = 0; i < NUM_BUTTONS; i++)
    if (cfg.btnPins[i] >= 0) pinMode(cfg.btnPins[i], cfg.btnActiveLow ? INPUT_PULLUP : INPUT_PULLDOWN);
  if (cfg.ldrPin >= 0) {
    pinMode(cfg.ldrPin, INPUT);
    analogReadResolution(12);
  }
}

static void onPress(int i) {
  Mqtt::publishButton(i, true);
}

static void onRelease(int i, uint32_t heldMs, bool longFired) {
  Mqtt::publishButton(i, false);
  if (longFired) return;
  if (i == 0) Apps::prev();
  else if (i == 2) Apps::next();
  else Apps::buttonAction();
}

static void onLong(int i) {
  if (i == 1) {  // long press middle: display on/off
    Lock l;
    cfg.power = !cfg.power;
    reqSaveConfig = true;
    reqMqttState = true;
  } else if (i == 0 || i == 2) {  // long press left/right: brightness
    Lock l;
    int b = cfg.brightness + (i == 2 ? 30 : -30);
    cfg.brightness = constrain(b, 2, 255);
    cfg.autoBright = false;
    reqSaveConfig = true;
    reqMqttState = true;
  }
}

int ldrRaw() { return ldrLast; }

void loop() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (cfg.btnPins[i] < 0) continue;
    bool raw = digitalRead(cfg.btnPins[i]) == (cfg.btnActiveLow ? LOW : HIGH);
    Button& b = btn[i];
    if (raw != b.lastRaw) { b.lastRaw = raw; b.changed = now; }
    if (now - b.changed > 30 && raw != b.state) {
      b.state = raw;
      if (raw) { b.pressedAt = now; b.longFired = false; onPress(i); }
      else onRelease(i, now - b.pressedAt, b.longFired);
    }
    if (b.state && !b.longFired && now - b.pressedAt > 800) {
      b.longFired = true;
      onLong(i);
    }
  }
  static uint32_t lastLdr = 0;
  if (cfg.ldrPin >= 0 && now - lastLdr > 200) {
    lastLdr = now;
    int v = analogRead(cfg.ldrPin);
    ldrLast = v;
    if (ldrAvg < 0) ldrAvg = v;
    ldrAvg = ldrAvg * 0.9f + v * 0.1f;
    float n = ldrAvg / 4095.0f;
    if (cfg.ldrInvert) n = 1.0f - n;
    // perceptual curve
    n = n * n;
    int br = cfg.minBright + (int)((cfg.maxBright - cfg.minBright) * n);
    Apps::ldrBrightness = constrain(br, 1, 255);
  }
}

}  // namespace Hw

// ================================================================ network
namespace Net {

static DNSServer dns;
static bool ap = false;
static uint32_t staStart = 0, lastReconnect = 0;
static bool wasConnected = false;
static bool mdnsStarted = false;
static int scanRes = -2;

static void startAP() {
  if (ap) return;
  String ssid = "HA-Panel-" + deviceId;
  WiFi.mode(cfg.wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(ssid.c_str(), cfg.apPass.length() >= 8 ? cfg.apPass.c_str() : nullptr);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  ap = true;
  Serial.printf("[net] AP started: %s  %s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  // permanent setup page while the access point is running
  JsonDocument d;
  d["text"] = "WiFi: " + ssid + "  heslo: " + (cfg.apPass.length() >= 8 ? cfg.apPass : String("-")) + "  web: " +
              WiFi.softAPIP().toString();
  d["icon"] = "wifi";
  d["color"] = "#40A0FF";
  d["duration"] = 5;
  Apps::setCustom("setup", d.as<JsonVariantConst>());
  Apps::gotoPage("setup");
}

static void stopAP() {
  if (!ap) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  JsonDocument d;
  Apps::setCustom("setup", d.as<JsonVariantConst>());
  Serial.println("[net] AP stopped");
}

static void startMdns() {
  if (mdnsStarted) return;
  if (MDNS.begin(cfg.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "device", "hapanel");
    mdnsStarted = true;
  }
}

void applyTime() {
  // SNTP keeps the server name pointers -> use static buffers
  static char ntp[64], tz[64];
  strlcpy(ntp, cfg.ntp.length() ? cfg.ntp.c_str() : "pool.ntp.org", sizeof(ntp));
  strlcpy(tz, cfg.tz.c_str(), sizeof(tz));
  configTzTime(tz, ntp, "time.google.com");
}

void begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(cfg.hostname.c_str());
  applyTime();
  if (cfg.wifiSsid.isEmpty()) {
    startAP();
    return;
  }
  WiFi.mode(WIFI_STA);
  if (cfg.staticIp) {
    IPAddress ip, gw, sn, d;
    if (ip.fromString(cfg.ip) && gw.fromString(cfg.gateway) && sn.fromString(cfg.subnet)) {
      if (!d.fromString(cfg.dns)) d = gw;
      WiFi.config(ip, gw, sn, d);
    }
  }
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  WiFi.setSleep(false);
  staStart = millis();
}

void loop() {
  if (ap) dns.processNextRequest();
  bool conn = WiFi.status() == WL_CONNECTED;
  if (conn && !wasConnected) {
    wasConnected = true;
    Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
    startMdns();
    Apps::systemMessage(WiFi.localIP().toString(), "wifi", 3000, false);
    // keep AP a bit so that the setup page can show the new IP, then stop it
  }
  if (!conn && wasConnected) {
    wasConnected = false;
    staStart = millis();
    Serial.println("[net] disconnected");
  }
  if (conn && ap && millis() - staStart > 60000) stopAP();
  if (!conn && cfg.wifiSsid.length()) {
    // fallback AP after 30 s without connection
    if (!ap && millis() - staStart > 30000) startAP();
    if (millis() - lastReconnect > 20000 && millis() - staStart > 10000) {
      lastReconnect = millis();
      WiFi.disconnect();
      WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    }
  }
  if (scanRes == -1) {
    int r = WiFi.scanComplete();
    if (r >= 0) scanRes = r;
    else if (r == WIFI_SCAN_FAILED) scanRes = 0;
  }
}

bool apMode() { return ap; }

String ip() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }

void scanStart() {
  if (scanRes == -1) return;
  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  WiFi.scanNetworks(true);
  scanRes = -1;
}

int scanState() { return scanRes; }

}  // namespace Net
