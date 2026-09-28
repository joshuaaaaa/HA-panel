#pragma once
#include <Arduino.h>
#include <vector>

// Minimal PNG encoder (RGB 8 bit, uncompressed deflate blocks) - enough for the
// tiny 32x8 screen image published to Home Assistant as an MQTT image entity.
void encodePng(const uint8_t* rgb, int w, int h, std::vector<uint8_t>& out);
