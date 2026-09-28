#pragma once
// AWTRIX 3 compatible API helpers shared by the HTTP server and MQTT
// (https://blueforcer.github.io/awtrix3/#/api)
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Awtrix {
void stats(JsonObject o);                 // /api/stats, [PREFIX]/stats
void settings(JsonObject o);              // GET /api/settings
void applySettings(JsonVariantConst v);   // POST /api/settings, [PREFIX]/settings
void screen(JsonArray a);                 // /api/screen, [PREFIX]/screen (24-bit colors)
void transitions(JsonArray a);            // /api/transitions
bool power(JsonVariantConst v);           // {"power":true} / true / "ON" ... returns new state
// handles a command addressed by AWTRIX topic / endpoint name (without prefix):
// notify, notify/dismiss, custom/<name>, indicator1-3, power, sleep, moodlight, switch,
// nextapp, previousapp, settings, reboot, sound, rtttl. Returns false if unknown.
bool command(const String& cmd, JsonVariantConst payload);
}
