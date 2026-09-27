#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Mqtt {
void begin();
void loop();
void reconfigure();
bool connected();
void publishState();      // light/page/switch states (called on change)
void publishDiscovery();
void publishButton(int idx, bool pressed);
void statusJson(JsonObject o);
}
