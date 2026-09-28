#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Apps {

enum TplField : uint8_t { F_TEXT = 0, F_COLOR, F_ICON, F_VISIBLE, F_PROGRESS, F_COUNT };

void begin();
void frame();  // called by the render task (~50 fps)

// navigation (thread safe)
void next();
void prev();
bool gotoPage(const String& nameOrIndex);
String currentName();
void buttonAction();  // middle button / "select"

// notifications: accepts plain string or object
void notify(JsonVariantConst v);
void systemMessage(const String& text, const String& icon, uint32_t durationMs = 4000, bool wakeup = true);
bool dismiss();
int queueLength();

// custom pages pushed over MQTT / HTTP (empty payload removes)
void setCustom(const String& name, JsonVariantConst v);
void customList(JsonArray a);

// indicators (1..3): object {color, blink} or empty to clear override
void setIndicator(int idx, JsonVariantConst v);
bool indicatorOn(int idx);

// AWTRIX 3 compatible extras
void setMoodlight(JsonVariantConst v);   // {"brightness":170,"kelvin":2300} / {"color":[..]}; empty = off
bool moodlightOn();
bool switchApp(const String& name);      // "Time", "Date" or page / custom app name
void loopJson(JsonObject o);             // {"app name": position}

// values rendered by Home Assistant templates
void setTemplateResult(uint8_t page, uint8_t field, const String& value, bool error = false);
void setIndicatorTemplate(uint8_t idx, const String& value);
void onPagesChanged();

void showTest(uint32_t durationMs);
void setOtaProgress(int pct);  // -1 = off

bool isNight();
uint8_t targetBrightness();
extern volatile uint8_t ldrBrightness;

// copies latest logical frame (RGB, row-major). returns bytes written
size_t copyFrame(uint8_t* out, size_t maxLen, uint32_t& frameNo);

void statusJson(JsonObject o);
}
