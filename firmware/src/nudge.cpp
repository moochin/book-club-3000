#include "nudge.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "settings.h"
#include "sync.h"

// The Mozilla root CA bundle compiled into the core (see sync.cpp).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace nudge {

constexpr uint32_t kPollMs = 60UL * 1000;          // inbox check
constexpr uint32_t kRosterMs = 60UL * 60 * 1000;   // friends list refresh
constexpr size_t kReplyCap = 4096;

static TaskHandle_t g_task;
static SemaphoreHandle_t g_mutex;

// Shared between the task and the main loop (guarded by g_mutex).
static char g_url[bc::kUrlLen] = "";
static volatile bool g_wifiUp = false;
static char g_friends[bc::kMaxFriends][bc::kNameLen];
static int g_friendCount = 0;
static uint32_t g_rosterAtMs = 0;
static bool g_rosterOk = false;
static bc::Nudge g_incoming[bc::kMaxInbox];
static int g_inHead = 0, g_inCount = 0;
static bool g_sendPending = false;
static char g_sendTo[bc::kNameLen] = "";
static char g_sendMsg[bc::kNudgeMsgLen] = "";
static volatile SendState g_sendState = SendState::Idle;
static char g_sendError[48] = "";
static char g_error[48] = "";

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
};

static void setError(char* dst, size_t cap, const char* msg) {
  Lock l;
  strlcpy(dst, msg, cap);
}

// One HTTPS request to the hub. Returns the HTTP status (or a negative
// HTTPClient error) and the reply body in `out`.
static int request(const char* method, const char* path, const char* body, char* out, size_t cap) {
  char url[bc::kUrlLen + 16];
  {
    Lock l;
    snprintf(url, sizeof url, "%s", g_url);
  }
  size_t n = strlen(url);
  while (n && url[n - 1] == '/') url[--n] = '\0';
  strlcat(url, path, sizeof url);

  char auth[sizeof settings::Unit::nudgeToken + 8];
  snprintf(auth, sizeof auth, "Bearer %s", settings::get().nudgeToken);

  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start);
  client.setTimeout(cfg::kHttpTimeoutMs / 1000);
  client.setHandshakeTimeout(cfg::kHttpTimeoutMs / 1000);
  HTTPClient http;
  http.setTimeout(cfg::kHttpTimeoutMs);
  http.setConnectTimeout(cfg::kHttpTimeoutMs);
  http.setUserAgent("BookClub3000/" BC_FW_VERSION);
  if (!http.begin(client, url)) return -100;
  http.addHeader("Authorization", auth);
  int code;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)), strlen(body));
  } else {
    code = http.GET();
  }
  out[0] = '\0';
  if (code > 0) {
    // getString() decodes chunked replies; the hub's are a few hundred bytes.
    const String reply = http.getString();
    strlcpy(out, reply.c_str(), cap);
  }
  http.end();
  return code;
}

// The hub's {"error": "..."} text, or a generic description.
static void describe(int code, const char* reply, char* dst, size_t cap) {
  const char* e = strstr(reply, "\"error\":\"");
  if (e) {
    e += 9;
    size_t i = 0;
    while (e[i] && e[i] != '"' && i < cap - 1) {
      dst[i] = e[i];
      ++i;
    }
    dst[i] = '\0';
  } else if (code < 0) {
    snprintf(dst, cap, "can't reach the hub");
  } else {
    snprintf(dst, cap, "hub said %d", code);
  }
}

static void doSend(char* buf) {
  char to[bc::kNameLen], msg[bc::kNudgeMsgLen];
  {
    Lock l;
    strlcpy(to, g_sendTo, sizeof to);
    strlcpy(msg, g_sendMsg, sizeof msg);
    g_sendPending = false;
  }
  char body[160];
  // Names and messages are plain ASCII without quotes or backslashes in
  // practice, but escape them anyway so the JSON is always valid.
  auto esc = [](const char* s, char* d, size_t cap) {
    size_t j = 0;
    for (; *s && j + 2 < cap; ++s) {
      if (*s == '"' || *s == '\\') d[j++] = '\\';
      d[j++] = *s;
    }
    d[j] = '\0';
  };
  char eto[2 * bc::kNameLen], emsg[2 * bc::kNudgeMsgLen];
  esc(to, eto, sizeof eto);
  esc(msg, emsg, sizeof emsg);
  snprintf(body, sizeof body, "{\"to\":\"%s\",\"msg\":\"%s\"}", eto, emsg);
  const int code = request("POST", "/nudge", body, buf, kReplyCap);
  if (code == 200) {
    g_sendState = SendState::Sent;
    Serial.printf("[nudge] sent to %s\n", to);
  } else {
    char why[48];
    describe(code, buf, why, sizeof why);
    setError(g_sendError, sizeof g_sendError, why);
    g_sendState = SendState::Failed;
    Serial.printf("[nudge] send failed: %s\n", why);
  }
}

static void doRoster(char* buf, uint32_t now) {
  const int code = request("GET", "/roster", "", buf, kReplyCap);
  char friends[bc::kMaxFriends][bc::kNameLen];
  char me[bc::kNameLen];
  const int n = code == 200 ? bc::parseRoster(buf, strlen(buf), friends, bc::kMaxFriends, me, sizeof me) : -1;
  if (n < 0) {
    char why[48];
    describe(code, buf, why, sizeof why);
    setError(g_error, sizeof g_error, why);
    return;
  }
  Lock l;
  memcpy(g_friends, friends, sizeof friends);
  g_friendCount = n;
  g_rosterAtMs = now;
  g_rosterOk = true;
  g_error[0] = '\0';
}

static void doInbox(char* buf) {
  const int code = request("GET", "/inbox", "", buf, kReplyCap);
  static bc::Nudge got[bc::kMaxInbox];
  const int n = code == 200 ? bc::parseInbox(buf, strlen(buf), got, bc::kMaxInbox) : -1;
  if (n < 0) {
    char why[48];
    describe(code, buf, why, sizeof why);
    setError(g_error, sizeof g_error, why);
    return;
  }
  Lock l;
  g_error[0] = '\0';
  for (int i = 0; i < n; ++i) {
    if (g_inCount == bc::kMaxInbox) {  // full: drop the oldest
      g_inHead = (g_inHead + 1) % bc::kMaxInbox;
      --g_inCount;
    }
    g_incoming[(g_inHead + g_inCount) % bc::kMaxInbox] = got[i];
    ++g_inCount;
  }
  if (n) Serial.printf("[nudge] %d new\n", n);
}

static void taskMain(void*) {
  static char buf[kReplyCap];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPollMs));
    if (!g_wifiUp || !configured()) continue;
    const uint32_t now = millis();
    datasync::lockTls();
    bool pending, rosterDue;
    {
      Lock l;
      pending = g_sendPending;
      rosterDue = !g_rosterOk || now - g_rosterAtMs >= kRosterMs;
    }
    if (pending) doSend(buf);
    if (rosterDue) doRoster(buf, now);
    doInbox(buf);
    datasync::unlockTls();
  }
}

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskMain, "nudge", 10240, nullptr, 1, &g_task, 0);
}

void tick(uint32_t, bool wifiUp, const char* hubUrl) {
  const bool wasUp = g_wifiUp;
  g_wifiUp = wifiUp;
  bool changed;
  {
    Lock l;
    changed = strcmp(g_url, hubUrl ? hubUrl : "") != 0;
    if (changed) {
      strlcpy(g_url, hubUrl ? hubUrl : "", sizeof g_url);
      g_rosterOk = false;
      g_friendCount = 0;
    }
  }
  // Check straight away when the hub or the network first appears.
  if ((changed || (wifiUp && !wasUp)) && configured()) xTaskNotifyGive(g_task);
}

bool configured() {
  Lock l;
  return g_url[0] && settings::get().nudgeToken[0];
}

int friendCount() {
  Lock l;
  return g_friendCount;
}

const char* friendName(int i) {
  static char name[bc::kNameLen];
  Lock l;
  strlcpy(name, (i >= 0 && i < g_friendCount) ? g_friends[i] : "", sizeof name);
  return name;
}

void send(const char* to, const char* msg) {
  {
    Lock l;
    strlcpy(g_sendTo, to, sizeof g_sendTo);
    strlcpy(g_sendMsg, msg, sizeof g_sendMsg);
    g_sendPending = true;
    g_sendError[0] = '\0';
  }
  g_sendState = SendState::Sending;
  xTaskNotifyGive(g_task);
}

SendState sendState() { return g_sendState; }

const char* sendTo() {
  static char to[bc::kNameLen];
  Lock l;
  strlcpy(to, g_sendTo, sizeof to);
  return to;
}

const char* sendError() {
  static char e[sizeof g_sendError];
  Lock l;
  strlcpy(e, g_sendError, sizeof e);
  return e;
}

void clearSendState() {
  if (g_sendState != SendState::Sending) g_sendState = SendState::Idle;
}

bool takeIncoming(bc::Nudge& out) {
  Lock l;
  if (!g_inCount) return false;
  out = g_incoming[g_inHead];
  g_inHead = (g_inHead + 1) % bc::kMaxInbox;
  --g_inCount;
  return true;
}

const char* lastError() {
  static char e[sizeof g_error];
  Lock l;
  strlcpy(e, g_error, sizeof e);
  return e;
}

}  // namespace nudge
