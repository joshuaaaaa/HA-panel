#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace HA {
void begin();
void loop();
void reconfigure();   // URL/token changed -> reconnect
void resubscribe();   // pages/templates changed
bool connected();
String stateText();   // human readable connection state
bool callService(const String& domain, const String& service, const String& entity);

// background jobs executed from loop() (HTTP REST API)
enum JobState : uint8_t { JOB_IDLE, JOB_PENDING, JOB_RUNNING, JOB_DONE, JOB_ERROR };
// entity search executed in HA: key "*" = domains ("domain\tcount" lines),
// "<domain>|<text>" = up to 60 matches ("entity_id\tname\tstate\tunit" lines)
void requestEntities(const String& key);
JobState entitiesState(const String& key);
const String& entitiesData();
void requestRender(const String& tpl);
JobState renderState();
String renderResult();
void statusJson(JsonObject o);
}
