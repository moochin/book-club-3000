#include "bc_health.h"

namespace bc {

Restart restartDue(const HealthInput& in) {
  if (in.busy) return Restart::None;
  // A wedged background task never recovers on its own: no nudges, no club
  // file, no updates. Restart once nobody's mid-use.
  if (in.stuckMs >= kStuckMs && in.idleMs >= kLowMemoryIdleMs) return Restart::Stuck;
  // Only if memory was fine earlier this run (so a restart restores it) and the
  // unit has been up a while: a unit that never has enough doesn't loop.
  if (in.lowMemoryMs >= kLowMemoryForMs && in.idleMs >= kLowMemoryIdleMs && in.hadMemory &&
      in.uptimeMs >= kLowMemoryMinUpMs) {
    return Restart::LowMemory;
  }
  if (in.idleMs < kDailyIdleMs) return Restart::None;
  if (in.now.valid) {
    const bool smallHours = in.now.minuteOfDay >= kDailyFromMinute && in.now.minuteOfDay < kDailyToMinute;
    if (smallHours && in.uptimeMs >= kDailyAfterMs) return Restart::Daily;
  } else if (in.uptimeMs >= kNoClockAfterMs) {
    return Restart::Daily;
  }
  return Restart::None;
}

}  // namespace bc
