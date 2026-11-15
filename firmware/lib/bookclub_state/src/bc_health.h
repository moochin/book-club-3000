// When a unit should restart itself to stay healthy: a quiet daily restart
// in the small hours, and one when memory has been too fragmented for a TLS
// connection for a while. Pure, so it's tested on the host; src/diag acts on it.
#pragma once

#include <stdint.h>

#include "bc_date.h"

namespace bc {

enum class Restart : uint8_t { None, Daily, LowMemory };

struct HealthInput {
  uint32_t uptimeMs = 0;
  uint32_t idleMs = 0;          // since the last knob turn, press or tap
  uint32_t lowMemoryMs = 0;     // how long TLS has been impossible (0: it isn't)
  bool hadMemory = false;       // TLS was possible earlier this run: a restart helps
  bool busy = false;            // a firmware update or the setup portal
  LocalNow now;
};

constexpr uint32_t kDailyAfterMs = 20UL * 60 * 60 * 1000;     // up at least this long
constexpr uint32_t kDailyIdleMs = 10UL * 60 * 1000;           // nobody using it
constexpr int kDailyFromMinute = 3 * 60;                      // 03:00..04:59 local
constexpr int kDailyToMinute = 5 * 60;
constexpr uint32_t kNoClockAfterMs = 48UL * 60 * 60 * 1000;   // no clock: every 2 days
constexpr uint32_t kLowMemoryForMs = 10UL * 60 * 1000;
constexpr uint32_t kLowMemoryIdleMs = 60UL * 1000;
constexpr uint32_t kLowMemoryMinUpMs = 30UL * 60 * 1000;  // never a restart loop

Restart restartDue(const HealthInput& in);

}  // namespace bc
