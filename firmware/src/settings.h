// Per-unit settings and the cached data file, kept in NVS (Preferences).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

constexpr int kStatCount = 7;

enum Theme : uint8_t { kThemeGreen = 0, kThemeAmber = 1, kThemeCount };

struct Unit {
  char owner[24] = "";
  uint8_t theme = kThemeGreen;
  uint8_t stats[kStatCount] = {5, 5, 5, 5, 5, 5, 5};  // owner card, 1..10
  char dataUrl[160] = "";
  char token[128] = "";    // optional read-only GitHub token; never logged
  char otaPass[32] = "";   // optional ArduinoOTA password
  uint8_t rotation = 1;    // 0..3
  bool sound = true;
  bool autoBrightness = true;
  uint8_t fixedBrightness = 160;  // 0..255 when !autoBrightness
  bool effects = true;            // scanlines + sweep
  uint8_t nightPreset = 1;        // bc::nightPreset(): 0 never, 1 = 23:00-07:00 off
  uint8_t idlePreset = 2;         // bc::idleDimPresetMs(): 0 never, 2 = dim after 30 min
  char nudgeToken[64] = "";       // this unit's token for the nudge hub; never logged
};

void begin();
Unit& get();
void save();

// Owner name if set, else the chip id ("3F2A1C").
const char* displayName();
const char* chipId();

// Last good bookclub.json. Returns bytes copied (0 if none).
size_t loadCachedFile(char* buf, size_t cap);
void saveCachedFile(const char* data, size_t len);
// UTC epoch of the last successful sync (0 = never).
uint32_t lastSyncEpoch();
void setLastSyncEpoch(uint32_t epoch);

// Clears settings, cache and saved Wi-Fi, then restarts.
[[noreturn]] void factoryReset();

// The seven owner-card stat names.
const char* statName(int i);

}  // namespace settings
