// HA-Panel - Home Assistant LED matrix display firmware (ESP32 + WS2812 8x32)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "apps.h"
#include "config.h"
#include "display.h"
#include "ha_client.h"
#include "hw.h"
#include "icons.h"
#include "mqtt.h"
#include "web.h"

#ifndef ARDUINO_RUNNING_CORE
#define ARDUINO_RUNNING_CORE 1
#endif

static void renderTask(void*) {
  const uint32_t period = 20;  // ms -> 50 fps
  for (;;) {
    uint32_t start = millis();
    Apps::frame();
    uint32_t took = millis() - start;
    // always yield at least one tick so that the network loop never starves
    vTaskDelay(pdMS_TO_TICKS(took < period ? period - took : 1));
  }
}

static void setupOta() {
  ArduinoOTA.setHostname(cfg.hostname.c_str());
  if (cfg.webPass.length()) ArduinoOTA.setPassword(cfg.webPass.c_str());
  ArduinoOTA.onStart([]() { Apps::setOtaProgress(0); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { Apps::setOtaProgress(t ? p * 100 / t : 0); });
  ArduinoOTA.onError([](ota_error_t) { Apps::setOtaProgress(-1); });
  ArduinoOTA.setMdnsEnabled(false);  // mDNS is handled by Net
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  gLock = xSemaphoreCreateRecursiveMutex();

  uint64_t mac = ESP.getEfuseMac();
  char id[8];
  snprintf(id, sizeof(id), "%06x", (uint32_t)((mac >> 24) & 0xFFFFFF));
  deviceId = id;
  Serial.printf("\n%s %s  id=%s\n", FW_NAME, FW_VERSION, id);

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount failed");
  configInitDefaults();
  if (!configLoad()) Serial.println("[cfg] using defaults");
  if (!pagesLoad()) Serial.println("[cfg] default pages");

  // hold the middle button for 5 s while powering on -> factory reset
  int8_t mid = cfg.btnPins[1];
  if (mid >= 0) {
    pinMode(mid, cfg.btnActiveLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    delay(20);
    uint32_t t0 = millis();
    while (digitalRead(mid) == (cfg.btnActiveLow ? LOW : HIGH) && millis() - t0 < 5000) delay(10);
    if (millis() - t0 >= 5000) {
      Serial.println("[cfg] factory reset by button");
      LittleFS.remove("/config.json");
      LittleFS.remove("/pages.json");
      ESP.restart();
    }
  }

  Display::begin();
  Icons::begin();
  Apps::begin();
  Apps::systemMessage(String(FW_NAME) + " " + FW_VERSION, "ha", 2500);
  xTaskCreatePinnedToCore(renderTask, "render", 8192, nullptr, 2, nullptr, ARDUINO_RUNNING_CORE);

  Hw::begin();
  Net::begin();
  Web::begin();
  HA::begin();
  Mqtt::begin();
  setupOta();
}

void loop() {
  Net::loop();
  ArduinoOTA.handle();
  HA::loop();
  Mqtt::loop();
  Web::loop();
  Hw::loop();

  if (reqSelect) {
    reqSelect = false;
    Apps::buttonAction();
  }
  if (reqHaResync) {
    reqHaResync = false;
    HA::resubscribe();
  }
  if (reqSaveConfig) {
    reqSaveConfig = false;
    configSave();
  }
  if (reqSavePages) {
    reqSavePages = false;
    pagesSave();
  }
  if (reqReboot) {
    delay(800);
    ESP.restart();
  }
  delay(2);
}
