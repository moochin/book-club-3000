// Diagnostics that survive a restart, so a unit far from a USB cable can say
// what went wrong: which task held up a core when the watchdog fired, and how
// the memory the TLS connections need is holding up.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bc_date.h"

namespace diag {

void begin();  // first thing in setup(): reads (and clears) what was recorded

// Why the unit last started, for ABOUT: "POWER ON", "CRASH",
// "WATCHDOG: sync (UP 42 MIN)"... `bad` is set for crash, watchdog, brownout.
const char* startedBy(bool& bad);

// "142K FREE, LOW 96K, BLOCK 60K": free heap now, the least since start-up,
// and the largest free block (a TLS connection needs a big one).
void memoryText(char* buf, size_t cap);

// This start was a planned restart (daily, or to heal low memory): start
// quietly, without the boot beeps.
bool quietStart();

// Self-healing (see bc_health.h): call every loop. Restarts the unit, quietly,
// in the small hours once a day, or when memory has been too fragmented for
// TLS for a while. `busy`: an update or the setup portal is running.
// `stuckMs`: the longest any background fetch has been running.
void tick(uint32_t nowMs, uint32_t idleMs, const bc::LocalNow& now, bool busy, uint32_t stuckMs);

}  // namespace diag
