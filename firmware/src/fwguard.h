// Firmware probation: the safety net for over-the-air updates, which are the
// only way into a unit once it has shipped.
//
// The bootloader can roll back to the previous firmware, but Arduino throws
// that away by marking every image good the moment it starts. We take that
// decision ourselves: a new image stays on probation (ESP-IDF's
// PENDING_VERIFY) until it has proved it can still update itself (GitHub
// answered it over HTTPS) and has run 10 minutes without a
// restart. Any restart before then (crash, watchdog, freeze, power cut)
// makes the bootloader boot the previous image instead. An image that runs
// but can't reach GitHub for 30 minutes of working Wi-Fi rolls itself back.
//
// A version that fails probation twice is skipped until the club file names
// a different one, so a bad release can't loop install, fail, install...
#pragma once

#include <stdint.h>

namespace fwguard {

void begin();  // early in setup(), after settings::begin()

// Call every loop: confirms or rolls back an image on probation.
// `reachedCount`: datasync::reachedCount().
void tick(uint32_t nowMs, bool wifiUp, uint32_t reachedCount);

bool onProbation();

// For ABOUT's FIRMWARE line: "ON PROBATION 3 MIN", "1.6.0 ROLLED BACK", or "".
const char* note(uint32_t nowMs);

// Should sync skip installing `version`? (It failed probation twice.)
bool skipVersion(const char* version);

// Sync calls this just before restarting into a freshly installed `version`.
void installing(const char* version);

}  // namespace fwguard
