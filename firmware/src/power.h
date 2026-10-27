// Battery, USB power, backlight, and the idle path: awake → nap (dimmed, pet sleeps, counts nap
// time) → screen off on battery. A touch wakes the screen and is swallowed.
#pragma once
#include <stdint.h>

void power_begin();
void power_tick(bool keep_awake);   // keep_awake: an approval is waiting, voice is on, ...
int power_battery_pct();            // -1 when no battery is connected
int power_battery_mv();
bool power_on_usb();
bool power_touch();                 // call on every touch; true = it only woke the screen
void power_wake();                  // e.g. a prompt arrived
bool power_napping();
bool power_screen_off();
uint32_t power_idle_ms();
uint32_t power_wake_hold_until();   // after waking, the pet keeps sleeping a bit (reference: 12 s)
void power_apply_brightness();      // Settings slider

// Implemented in main.cpp (owns the display).
void display_backlight(uint8_t level);   // 0..255
