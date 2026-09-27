#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "display.h"

#define ICON_MAX_FRAMES 8
#define ICON_PIXELS 64

namespace Icons {
void begin();
// draws 8x8 icon at x,y. returns false if icon unknown. ms = animation time
bool draw(Canvas& c, int x, int y, const String& name, uint32_t ms);
bool exists(const String& name);
// list of all icons: [{name, builtin}]
void list(JsonArray a);
// icon data as JSON {name, builtin, delay, frames:[ "RRGGBB..." x64 ]}
bool toJson(const String& name, JsonObject o);
// save custom icon; frames: array of hex strings (64 * 6 chars)
bool save(const String& name, uint16_t delayMs, JsonArrayConst frames, String& err);
bool remove(const String& name);
String sanitizeName(const String& n);
}
