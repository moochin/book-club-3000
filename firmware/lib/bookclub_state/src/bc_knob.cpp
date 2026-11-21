#include "bc_knob.h"

namespace bc {

int knobTurns(KnobFilter& f, int64_t count, bool pressed, uint32_t nowMs, int countsPerDetent,
              uint32_t lockoutMs) {
  if (!f.started) {  // wherever the knob sits at power-on is a resting click
    f.ref = count;
    f.started = true;
  }
  if (pressed) {
    f.quiet = true;
    f.quietUntilMs = nowMs + lockoutMs;
  } else if (f.quiet && (int32_t)(nowMs - f.quietUntilMs) >= 0) {
    f.quiet = false;
  }
  if (countsPerDetent < 1) countsPerDetent = 1;
  const int64_t clicks = (count - f.ref) / countsPerDetent;  // truncates toward zero: a dead zone both ways
  if (clicks == 0) return 0;
  f.ref += clicks * countsPerDetent;
  if (f.quiet) return 0;
  if (clicks > 100) return 100;
  if (clicks < -100) return -100;
  return (int)clicks;
}

}  // namespace bc
