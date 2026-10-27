#include "power.h"
#include <Arduino.h>
#include "settings.h"
#include "stats.h"

namespace {

constexpr int PIN_BAT = 9;              // battery / 2 (BSP: BSP_BATTERY_VOLTAGE_DIV)
constexpr uint32_t NAP_AFTER_BATTERY_MS = 30000;
constexpr uint32_t NAP_AFTER_USB_MS = 60000;
constexpr uint32_t OFF_AFTER_MS = 30000;   // on battery, without naps (reference: SCREEN_OFF_MS)
constexpr uint32_t OFF_AFTER_NAP_MS = 90000;
constexpr uint32_t WAKE_HOLD_MS = 12000;

int mv = 0, pct = -1;
bool usb = true;
uint32_t last_touch = 0, last_battery = 0, nap_start = 0, wake_hold = 0;
bool napping = false, off = false;

// LiPo resting voltage → charge, piecewise linear.
int lipo_pct(int v) {
  static const int16_t table[][2] = {{4200, 100}, {4100, 90}, {4000, 78}, {3900, 65}, {3800, 50},
                                     {3700, 33},  {3600, 18}, {3500, 8},  {3400, 3},  {3300, 0}};
  if (v >= table[0][0]) return 100;
  for (size_t i = 1; i < sizeof table / sizeof table[0]; i++)
    if (v >= table[i][0])
      return table[i][1] + (v - table[i][0]) * (table[i - 1][1] - table[i][1]) / (table[i - 1][0] - table[i][0]);
  return 0;
}

void read_battery() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT);
  const int now_mv = sum / 8 * 2;
  mv = mv ? (mv * 3 + now_mv) / 4 : now_mv;   // smooth out load spikes (WiFi, speaker)
  const bool no_battery = mv < 2500;
#if ARDUINO_USB_MODE
  const bool host = Serial.isPlugged();       // USB data lines see a host (not for plain chargers)
#else
  const bool host = bool(Serial);
#endif
  usb = host || no_battery || mv >= 4250;     // a charger keeps the cell above ~4.2 V
  pct = no_battery ? -1 : lipo_pct(mv);
}

uint8_t level() { return 255 * constrain(settings().brightness, 5, 100) / 100; }

}  // namespace

void power_begin() {
  read_battery();   // the first read sets the pin up as an ADC input (11 dB, 0..3.1 V, by default)
  last_touch = millis();
  power_apply_brightness();
}

void power_tick(bool keep_awake) {
  const uint32_t now = millis();
  if (now - last_battery > 5000) {
    last_battery = now;
    read_battery();
  }
  if (keep_awake) {
    if (napping || off) power_wake();
    last_touch = now;
    return;
  }
  const uint32_t idle = now - last_touch;
  if (!napping && !off && settings().nap && idle > (usb ? NAP_AFTER_USB_MS : NAP_AFTER_BATTERY_MS)) {
    napping = true;
    nap_start = now;
    display_backlight(usb ? 30 : 10);
  }
  if (!off && !usb && idle > (settings().nap ? OFF_AFTER_NAP_MS : OFF_AFTER_MS)) {
    off = true;
    display_backlight(0);
  }
}

int power_battery_pct() { return pct; }
int power_battery_mv() { return mv; }
bool power_on_usb() { return usb; }

bool power_touch() {
  const bool asleep = napping || off;
  power_wake();
  return asleep;
}

void power_wake() {
  const uint32_t now = millis();
  last_touch = now;
  if (!napping && !off) return;
  if (napping) {
    stats_on_nap_end((now - nap_start) / 1000);
    stats_on_wake();
    wake_hold = now + WAKE_HOLD_MS;
  }
  napping = off = false;
  power_apply_brightness();
}

bool power_napping() { return napping; }
bool power_screen_off() { return off; }
uint32_t power_idle_ms() { return millis() - last_touch; }
uint32_t power_wake_hold_until() { return wake_hold; }
void power_apply_brightness() { display_backlight(level()); }
