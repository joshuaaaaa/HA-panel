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
void requestEntities();
JobState entitiesState();
const String& entitiesData();   // "entity_id\tfriendly name\n" lines
void requestRender(const String& tpl);
JobState renderState();
String renderResult();
void statusJson(JsonObject o);
}
