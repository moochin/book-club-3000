#include "clock.h"

#include <Arduino.h>
#include <stdio.h>

namespace clockmod {

static bool g_started = false;

void begin() {
  // Set the zone now, so localtime() is right as soon as SNTP lands.
  setenv("TZ", bc::kLondonPosixTz, 1);
  tzset();
}

void tick(bool wifiUp) {
  if (wifiUp && !g_started) {
    // SNTP keeps re-syncing in the background (hourly by default).
    configTzTime(bc::kLondonPosixTz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    g_started = true;
  }
}

time_t nowUtc() { return time(nullptr); }
bool valid() { return nowUtc() >= bc::kMinValidEpoch; }
bc::LocalNow localNow() { return bc::localNowFromEpoch(nowUtc()); }

uint32_t ageSeconds(uint32_t epoch) {
  if (!epoch || !valid()) return UINT32_MAX;
  const time_t now = nowUtc();
  return now > (time_t)epoch ? (uint32_t)(now - epoch) : 0;
}

void formatAge(uint32_t epoch, char* buf, int len) {
  if (!epoch) {
    snprintf(buf, (size_t)len, "NEVER");
    return;
  }
  const uint32_t a = ageSeconds(epoch);
  if (a == UINT32_MAX) snprintf(buf, (size_t)len, "?");
  else if (a < 120) snprintf(buf, (size_t)len, "JUST NOW");
  else if (a < 3600) snprintf(buf, (size_t)len, "%lu MIN AGO", (unsigned long)(a / 60));
  else if (a < 172800) snprintf(buf, (size_t)len, "%lu H AGO", (unsigned long)(a / 3600));
  else snprintf(buf, (size_t)len, "%lu D AGO", (unsigned long)(a / 86400));
}

}  // namespace clockmod
