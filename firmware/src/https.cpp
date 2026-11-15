#include "https.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "config.h"

// The Mozilla root CA bundle compiled into the core.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace https {

bool memoryOk() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kMinBlockBytes; }

void setup(WiFiClientSecure& client, HTTPClient& http) {
  client.setCACertBundle(x509_crt_bundle_start);
  client.setHandshakeTimeout(cfg::kHttpTimeoutMs / 1000);  // its loop sleeps: safe to be long
  http.setConnectTimeout(cfg::kHttpTimeoutMs);
  http.setTimeout(kStreamTimeoutMs);  // header wait and any readStringUntil() spin
  http.useHTTP10(true);               // no chunked replies to decode
  http.setReuse(false);
  http.setUserAgent("BookClub3000/" BC_FW_VERSION);
}

int readBody(HTTPClient& http, char* out, size_t cap, uint32_t timeoutMs) {
  const int declared = http.getSize();  // -1: until the server closes
  if (declared >= 0 && (size_t)declared > cap - 1) {
    out[0] = 0;
    return -1;
  }
  WiFiClient* in = http.getStreamPtr();
  size_t n = 0;
  uint32_t lastData = millis();
  while (declared < 0 || n < (size_t)declared) {
    const int avail = in ? in->available() : 0;
    if (avail > 0) {
      if (n + (size_t)avail > cap - 1) {
        out[n] = 0;
        return -1;
      }
      const int got = in->read(reinterpret_cast<uint8_t*>(out + n), (size_t)avail);
      if (got > 0) {
        n += (size_t)got;
        lastData = millis();
      }
      vTaskDelay(1);
    } else {
      if (!in || !in->connected() || millis() - lastData > timeoutMs) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  out[n] = 0;
  if (declared >= 0 && n != (size_t)declared) return -2;
  return (int)n;
}

void tlsReason(WiFiClientSecure& client, char* out, size_t cap) {
  out[0] = 0;
  // -1 is the network (no socket, refused, timed out); below that, TLS.
  if (client.lastError(out, cap) >= -1) out[0] = 0;
}

}  // namespace https
