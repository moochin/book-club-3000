#include "sync.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>

#include <memory>

#include "bc_state.h"
#include "clock.h"
#include "config.h"
#include "settings.h"

// The full Mozilla root CA bundle, already compiled into the core's mbedTLS
// library. Using it means GitHub's certificates validate without pinning.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace datasync {

static TaskHandle_t g_task;
static SemaphoreHandle_t g_mutex;
static volatile Status g_status = Status::Idle;
static char g_error[64] = "";
static bool g_hasNew = false;
static std::unique_ptr<bc::BookClubData> g_new;
static volatile int g_updatePct = 0;

static bool g_bootFetchDone = false;
static uint32_t g_nextFetchMs = 0;
static int g_failCount = 0;
static bool g_scheduled = false;
static char g_failedFwVersion[bc::kVersionLen] = "";
// Bumped by the task each time a fetch finishes; the main loop reschedules.
static volatile uint32_t g_doneCount = 0;
static volatile bool g_lastOk = false;
static volatile bool g_needStamp = false;  // synced before the clock was set

static void setError(const char* fmt, ...) {
  char buf[sizeof g_error];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strlcpy(g_error, buf, sizeof g_error);
  xSemaphoreGive(g_mutex);
  Serial.printf("[sync] %s\n", buf);
}

static void secureClient(WiFiClientSecure& c) {
  c.setCACertBundle(x509_crt_bundle_start);
  c.setTimeout(cfg::kHttpTimeoutMs / 1000);
  c.setHandshakeTimeout(cfg::kHttpTimeoutMs / 1000);
}

// Downloads at most bc::kMaxFileBytes into buf. Returns length or -1.
static int download(const char* url, const char* token, char* buf, size_t cap) {
  if (!url[0]) {
    setError("no data URL: set it on the setup page");
    return -1;
  }
  WiFiClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.setTimeout(cfg::kHttpTimeoutMs);
  http.setConnectTimeout(cfg::kHttpTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("BookClub3000/" BC_FW_VERSION);
  if (!http.begin(client, url)) {
    setError("bad URL");
    return -1;
  }
  if (token && token[0]) {
    String auth = String("Bearer ") + token;
    http.addHeader("Authorization", auth);
  }
  http.addHeader("Cache-Control", "no-cache");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code < 0) setError("fetch failed: %s", HTTPClient::errorToString(code).c_str());
    else setError("HTTP %d%s", code, (code == 404 && !(token && token[0])) ? " (check the data URL)" : "");
    http.end();
    return -1;
  }
  const int declared = http.getSize();
  if (declared > (int)cap - 1) {
    setError("file too large (%d bytes)", declared);
    http.end();
    return -1;
  }
  WiFiClient* stream = http.getStreamPtr();
  size_t n = 0;
  const uint32_t start = millis();
  while (http.connected() && (declared < 0 || n < (size_t)declared)) {
    const int avail = stream->available();
    if (avail > 0) {
      if (n + (size_t)avail > cap - 1) {
        setError("file too large");
        http.end();
        return -1;
      }
      n += stream->readBytes(buf + n, (size_t)avail);
    } else {
      if (millis() - start > cfg::kHttpTimeoutMs) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  if (declared >= 0 && n != (size_t)declared) {
    setError("download cut short");
    return -1;
  }
  buf[n] = 0;
  return (int)n;
}

static void maybeUpdateFirmware(const bc::FirmwareInfo& fw) {
#if defined(BC_PANEL_ST7789)
  const char* url = fw.urlFor(true);
#else
  const char* url = fw.urlFor(false);
#endif
  if (!url) return;
  if (bc::compareVersions(fw.version, BC_FW_VERSION) <= 0) return;
  if (strcmp(fw.version, g_failedFwVersion) == 0) return;  // tried already this boot
  Serial.printf("[sync] firmware %s available (running %s)\n", fw.version, BC_FW_VERSION);
  g_status = Status::Updating;
  g_updatePct = 0;
  WiFiClientSecure client;
  secureClient(client);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // GitHub release assets redirect
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.onProgress([](int cur, int total) {
    if (total > 0) g_updatePct = cur * 100 / total;
  });
  const t_httpUpdate_return r = httpUpdate.update(client, url);
  // Only reached on failure (success reboots).
  strlcpy(g_failedFwVersion, fw.version, sizeof g_failedFwVersion);
  setError("update %s failed: %s", fw.version,
           r == HTTP_UPDATE_NO_UPDATES ? "no update" : httpUpdate.getLastErrorString().c_str());
}

static void runFetch() {
  g_status = Status::Fetching;
  const settings::Unit& u = settings::get();
  std::unique_ptr<char[]> buf(new (std::nothrow) char[bc::kMaxFileBytes + 1]);
  std::unique_ptr<bc::BookClubData> data(new (std::nothrow) bc::BookClubData());
  if (!buf || !data) {
    setError("out of memory");
    g_status = Status::Failed;
    return;
  }
  const int n = download(u.dataUrl, u.token, buf.get(), bc::kMaxFileBytes + 1);
  if (n < 0) {
    g_status = Status::Failed;
    return;
  }
  const bc::ParseError err = bc::parseBookClub(buf.get(), (size_t)n, *data);
  if (err != bc::ParseError::None) {
    // Keep the cached copy: a bad edit must never blank the unit.
    setError("rejected file: %s", bc::parseErrorText(err));
    g_status = Status::Failed;
    return;
  }
  settings::saveCachedFile(buf.get(), (size_t)n);
  if (clockmod::valid()) settings::setLastSyncEpoch((uint32_t)clockmod::nowUtc());
  else g_needStamp = true;
  const bc::FirmwareInfo fw = data->firmware;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_new = std::move(data);
  g_hasNew = true;
  g_error[0] = 0;
  xSemaphoreGive(g_mutex);
  g_status = Status::Ok;
  Serial.printf("[sync] ok, %d bytes\n", n);
  maybeUpdateFirmware(fw);
  if (g_status == Status::Updating) g_status = Status::Failed;
}

static void taskMain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (WiFi.isConnected()) {
      runFetch();
    } else {
      setError("no Wi-Fi");
      g_status = Status::Failed;
    }
    g_lastOk = g_status == Status::Ok;
    g_doneCount = g_doneCount + 1;
  }
}

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  // TLS needs a deep stack. Core 0, alongside the Wi-Fi stack; the UI runs on core 1.
  xTaskCreatePinnedToCore(taskMain, "sync", 12288, nullptr, 1, &g_task, 0);
}

void requestNow() {
  if (g_status == Status::Fetching || g_status == Status::Updating) return;
  xTaskNotifyGive(g_task);
}

static uint32_t jitter() { return esp_random() % cfg::kSyncJitterMs; }

void tick(uint32_t now, bool wifiUp) {
  static uint32_t seenDone = 0;
  if (g_doneCount != seenDone) {
    // A fetch just finished: schedule the next one.
    seenDone = g_doneCount;
    if (g_lastOk) {
      g_failCount = 0;
      g_nextFetchMs = now + cfg::kSyncIntervalMs + jitter();
    } else {
      const int n = sizeof cfg::kSyncRetryMs / sizeof cfg::kSyncRetryMs[0];
      g_nextFetchMs = now + cfg::kSyncRetryMs[g_failCount < n ? g_failCount : n - 1];
      if (g_failCount < n) ++g_failCount;
    }
    g_scheduled = true;
  }
  if (g_needStamp && clockmod::valid()) {
    g_needStamp = false;
    settings::setLastSyncEpoch((uint32_t)clockmod::nowUtc());
  }
  const Status s = g_status;
  if (!wifiUp || s == Status::Fetching || s == Status::Updating) return;
  if (!g_bootFetchDone) {
    // First fetch as soon as Wi-Fi is up. Give SNTP a moment so the
    // "last synced" stamp is real.
    static uint32_t upSince = 0;
    if (!upSince) upSince = now;
    if (clockmod::valid() || now - upSince > 10000) {
      g_bootFetchDone = true;
      requestNow();
    }
    return;
  }
  if (g_scheduled && (int32_t)(now - g_nextFetchMs) >= 0) {
    g_scheduled = false;
    requestNow();
  }
}

Status status() { return g_status; }

const char* lastError() {
  static char copy[sizeof g_error];
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strlcpy(copy, g_error, sizeof copy);
  xSemaphoreGive(g_mutex);
  return copy;
}

bool takeNewData(bc::BookClubData& out) {
  if (!g_hasNew) return false;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool got = false;
  if (g_hasNew && g_new) {
    out = *g_new;
    g_new.reset();
    g_hasNew = false;
    got = true;
  }
  xSemaphoreGive(g_mutex);
  return got;
}

int updateProgress() { return g_updatePct; }

}  // namespace datasync
