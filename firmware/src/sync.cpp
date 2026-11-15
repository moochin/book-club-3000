#include "sync.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>

#include <memory>

#include "bc_state.h"
#include "clock.h"
#include "config.h"
#include "fwguard.h"
#include "https.h"
#include "settings.h"


namespace datasync {

static TaskHandle_t g_task;
static SemaphoreHandle_t g_mutex;
static SemaphoreHandle_t g_tlsMutex;
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
static volatile uint32_t g_reachedCount = 0;  // fwguard's proof: see reachedCount()
static volatile uint32_t g_busySinceMs = 0;  // 0: not fetching
static volatile bool g_tlsHeld = false;  // a task holds the TLS lock
static volatile bool g_probeFailed = false;  // the probation check failed this fetch
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

// "gist.githubusercontent.com" from "https://gist.githubusercontent.com/...".
static void hostOf(const char* url, char* host, size_t cap) {
  const char* p = strstr(url, "://");
  p = p ? p + 3 : url;
  size_t n = 0;
  while (p[n] && p[n] != '/' && p[n] != ':' && n + 1 < cap) {
    host[n] = p[n];
    ++n;
  }
  host[n] = 0;
}

// Downloads at most bc::kMaxFileBytes into buf. Returns length or -1.
// `answered`: set when the host replied with an HTTP status (good or bad).
static int download(const char* url, const char* token, char* buf, size_t cap, bool* answered) {
  if (!url[0]) {
    setError("no data URL: set it on the setup page");
    return -1;
  }
  // Look the host up first, so a DNS failure says so rather than hiding in
  // HTTPClient's catch-all "connection refused".
  char host[64];
  hostOf(url, host, sizeof host);
  IPAddress ip;
  if (host[0] && !WiFi.hostByName(host, ip)) {
    setError("fetch failed: can't look up %s", host);
    return -1;
  }
  // Rather than fail partway through a handshake, don't start one.
  if (!https::memoryOk()) {
    setError("fetch failed: low memory (trying later)");
    return -1;
  }
  WiFiClientSecure client;
  HTTPClient http;
  https::setup(client, http);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
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
  if (code > 0 && answered) *answered = true;
  if (code != HTTP_CODE_OK) {
    // HTTPClient calls any failed connect "connection refused". The secure
    // client knows better: -1 is the network (no socket, refused, timed out,
    // often short of memory too); below that, TLS's own error.
    char tls[48] = "";
    if (code < 0) https::tlsReason(client, tls, sizeof tls);
    if (tls[0]) setError("fetch failed: %s", tls);
    else if (code == HTTPC_ERROR_CONNECTION_REFUSED) setError("fetch failed: can't connect to %s", host);
    else if (code < 0) setError("fetch failed: %s", HTTPClient::errorToString(code).c_str());
    else setError("HTTP %d%s", code, (code == 404 && !(token && token[0])) ? " (check the data URL)" : "");
    http.end();
    return -1;
  }
  const int n = https::readBody(http, buf, cap, cfg::kHttpTimeoutMs);
  http.end();
  if (n == -1) {
    setError("file too large");
    return -1;
  }
  if (n < 0) {
    setError("download cut short");
    return -1;
  }
  return n;
}

// Downloads `url` straight into the spare app slot. Arduino's HTTPUpdate
// feeds Update.writeStream(), which pulls TLS data one byte at a time through
// Stream::readBytes(): about 2.5 KB/s, so a 1.4 MB update took ~10 minutes.
// Bulk reads into a 4 KB buffer instead. Writes a reason into `err` on failure.
static bool fetchFirmware(const char* url, char* err, size_t errLen) {
  if (!https::memoryOk()) {
    snprintf(err, errLen, "low memory (trying later)");
    return false;
  }
  WiFiClientSecure client;
  HTTPClient http;
  https::setup(client, http);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // GitHub release assets redirect
  if (!http.begin(client, url)) {
    snprintf(err, errLen, "bad URL");
    return false;
  }
  const int code = http.GET();
  const int total = http.getSize();
  if (code != HTTP_CODE_OK || total <= 0) {
    if (code != HTTP_CODE_OK) snprintf(err, errLen, "HTTP %d", code);
    else snprintf(err, errLen, "no length");
    http.end();
    return false;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[4096]);
  if (!buf || !Update.begin((size_t)total, U_FLASH)) {
    snprintf(err, errLen, "%s", buf ? Update.errorString() : "out of memory");
    http.end();
    return false;
  }
  WiFiClient* in = http.getStreamPtr();
  size_t done = 0;
  const uint32_t startMs = millis();
  uint32_t lastData = startMs, lastLog = startMs;
  while (done < (size_t)total) {
    const int avail = in->available();
    if (avail > 0) {
      size_t want = (size_t)avail < 4096 ? (size_t)avail : 4096;
      if (want > (size_t)total - done) want = (size_t)total - done;
      const int n = in->read(buf.get(), want);
      if (n > 0) {
        if (Update.write(buf.get(), (size_t)n) != (size_t)n) {
          snprintf(err, errLen, "flash write: %s", Update.errorString());
          Update.abort();
          http.end();
          return false;
        }
        done += (size_t)n;
        g_updatePct = (int)(done * 100 / (size_t)total);
        lastData = millis();
      }
      vTaskDelay(1);  // let core 0's idle task in now and then
    } else {
      if (!in->connected() || millis() - lastData > 30000) break;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (millis() - lastLog >= 5000) {
      lastLog = millis();
      Serial.printf("[sync] update %d%%, %lu KB/s\n", g_updatePct,
                    (unsigned long)(done / (millis() - startMs + 1)));  // bytes per ms = KB/s
    }
  }
  http.end();
  if (done != (size_t)total) {
    snprintf(err, errLen, "cut short (%u of %d bytes)", (unsigned)done, total);
    Update.abort();
    return false;
  }
  if (!Update.end(true)) {
    snprintf(err, errLen, "%s", Update.errorString());
    return false;
  }
  Serial.printf("[sync] update downloaded: %d bytes in %lu s\n", total,
                (unsigned long)((millis() - startMs) / 1000));
  return true;
}

static const char* firmwareUrl(const bc::FirmwareInfo& fw) {
#if defined(BC_PANEL_ST7789)
  return fw.urlFor(true);
#else
  return fw.urlFor(false);
#endif
}

// For fwguard: can this firmware still reach a release download? Asks for the
// first byte of `url` along the same path an update takes (github.com, then a
// redirect to GitHub's download host). Any HTTP status counts: TLS and the
// redirect worked, even if the club file names a missing asset.
static bool probeFirmware(const char* url) {
  if (!https::memoryOk()) return false;
  WiFiClientSecure client;
  HTTPClient http;
  https::setup(client, http);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("Range", "bytes=0-0");
  const int code = http.GET();
  http.end();  // not reading the body: closing is enough
  Serial.printf("[sync] release download check -> %d\n", code);
  return code > 0;
}

static void maybeUpdateFirmware(const bc::FirmwareInfo& fw) {
  const char* url = firmwareUrl(fw);
  if (!url) return;
  if (bc::compareVersions(fw.version, BC_FW_VERSION) <= 0) return;
  if (strcmp(fw.version, g_failedFwVersion) == 0) return;  // tried already this boot
  if (!fw.forUnit(settings::get().owner)) return;           // "only" names other units
  if (fwguard::onProbation()) return;  // prove this one first: no update mid-probation
  if (fwguard::skipVersion(fw.version)) {
    Serial.printf("[sync] skipping %s: it was rolled back twice\n", fw.version);
    return;
  }
  Serial.printf("[sync] firmware %s available (running %s)\n", fw.version, BC_FW_VERSION);
  g_status = Status::Updating;
  g_updatePct = 0;
  // The download and flash writes keep core 0 busy for long stretches, and
  // the task watchdog then reset the board mid-install (from 1.2.0 to 1.3.0
  // it looped: reset, retry, reset). Don't watch core 0's idle task while
  // the update runs.
  disableCore0WDT();
  char err[64] = "";
  const bool ok = fetchFirmware(url, err, sizeof err);
  enableCore0WDT();
  if (ok) {
    fwguard::installing(fw.version);  // so a rollback is noticed and counted
    Serial.println("[sync] update installed, restarting");
    delay(200);
    ESP.restart();
  }
  strlcpy(g_failedFwVersion, fw.version, sizeof g_failedFwVersion);
  setError("update %s failed: %s", fw.version, err);
}

static void runFetch() {
  g_status = Status::Fetching;
  g_probeFailed = false;
  bool answered = false;
  const settings::Unit& u = settings::get();
  std::unique_ptr<char[]> buf(new (std::nothrow) char[bc::kMaxFileBytes + 1]);
  std::unique_ptr<bc::BookClubData> data(new (std::nothrow) bc::BookClubData());
  if (!buf || !data) {
    setError("out of memory");
    g_status = Status::Failed;
    return;
  }
  const int n = download(u.dataUrl, u.token, buf.get(), bc::kMaxFileBytes + 1, &answered);
  if (n < 0) {
    if (answered) g_reachedCount = g_reachedCount + 1;  // see reachedCount()
    g_status = Status::Failed;
    return;
  }
  const bc::ParseError err = bc::parseBookClub(buf.get(), (size_t)n, *data);
  if (err != bc::ParseError::None) {
    // Keep the cached copy: a bad edit must never blank the unit.
    setError("rejected file: %s", bc::parseErrorText(err));
    g_reachedCount = g_reachedCount + 1;  // a typo mustn't roll back a good build
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
  buf.reset();  // the club file is saved: free its buffer before more TLS
  // On probation, prove the update path itself when the club file names a
  // release; otherwise GitHub's answer for the club file has to do.
  const char* probeUrl = fwguard::onProbation() ? firmwareUrl(fw) : nullptr;
  if (!probeUrl || probeFirmware(probeUrl)) g_reachedCount = g_reachedCount + 1;
  else g_probeFailed = true;  // try again soon, not in a day (see tick())
  maybeUpdateFirmware(fw);
  if (g_status == Status::Updating) g_status = Status::Failed;
}

static void taskMain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (WiFi.isConnected()) {
      lockTls();
      g_busySinceMs = millis() | 1;
      runFetch();
      g_busySinceMs = 0;
      unlockTls();
    } else {
      setError("no Wi-Fi");
      g_status = Status::Failed;
    }
    g_lastOk = g_status == Status::Ok && !g_probeFailed;
    g_doneCount = g_doneCount + 1;
  }
}

void lockTls() {
  xSemaphoreTake(g_tlsMutex, portMAX_DELAY);
  g_tlsHeld = true;
}
void unlockTls() {
  g_tlsHeld = false;
  xSemaphoreGive(g_tlsMutex);
}

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  g_tlsMutex = xSemaphoreCreateMutex();
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
    } else if (fwguard::onProbation()) {
      // A new firmware has 30 minutes of Wi-Fi to prove itself (fwguard.cpp):
      // keep trying every couple of minutes, not on the usual backoff.
      g_nextFetchMs = now + cfg::kSyncRetryMs[0];
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

uint32_t reachedCount() { return g_reachedCount; }

uint32_t busyForMs(uint32_t nowMs) {
  const uint32_t since = g_busySinceMs;
  if (!since || g_status == Status::Updating) return 0;
  // The task may have started after the caller read the clock: not negative.
  return (int32_t)(nowMs - since) > 0 ? nowMs - since : 0;
}

bool tlsBusy() { return g_tlsHeld; }

}  // namespace datasync
