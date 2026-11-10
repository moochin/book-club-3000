// Backlight: auto-dimming from the light sensor, a readable floor, and
// blanking after 10 minutes in a dark room.
#pragma once

#include <stdint.h>

namespace backlight {

void begin();
// `activity` = the knob was used this tick (wakes the screen, resets blanking).
void tick(uint32_t nowMs, bool activity);

bool blanked();
// Keeps the screen awake (e.g. while the setup portal is open).
void holdAwake(bool hold);

// 0 (dark) .. 1000 (bright), smoothed; and the latest raw ADC reading.
int lightLevel();
int rawReading();

}  // namespace backlight
