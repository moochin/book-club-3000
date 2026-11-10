// Clock: NTP once Wi-Fi is up, Europe/London local time via a POSIX TZ rule.
#pragma once

#include <stdint.h>
#include <time.h>

#include "bc_date.h"

namespace clockmod {

void begin();
void tick(bool wifiUp);

bool valid();
time_t nowUtc();
bc::LocalNow localNow();

// "3 H AGO", "2 D AGO", "NEVER" for a past UTC epoch.
void formatAge(uint32_t epoch, char* buf, int len);
uint32_t ageSeconds(uint32_t epoch);

}  // namespace clockmod
