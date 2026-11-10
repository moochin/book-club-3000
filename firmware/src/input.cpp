#include "input.h"

#include <Arduino.h>
#include <ESP32Encoder.h>

#include "config.h"

namespace input {

static ESP32Encoder g_enc;
static int64_t g_lastDetentCount = 0;

static bool g_rawDown = false;
static bool g_down = false;
static uint32_t g_changeMs = 0;
static uint32_t g_downMs = 0;
static bool g_longFired = false;

void begin() {
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  g_enc.attachFullQuad(cfg::kPinEncA, cfg::kPinEncB);
  g_enc.setFilter(1023);  // glitch filter, in APB clock cycles
  g_enc.clearCount();
  pinMode(cfg::kPinEncSw, INPUT);  // GPIO 35 has no internal pull-up
  pinMode(cfg::kPinBoot, INPUT_PULLUP);
}

Events poll(uint32_t now) {
  Events e;

  // Turn: count whole detents, keep the remainder for next time.
  const int64_t count = g_enc.getCount();
  const int64_t detents = count / cfg::kEncCountsPerDetent;
  if (detents != g_lastDetentCount) {
    int64_t d = detents - g_lastDetentCount;
    if (d > 100) d = 100;
    if (d < -100) d = -100;
    e.turn = (int8_t)d;
    g_lastDetentCount = detents;
  }

  // Press: debounce, short on release, long while held.
  const bool raw = digitalRead(cfg::kPinEncSw) == LOW;
  if (raw != g_rawDown) {
    g_rawDown = raw;
    g_changeMs = now;
  }
  if (raw != g_down && now - g_changeMs >= cfg::kDebounceMs) {
    g_down = raw;
    if (g_down) {
      g_downMs = now;
      g_longFired = false;
    } else if (!g_longFired) {
      e.shortPress = true;
    }
  }
  if (g_down && !g_longFired && now - g_downMs >= cfg::kLongPressMs) {
    g_longFired = true;
    e.longPress = true;
  }
  return e;
}

bool bootHeld() { return digitalRead(cfg::kPinBoot) == LOW; }

}  // namespace input
