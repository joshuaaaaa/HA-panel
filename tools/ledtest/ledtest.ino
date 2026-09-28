// HA-Panel LED test for ESP32-S3
// Sends the same WS2812 signal to EVERY free GPIO at once (so it does not matter
// where the data wire is) and to the on-board RGB LED (GPIO48 / GPIO38).
// Colors cycle red -> green -> blue -> white (dim), one second each.
//  * on-board RGB LED changes color, panel stays dark -> power or wiring of the panel
//  * nothing changes at all                            -> the board does not run this firmware
#include <Arduino.h>
#include <soc/gpio_struct.h>
#include <xtensa/core-macros.h>

static const int PINS[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
                           18, 21, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 45, 46, 47, 48};
static const int NLEDS = 256;
static uint32_t mask0 = 0, mask1 = 0;  // GPIO 0-31, GPIO 32-48

static inline uint32_t cycles() { return XTHAL_GET_CCOUNT(); }

// bit-banged WS2812 (800 kHz) on all pins in parallel, CPU at 240 MHz
static void IRAM_ATTR sendAll(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t T0H = 96, T1H = 192, TP = 300;  // 0.4 / 0.8 / 1.25 us
  uint8_t grb[3] = {g, r, b};
  portDISABLE_INTERRUPTS();
  uint32_t t = cycles();
  for (int led = 0; led < NLEDS; led++) {
    for (int c = 0; c < 3; c++) {
      for (int bit = 7; bit >= 0; bit--) {
        uint32_t high = ((grb[c] >> bit) & 1) ? T1H : T0H;
        while (cycles() - t < TP) {
        }
        t = cycles();
        GPIO.out_w1ts = mask0;
        GPIO.out1_w1ts.val = mask1;
        while (cycles() - t < high) {
        }
        GPIO.out_w1tc = mask0;
        GPIO.out1_w1tc.val = mask1;
      }
    }
  }
  portENABLE_INTERRUPTS();
  delayMicroseconds(300);  // latch
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);
  for (int p : PINS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
    if (p < 32) mask0 |= 1UL << p;
    else mask1 |= 1UL << (p - 32);
  }
}

void loop() {
  static const uint8_t cols[4][3] = {{40, 0, 0}, {0, 40, 0}, {0, 0, 40}, {20, 20, 20}};
  static const char* names[4] = {"CERVENA", "ZELENA", "MODRA", "BILA"};
  for (int i = 0; i < 4; i++) {
    Serial.printf("LED test: %s na vsech pinech\n", names[i]);
    for (int k = 0; k < 10; k++) {  // repeat for 1 s
      sendAll(cols[i][0], cols[i][1], cols[i][2]);
      delay(100);
    }
  }
}
