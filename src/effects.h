#pragma once
#include <ArduinoJson.h>
#include "display.h"

namespace Effects {
// renders effect into canvas; returns false if unknown effect
bool render(Canvas& c, const String& name, uint32_t ms);
void list(JsonArray a);
}
