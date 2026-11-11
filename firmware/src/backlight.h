// Backlight: auto-dimming from the light sensor, and the screen-power policy
// (bc_screen.h): off at night and in a dark room, dimmed when idle.
#pragma once

#include <stdint.h>

#include "bc_date.h"

namespace backlight {

void begin();
// `activity` = the knob or touch screen was used this tick (wakes the screen).
// `now` is local time, for the night window.
void tick(uint32_t nowMs, bool activity, const bc::LocalNow& now);

bool blanked();
// Keeps the screen awake (e.g. while the setup portal is open).
void holdAwake(bool hold);

// 0 (dark) .. 1000 (bright), smoothed; and the latest raw ADC reading.
int lightLevel();
int rawReading();

}  // namespace backlight
