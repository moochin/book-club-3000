// Diagnostics that survive a restart, so a unit far from a USB cable can say
// what went wrong: which task held up a core when the watchdog fired, and how
// the memory the TLS connections need is holding up.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace diag {

void begin();  // first thing in setup(): reads (and clears) what was recorded

// Why the unit last started, for ABOUT: "POWER ON", "CRASH",
// "WATCHDOG: sync (UP 42 MIN)"... `bad` is set for crash, watchdog, brownout.
const char* startedBy(bool& bad);

// "142K FREE, LOW 96K, BLOCK 60K": free heap now, the least since start-up,
// and the largest free block (a TLS connection needs a big one).
void memoryText(char* buf, size_t cap);

}  // namespace diag
