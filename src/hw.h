#pragma once
#include <Arduino.h>

namespace Hw {
void begin();
void loop();
int ldrRaw();
}

namespace Net {
void begin();
void loop();
bool apMode();
String ip();
void scanStart();
int scanState();  // -2 idle, -1 running, >=0 result count
void applyTime();  // (re)configure NTP + time zone from cfg
}
