// Input: EC11 encoder on the hardware pulse counter plus its push switch, and
// the touch screen (a tap with its position, or a 2 s hold as a long press).
#pragma once

#include <stdint.h>

namespace input {

struct Events {
  int8_t turn = 0;          // detents since last poll (+ = clockwise)
  bool shortPress = false;  // released before the long-press time
  bool longPress = false;   // fired once, while still held, at 2 s
  bool tap = false;         // touch screen tapped at (x, y), screen coordinates
  int16_t x = 0, y = 0;
  bool any() const { return turn != 0 || shortPress || longPress || tap; }
};

void begin();
Events poll(uint32_t nowMs);

// True while the BOOT button (GPIO 0) is held.
bool bootHeld();

}  // namespace input
