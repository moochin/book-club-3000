#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <nvs_flash.h>
#include <string.h>

#include "config.h"

namespace settings {

static Unit g_unit;
static char g_chipId[8];
static SemaphoreHandle_t g_cacheMutex;

static const char* kNs = "unit";
static const char* kCacheNs = "cache";

static void loadStr(Preferences& p, const char* key, char* dst, size_t cap, const char* def) {
  String v = p.getString(key, def);
  strlcpy(dst, v.c_str(), cap);
}

void begin() {
  g_cacheMutex = xSemaphoreCreateMutex();
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(g_chipId, sizeof g_chipId, "%02X%02X%02X", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 40));

  Preferences p;
  p.begin(kNs, true);
  Unit& u = g_unit;
  loadStr(p, "owner", u.owner, sizeof u.owner, "");
  u.theme = p.getUChar("theme", kThemeGreen) % kThemeCount;
  if (p.getBytesLength("stats") == kStatCount) p.getBytes("stats", u.stats, kStatCount);
  loadStr(p, "url", u.dataUrl, sizeof u.dataUrl, cfg::kDefaultDataUrl);
  loadStr(p, "token", u.token, sizeof u.token, "");
  loadStr(p, "otapass", u.otaPass, sizeof u.otaPass, "");
  u.rotation = p.getUChar("rot", cfg::kDefaultRotation) & 3;
  u.sound = p.getBool("sound", true);
  u.autoBrightness = p.getBool("autobr", true);
  u.fixedBrightness = p.getUChar("fixbr", 160);
  u.effects = p.getBool("fx", true);
  p.end();
  for (auto& s : u.stats) s = constrain(s, 1, 10);
}

Unit& get() { return g_unit; }

void save() {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  const Unit& u = g_unit;
  p.putString("owner", u.owner);
  p.putUChar("theme", u.theme);
  p.putBytes("stats", u.stats, kStatCount);
  p.putString("url", u.dataUrl);
  p.putString("token", u.token);
  p.putString("otapass", u.otaPass);
  p.putUChar("rot", u.rotation);
  p.putBool("sound", u.sound);
  p.putBool("autobr", u.autoBrightness);
  p.putUChar("fixbr", u.fixedBrightness);
  p.putBool("fx", u.effects);
  p.end();
}

const char* chipId() { return g_chipId; }
const char* displayName() { return g_unit.owner[0] ? g_unit.owner : g_chipId; }

size_t loadCachedFile(char* buf, size_t cap) {
  xSemaphoreTake(g_cacheMutex, portMAX_DELAY);
  Preferences p;
  size_t n = 0;
  if (p.begin(kCacheNs, true)) {
    const size_t len = p.getBytesLength("file");
    if (len > 0 && len < cap) n = p.getBytes("file", buf, len);
    p.end();
  }
  xSemaphoreGive(g_cacheMutex);
  buf[n < cap ? n : cap - 1] = '\0';
  return n;
}

void saveCachedFile(const char* data, size_t len) {
  xSemaphoreTake(g_cacheMutex, portMAX_DELAY);
  Preferences p;
  if (p.begin(kCacheNs, false)) {
    p.putBytes("file", data, len);
    p.end();
  }
  xSemaphoreGive(g_cacheMutex);
}

uint32_t lastSyncEpoch() {
  Preferences p;
  p.begin(kCacheNs, true);
  const uint32_t v = p.getULong("synced", 0);
  p.end();
  return v;
}

void setLastSyncEpoch(uint32_t epoch) {
  Preferences p;
  if (!p.begin(kCacheNs, false)) return;
  p.putULong("synced", epoch);
  p.end();
}

void factoryReset() {
  Serial.println("[settings] factory reset");
  WiFi.disconnect(true, true);  // forget saved network
  delay(200);
  nvs_flash_erase();
  nvs_flash_init();
  delay(200);
  ESP.restart();
  while (true) {
  }
}

const char* statName(int i) {
  static const char* const kNames[kStatCount] = {
      "GAMING SKILL",  "MOVIE TRIVIA", "BEER CAPACITY", "SNACK SUPPLY",
      "BANTER",        "PUNCTUALITY",  "STAYING POWER",
  };
  return (i >= 0 && i < kStatCount) ? kNames[i] : "?";
}

}  // namespace settings
