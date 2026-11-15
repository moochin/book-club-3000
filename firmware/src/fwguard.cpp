#include "fwguard.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <string.h>

#include "bc_state.h"
#include "config.h"

// Arduino's start-up asks this (a weak hook in esp32-hal-misc.c): true means
// "don't mark this image valid now, the app will decide". fwguard does.
extern "C" bool verifyRollbackLater() { return true; }

namespace fwguard {

namespace {

constexpr const char* kNs = "fwguard";
constexpr uint32_t kProveUpMs = 10UL * 60 * 1000;    // run this long without a restart...
constexpr uint32_t kGiveUpWifiMs = 30UL * 60 * 1000; // ...but can't reach GitHub in this much Wi-Fi
constexpr uint8_t kSkipAfter = 2;                    // failed probations before skipping a version

bool g_probation = false;
uint32_t g_syncOkAtBoot = 0;
bool g_syncCounted = false;
uint32_t g_wifiMs = 0, g_lastTickMs = 0;
char g_bad[16] = "";   // the version that last failed probation
uint8_t g_badCount = 0;
char g_note[40] = "";

void saveStr(const char* key, const char* v) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putString(key, v);
  p.end();
}

}  // namespace

void begin() {
  Preferences p;
  char trial[16] = "";
  if (p.begin(kNs, true)) {
    p.getString("trial", trial, sizeof trial);
    p.getString("bad", g_bad, sizeof g_bad);
    g_badCount = p.getUChar("badn", 0);
    p.end();
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running) esp_ota_get_state_partition(running, &state);
  g_probation = state == ESP_OTA_IMG_PENDING_VERIFY;

  if (!g_probation && trial[0] && strcmp(trial, BC_FW_VERSION) != 0) {
    // We were trying `trial`, and we're running something else: it was
    // rolled back. Count it, so a bad release isn't retried forever.
    if (strcmp(g_bad, trial) == 0) {
      if (g_badCount < 255) ++g_badCount;
    } else {
      strlcpy(g_bad, trial, sizeof g_bad);
      g_badCount = 1;
    }
    if (p.begin(kNs, false)) {
      p.putString("bad", g_bad);
      p.putUChar("badn", g_badCount);
      p.putString("trial", "");
      p.end();
    }
    Serial.printf("[fwguard] %s was rolled back (%u time%s)\n", g_bad, g_badCount, g_badCount == 1 ? "" : "s");
  } else if (g_probation && strcmp(trial, BC_FW_VERSION) != 0) {
    saveStr("trial", BC_FW_VERSION);  // installed some other way (e.g. over the LAN)
  } else if (!g_probation && trial[0]) {
    saveStr("trial", "");
  }
  if (g_probation) Serial.printf("[fwguard] %s on probation\n", BC_FW_VERSION);
}

void tick(uint32_t nowMs, bool wifiUp, uint32_t syncOkCount) {  // = reachedCount
  if (!g_probation) return;
  if (!g_syncCounted) {  // the first call: what sync had done before us
    g_syncOkAtBoot = syncOkCount;
    g_syncCounted = true;
    g_lastTickMs = nowMs;
  }
  if (wifiUp) g_wifiMs += nowMs - g_lastTickMs;
  g_lastTickMs = nowMs;

  if (syncOkCount > g_syncOkAtBoot && nowMs >= kProveUpMs) {
    esp_ota_mark_app_valid_cancel_rollback();
    g_probation = false;
    saveStr("trial", "");
    // A good build at or past the one that failed: that failure is history
    // (we never install an older version, so its skip no longer matters).
    if (g_bad[0] && bc::compareVersions(BC_FW_VERSION, g_bad) >= 0) {
      Preferences p;
      if (p.begin(kNs, false)) {
        p.putString("bad", "");
        p.putUChar("badn", 0);
        p.end();
      }
      g_bad[0] = 0;
      g_badCount = 0;
    }
    Serial.printf("[fwguard] %s confirmed\n", BC_FW_VERSION);
    return;
  }
  if (g_wifiMs >= kGiveUpWifiMs) {
    Serial.printf("[fwguard] %s can't reach GitHub: rolling back\n", BC_FW_VERSION);
    delay(50);
    esp_ota_mark_app_invalid_rollback_and_reboot();  // doesn't return
  }
}

bool onProbation() { return g_probation; }

const char* note(uint32_t nowMs) {
  if (g_probation) {
    snprintf(g_note, sizeof g_note, "ON PROBATION %lu MIN", (unsigned long)(nowMs / 60000));
  } else if (g_bad[0] && g_badCount) {
    snprintf(g_note, sizeof g_note, "%s ROLLED BACK%s", g_bad, g_badCount >= kSkipAfter ? ", SKIPPED" : "");
  } else {
    g_note[0] = 0;
  }
  return g_note;
}

bool skipVersion(const char* version) {
  return g_badCount >= kSkipAfter && strcmp(g_bad, version) == 0;
}

void installing(const char* version) { saveStr("trial", version); }

}  // namespace fwguard
