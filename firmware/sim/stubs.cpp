// Stand-ins for the hardware-facing modules the UI calls into.
#include <stdio.h>
#include <time.h>
#include "Arduino.h"
#include "clock.h"
#include "settings.h"

SimEsp ESP;
uint32_t g_simMillis = 0;
uint32_t millis() { return g_simMillis; }

namespace settings {
const char* chipId() { return "3F2A1C"; }
const char* statName(int i) {
  static const char* const kNames[kStatCount] = {
      "GAMING SKILL",  "MOVIE TRIVIA", "BEER CAPACITY", "SNACK SUPPLY",
      "BANTER",        "PUNCTUALITY",  "STAYING POWER",
  };
  return (i >= 0 && i < kStatCount) ? kNames[i] : "?";
}
}  // namespace settings

namespace clockmod {
time_t g_simNow = 1795000000;  // fixed "now" for sync-age text
bool valid() { return true; }
time_t nowUtc() { return g_simNow; }
uint32_t ageSeconds(uint32_t e) { return e ? (uint32_t)(g_simNow - e) : UINT32_MAX; }
void formatAge(uint32_t epoch, char* buf, int len) {
  if (!epoch) { snprintf(buf, len, "NEVER"); return; }
  const uint32_t a = ageSeconds(epoch);
  if (a < 3600) snprintf(buf, len, "%u MIN AGO", a / 60);
  else if (a < 172800) snprintf(buf, len, "%u H AGO", a / 3600);
  else snprintf(buf, len, "%u D AGO", a / 86400);
}
}  // namespace clockmod
