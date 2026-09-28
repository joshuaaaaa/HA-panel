#pragma once
#include <ArduinoJson.h>
#include "display.h"

// Procedurally rendered, anti-aliased animated eyes for the 32x8 matrix.
// Effect names start with "eyes" (eyes, eyes_angry, eyes_evil, ...).
namespace Eyes {
bool render(Canvas& c, const String& name, uint32_t ms);
void list(JsonArray a);
}
