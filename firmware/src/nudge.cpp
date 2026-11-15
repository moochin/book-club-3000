#include "nudge.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "https.h"
#include "settings.h"
#include "sync.h"

namespace nudge {

// One /poll every 5 minutes brings nudges, signals and the friends list in a
// single TLS connection. It used to be up to three connections a minute,
// which churned the heap (TLS needs a big block each time) for little gain.
constexpr uint32_t kPollMs = 5UL * 60 * 1000;
constexpr uint32_t kWakeMs = 60UL * 1000;          // how often the task looks
constexpr uint32_t kRosterMs = 60UL * 60 * 1000;   // friends list, on an old hub
constexpr size_t kReplyCap = 6144;                 // 20 nudges + 8 signals + roster
// The TLS handshake takes most of a 10 KB stack; sending a 100-character
// signal's buffers on top of that overflowed it and restarted the unit.
constexpr uint32_t kTaskStack = 16384;

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
static char g_sendMsg[bc::kSignalMsgLen] = "";  // a nudge or a signal
static bool g_sendSignal = false;
static char g_me[bc::kNameLen] = "";
static bc::Signal g_signals[bc::kMaxSignals];
static int g_signalCount = 0;
static uint32_t g_signalsAtMs = 0;     // when g_signals was fetched
static uint32_t g_pollTryMs = 0;       // the last poll, good or bad
static bool g_pollDue = true;          // poll at the next wake, whatever the time
static volatile uint32_t g_signalsVersion = 0;
static char g_signalsError[48] = "";
static volatile SendState g_sendState = SendState::Idle;
static char g_sendError[48] = "";
static char g_error[48] = "";
static char g_tlsWhy[48] = "";
static volatile uint32_t g_busySinceMs = 0;  // 0: idle  // TLS's reason for the last failed connect (task only)

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
};

static void setError(char* dst, size_t cap, const char* msg) {
  Lock l;
  strlcpy(dst, msg, cap);
}

constexpr int kLowMemory = -200;  // request() didn't try: not enough heap for TLS

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

  out[0] = '\0';
  g_tlsWhy[0] = '\0';
  // Rather than fail partway through a handshake, don't start one.
  if (!https::memoryOk()) return kLowMemory;
  WiFiClientSecure client;
  HTTPClient http;
  https::setup(client, http);
  if (!http.begin(client, url)) return -100;
  http.addHeader("Authorization", auth);
  int code;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)), strlen(body));
  } else {
    code = http.GET();
  }
  if (code < 0) https::tlsReason(client, g_tlsWhy, sizeof g_tlsWhy);
  // Not getString(): its reads can spin on core 0 (see https.h).
  if (code > 0 && https::readBody(http, out, cap, cfg::kHttpTimeoutMs) < 0) out[0] = '\0';
  http.end();
  Serial.printf("[nudge] %s %s -> %d (stack free %u)\n", method, path, code,
                (unsigned)uxTaskGetStackHighWaterMark(nullptr));
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
  } else if (code == kLowMemory) {
    snprintf(dst, cap, "low memory: trying later");
  } else if (code < 0 && g_tlsWhy[0]) {
    snprintf(dst, cap, "hub: %s", g_tlsWhy);
  } else if (code < 0) {
    snprintf(dst, cap, "can't reach the hub");
  } else {
    snprintf(dst, cap, "hub said %d", code);
  }
}

static void doSend(char* buf) {
  // Static, not on the stack: only the nudge task sends, one at a time, and
  // the TLS handshake underneath needs the stack.
  static char to[bc::kNameLen], msg[bc::kSignalMsgLen];
  bool signal;
  {
    Lock l;
    strlcpy(to, g_sendTo, sizeof to);
    strlcpy(msg, g_sendMsg, sizeof msg);
    signal = g_sendSignal;
    g_sendPending = false;
  }
  static char body[2 * bc::kNameLen + 2 * bc::kSignalMsgLen + 24];
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
  static char eto[2 * bc::kNameLen], emsg[2 * bc::kSignalMsgLen];
  esc(to, eto, sizeof eto);
  esc(msg, emsg, sizeof emsg);
  int code;
  if (signal) {
    snprintf(body, sizeof body, "{\"msg\":\"%s\"}", emsg);
    code = request("POST", "/signal", body, buf, kReplyCap);
    if (code == 404) strlcpy(buf, "{\"error\":\"the hub needs updating for signals\"}", kReplyCap);
  } else {
    snprintf(body, sizeof body, "{\"to\":\"%s\",\"msg\":\"%s\"}", eto, emsg);
    code = request("POST", "/nudge", body, buf, kReplyCap);
  }
  if (code == 200) {
    g_sendState = SendState::Sent;
    if (signal) {
      Lock l;
      g_pollDue = true;  // show our own straight away
    }
    Serial.printf("[nudge] %s\n", signal ? "signal broadcast" : "sent");
  } else {
    char why[48];
    describe(code, buf, why, sizeof why);
    setError(g_sendError, sizeof g_sendError, why);
    g_sendState = SendState::Failed;
    Serial.printf("[nudge] send failed: %s\n", why);
  }
}

// Each of these takes a hub reply (from /poll, or an old hub's own route)
// and updates the shared state. False if the reply wasn't usable.
static bool applyRoster(const char* buf, int code, uint32_t now) {
  char friends[bc::kMaxFriends][bc::kNameLen];
  char me[bc::kNameLen];
  const int n = code == 200 ? bc::parseRoster(buf, strlen(buf), friends, bc::kMaxFriends, me, sizeof me) : -1;
  if (n < 0) return false;
  Lock l;
  memcpy(g_friends, friends, sizeof friends);
  strlcpy(g_me, me, sizeof g_me);
  g_friendCount = n;
  g_rosterAtMs = now;
  g_rosterOk = true;
  return true;
}

static bool applyInbox(const char* buf, int code) {
  static bc::Nudge got[bc::kMaxInbox];
  const int n = code == 200 ? bc::parseInbox(buf, strlen(buf), got, bc::kMaxInbox) : -1;
  if (n < 0) return false;
  Lock l;
  for (int i = 0; i < n; ++i) {
    if (g_inCount == bc::kMaxInbox) {  // full: drop the oldest
      g_inHead = (g_inHead + 1) % bc::kMaxInbox;
      --g_inCount;
    }
    g_incoming[(g_inHead + g_inCount) % bc::kMaxInbox] = got[i];
    ++g_inCount;
  }
  if (n) Serial.printf("[nudge] %d new\n", n);
  return true;
}

static bool applySignals(const char* buf, int code, uint32_t now) {
  static bc::Signal got[bc::kMaxSignals];
  const int n = code == 200 ? bc::parseSignals(buf, strlen(buf), got, bc::kMaxSignals) : -1;
  if (n < 0) return false;
  Lock l;
  memcpy(g_signals, got, sizeof got);
  g_signalCount = n;
  g_signalsAtMs = now;  // "left" counts down from the reply
  g_signalsError[0] = '\0';
  ++g_signalsVersion;
  return true;
}

static void failed(int code, const char* buf, char* dst, size_t cap) {
  char why[48];
  describe(code, buf, why, sizeof why);
  setError(dst, cap, why);
}

// A hub from before /poll: the three routes, the roster only hourly.
static void pollOldHub(char* buf, uint32_t now) {
  bool rosterDue;
  {
    Lock l;
    rosterDue = !g_rosterOk || now - g_rosterAtMs >= kRosterMs;
  }
  if (rosterDue) {
    const int code = request("GET", "/roster", "", buf, kReplyCap);
    if (!applyRoster(buf, code, now)) return failed(code, buf, g_error, sizeof g_error);
  }
  int code = request("GET", "/inbox", "", buf, kReplyCap);
  if (!applyInbox(buf, code)) return failed(code, buf, g_error, sizeof g_error);
  setError(g_error, sizeof g_error, "");
  code = request("GET", "/signals", "", buf, kReplyCap);
  if (!applySignals(buf, code, millis())) {
    if (code == 404) setError(g_signalsError, sizeof g_signalsError, "the hub needs updating for signals");
    else failed(code, buf, g_signalsError, sizeof g_signalsError);
  }
}

static void doPoll(char* buf, uint32_t now) {
  {
    Lock l;
    g_pollDue = false;
    g_pollTryMs = now;  // paces the next poll, after a failure too
  }
  const int code = request("GET", "/poll", "", buf, kReplyCap);
  if (code == 404) return pollOldHub(buf, now);
  const uint32_t at = millis();
  if (applyRoster(buf, code, at) && applyInbox(buf, code) && applySignals(buf, code, at)) {
    setError(g_error, sizeof g_error, "");
    return;
  }
  failed(code, buf, g_error, sizeof g_error);
  failed(code, buf, g_signalsError, sizeof g_signalsError);
}

static void taskMain(void*) {
  static char buf[kReplyCap];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kWakeMs));
    if (!g_wifiUp || !configured()) continue;
    const uint32_t now = millis();
    bool pending, due;
    {
      Lock l;
      pending = g_sendPending;
      due = g_pollDue || now - g_pollTryMs >= kPollMs;
    }
    if (!pending && !due) continue;
    datasync::lockTls();
    g_busySinceMs = millis() | 1;
    if (pending) doSend(buf);
    {
      Lock l;  // a broadcast makes the poll due, to show it straight away
      due = due || g_pollDue;
    }
    if (due) doPoll(buf, now);
    g_busySinceMs = 0;
    datasync::unlockTls();
  }
}

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskMain, "nudge", kTaskStack, nullptr, 1, &g_task, 0);
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
      g_signalCount = 0;
      g_pollDue = true;
    }
    if (wifiUp && !wasUp) g_pollDue = true;
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

static void queue(const char* to, const char* msg, bool signal) {
  {
    Lock l;
    strlcpy(g_sendTo, to, sizeof g_sendTo);
    strlcpy(g_sendMsg, msg, signal ? sizeof g_sendMsg : bc::kNudgeMsgLen);
    g_sendSignal = signal;
    g_sendPending = true;
    g_sendError[0] = '\0';
  }
  g_sendState = SendState::Sending;
  xTaskNotifyGive(g_task);
}

void send(const char* to, const char* msg) { queue(to, msg, false); }

void broadcast(const char* msg) { queue("*", msg, true); }

SendState sendState() { return g_sendState; }

bool sendWasSignal() {
  Lock l;
  return g_sendSignal;
}

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

const char* me() {
  static char name[bc::kNameLen];
  Lock l;
  strlcpy(name, g_me, sizeof name);
  return name;
}

int signals(bc::Signal* out, int cap, uint32_t nowMs) {
  Lock l;
  const uint32_t gone = (nowMs - g_signalsAtMs) / 1000;  // seconds since the check
  int n = 0;
  for (int i = 0; i < g_signalCount && n < cap; ++i) {
    if (g_signals[i].left <= gone) continue;  // its 24 hours are up
    out[n] = g_signals[i];
    out[n].left -= gone;
    ++n;
  }
  return n;
}

uint32_t signalsVersion() { return g_signalsVersion; }

const char* signalsError() {
  static char e[sizeof g_signalsError];
  Lock l;
  strlcpy(e, g_signalsError, sizeof e);
  return e;
}

uint32_t busyForMs(uint32_t nowMs) {
  const uint32_t since = g_busySinceMs;
  return since ? nowMs - since : 0;
}

}  // namespace nudge
