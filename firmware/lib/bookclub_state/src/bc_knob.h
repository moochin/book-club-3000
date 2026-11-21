// Knob filtering: turns the encoder's raw count into whole-click steps.
//
// Pure (no hardware), so it is unit-tested on the PC. Two guards against the
// knob wobbling when it is pressed (the push switch is in the shaft, so an
// off-centre press nudges it):
//   - a dead zone: a step needs a full click (countsPerDetent) from where the
//     last step left the knob, so a wobble of a count or two either way, and
//     back, is never a turn;
//   - a press lockout: no turns while the switch is down, nor for lockoutMs
//     after it is let go. Whole clicks made meanwhile are absorbed, so the
//     dead zone stays lined up with the knob's resting positions.
#pragma once

#include <stdint.h>

namespace bc {

struct KnobFilter {
  int64_t ref = 0;          // the count at the knob's last resting click
  bool started = false;
  bool quiet = false;       // in the press lockout
  uint32_t quietUntilMs = 0;
};

// Steps (+ or -) since the last call. `pressed` is the switch's raw state.
int knobTurns(KnobFilter& f, int64_t count, bool pressed, uint32_t nowMs, int countsPerDetent,
              uint32_t lockoutMs);

}  // namespace bc
