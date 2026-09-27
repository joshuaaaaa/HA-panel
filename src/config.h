#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define FW_NAME "HA-Panel"
#define FW_VERSION "1.0.0"

#define MAX_PAGES 24
#define NUM_INDICATORS 3
#define NUM_BUTTONS 3
#define MAX_LEDS 1024

#ifndef DEFAULT_LED_PIN
#define DEFAULT_LED_PIN 16
#endif

enum PageType : uint8_t {
  PT_CLOCK = 0,
  PT_DATE,
  PT_ENTITY,
  PT_TEMPLATE,
  PT_TEXT,
  PT_EFFECT,
  PT_COUNT
};

extern const char* const PAGE_TYPE_NAMES[PT_COUNT];

// Colors: -1 = "use default"
struct PageCfg {
  String name;
  uint8_t type = PT_CLOCK;
  bool enabled = true;
  String icon;          // static icon name ("" = none)
  String entity;        // PT_ENTITY: entity_id
  int8_t decimals = -1; // PT_ENTITY: -1 = raw state
  String unit;          // PT_ENTITY: "" = unit from HA, "-" = no unit
  String text;          // PT_TEXT: static text, PT_TEMPLATE: Jinja template, PT_EFFECT: overlay text
  String colorTpl;      // template -> "#RRGGBB" / "r,g,b" / name
  String iconTpl;       // template -> icon name
  String visibleTpl;    // template -> true/false (page shown only when true)
  String progressTpl;   // template -> 0..100 (progress bar)
  int32_t color = -1;
  int32_t progressColor = -1;
  uint16_t duration = 8; // seconds
  String effect;         // PT_EFFECT: effect name; others: background effect
  bool rainbow = false;
  uint8_t style = 0;     // clock: 0 = small font, 1 = big font
  String action;         // button action: "", "toggle", "service"
  String actionEntity;   // entity for action ("" = page entity)
  String actionService;  // "domain.service" for action == "service"
};

struct Config {
  // --- network / system
  String hostname = "hapanel";
  String wifiSsid, wifiPass;
  bool staticIp = false;
  String ip, gateway, subnet = "255.255.255.0", dns;
  String apPass = "hapanel1";
  String webUser = "admin", webPass;

  // --- Home Assistant (WebSocket API)
  bool haEnabled = false;
  String haUrl = "http://homeassistant.local:8123";
  String haToken;

  // --- MQTT
  bool mqttEnabled = false;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser, mqttPass;
  String mqttTopic;               // base topic, default hapanel/<id>
  bool discovery = true;
  String discoveryPrefix = "homeassistant";

  // --- matrix hardware
  int8_t ledPin = DEFAULT_LED_PIN;
  uint8_t width = 32, height = 8;
  bool vertical = true, serpentine = true, startRight = false, startBottom = false;
  String colorOrder = "GRB";
  uint16_t maxCurrent = 2000;     // mA, 0 = unlimited
  bool gamma = true;

  // --- display
  bool power = true;
  uint8_t brightness = 40;
  bool autoBright = false;
  int8_t ldrPin = -1;
  bool ldrInvert = false;
  uint8_t minBright = 4, maxBright = 160;
  int32_t textColor = 0xFFFFFF;
  uint8_t scrollSpeed = 25;       // px/s
  uint8_t transition = 1;         // 0 none, 1 slide left, 2 slide up, 3 fade
  uint16_t transitionMs = 400;
  int8_t textY = 1;
  bool autoRotate = true;
  bool uppercase = false;

  // --- time
  String ntp = "pool.ntp.org";
  String tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  bool h24 = true;
  bool showSeconds = false;
  bool blinkColon = true;
  bool weekdayBar = true;
  bool mondayFirst = true;
  uint8_t dateFormat = 0;         // 0 DD.MM. 1 DD.MM.YY 2 MM/DD 3 YYYY-MM-DD 4 D.M.
  int32_t weekdayColor = 0x404040, weekdayActive = 0xFFFFFF;

  // --- night mode
  bool nightEnabled = false;
  String nightStart = "22:00", nightEnd = "06:00";
  uint8_t nightBright = 3;
  bool nightClockOnly = true;
  int32_t nightColor = 0xFF0000;  // -1 = keep colors

  // --- buttons
  int8_t btnPins[NUM_BUTTONS] = {-1, -1, -1};
  bool btnActiveLow = true;

  // --- indicators (right edge), HA templates returning a color
  String indTpl[NUM_INDICATORS];

  // --- pages
  PageCfg pages[MAX_PAGES];
  uint8_t pageCount = 0;
};

extern Config cfg;
extern String deviceId;   // e.g. "a1b2c3"

// global recursive lock shared by render task, web server and network loop
extern SemaphoreHandle_t gLock;
struct Lock {
  Lock() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(gLock); }
};

void configInitDefaults();
bool configLoad();
bool configSave();
bool pagesLoad();
bool pagesSave();

void configToJson(JsonObject o, bool withSecrets);
// returns bitmask of what changed (CFG_CHG_*)
uint32_t configFromJson(JsonObjectConst o);
void pagesToJson(JsonArray a);
void pagesFromJson(JsonArrayConst a);
void pageToJson(const PageCfg& p, JsonObject o);
void pageFromJson(PageCfg& p, JsonObjectConst o);

enum : uint32_t {
  CFG_CHG_REBOOT = 1,   // hardware / network change -> reboot
  CFG_CHG_HA = 2,
  CFG_CHG_MQTT = 4,
  CFG_CHG_TIME = 8,
  CFG_CHG_DISPLAY = 16,
};

// color helpers
int32_t parseColor(const String& s, int32_t def = -1);
String colorToHex(int32_t c);

// request flags processed by the main loop
extern volatile bool reqSaveConfig, reqSavePages, reqReboot, reqHaResync, reqMqttRediscover, reqMqttState, reqSelect;
