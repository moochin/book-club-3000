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
constexpr uint32_t kSignalsMs = 10UL * 60 * 1000;  // signals check
constexpr size_t kReplyCap = 4096;
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
static uint32_t g_signalsTryMs = 0;    // the last check, good or bad
static bool g_signalsDue = true;
static volatile uint32_t g_signalsVersion = 0;
static char g_signalsError[48] = "";
static volatile SendState g_sendState = SendState::Idle;
static char g_sendError[48] = "";
static char g_error[48] = "";
static char g_tlsWhy[48] = "";  // TLS's reason for the last failed connect (task only)

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
  g_tlsWhy[0] = '\0';
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
  if (code < 0 && client.lastError(g_tlsWhy, sizeof g_tlsWhy) == 0) g_tlsWhy[0] = '\0';
  if (code > 0) {
    // getString() decodes chunked replies; the hub's are a few hundred bytes.
    const String reply = http.getString();
    strlcpy(out, reply.c_str(), cap);
  }
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
      g_signalsDue = true;  // show our own straight away
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
  strlcpy(g_me, me, sizeof g_me);
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

static void doSignals(char* buf) {
  const int code = request("GET", "/signals", "", buf, kReplyCap);
  const uint32_t now = millis();  // "left" counts down from the reply
  static bc::Signal got[bc::kMaxSignals];
  const int n = code == 200 ? bc::parseSignals(buf, strlen(buf), got, bc::kMaxSignals) : -1;
  Lock l;
  g_signalsDue = false;
  g_signalsTryMs = now;  // paces the next check, after a failure too
  if (n < 0) {
    if (code == 404) strlcpy(g_signalsError, "the hub needs updating for signals", sizeof g_signalsError);
    else describe(code, buf, g_signalsError, sizeof g_signalsError);
    return;
  }
  memcpy(g_signals, got, sizeof got);
  g_signalCount = n;
  g_signalsAtMs = now;
  g_signalsError[0] = '\0';
  ++g_signalsVersion;
}

static void taskMain(void*) {
  static char buf[kReplyCap];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPollMs));
    if (!g_wifiUp || !configured()) continue;
    const uint32_t now = millis();
    datasync::lockTls();
    bool pending, rosterDue, signalsDue;
    {
      Lock l;
      pending = g_sendPending;
      rosterDue = !g_rosterOk || now - g_rosterAtMs >= kRosterMs;
    }
    if (pending) doSend(buf);
    if (rosterDue) doRoster(buf, now);
    doInbox(buf);
    {
      Lock l;  // after doSend, which can make it due
      signalsDue = g_signalsDue || now - g_signalsTryMs >= kSignalsMs;
    }
    if (signalsDue) doSignals(buf);
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
      g_signalsDue = true;
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

}  // namespace nudge
