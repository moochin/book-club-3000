// Safe HTTPS for the background tasks (sync and nudge), which run on core 0
// where the task watchdog restarts the unit if the idle task is starved for
// 5 s.
//
// Why this exists: the TLS socket is non-blocking, and Arduino's
// Stream::timedRead() (behind readStringUntil(), which HTTPClient uses for
// header lines, chunked replies and getString()) retries read() in a tight
// loop, without yielding, until its timeout. With a 15 s timeout, a reply
// that arrived in pieces held core 0 long enough to trip the watchdog. So:
// a short stream timeout (any such spin ends before the watchdog), HTTP/1.0
// (no chunked replies), our own body loop that sleeps while waiting, and no
// TLS at all when the heap can't hold a session.
#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stddef.h>
#include <stdint.h>

namespace https {

// A TLS session needs its 16 KB input buffer in one piece (plus smaller
// allocations). Below this a handshake can't work, so don't start one.
constexpr size_t kMinBlockBytes = 20 * 1024;
// HTTPClient applies this to the stream: the longest it can spin (< 5 s).
constexpr uint16_t kStreamTimeoutMs = 3000;

// Enough contiguous heap for a TLS session right now.
bool memoryOk();

// CA bundle, connect/handshake timeouts, the short stream timeout, HTTP/1.0.
void setup(WiFiClientSecure& client, HTTPClient& http);

// Reads the reply body (to its Content-Length, or until the server closes)
// into `out`, NUL-terminated, sleeping between reads. Returns its length,
// -1 if it doesn't fit in `cap`, or -2 if it stopped short.
int readBody(HTTPClient& http, char* out, size_t cap, uint32_t timeoutMs);

// TLS's own reason for a failed connect (from WiFiClientSecure), or "" when
// the failure was the network rather than TLS.
void tlsReason(WiFiClientSecure& client, char* out, size_t cap);

}  // namespace https
