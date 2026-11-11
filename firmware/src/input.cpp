#include "input.h"

#include <Arduino.h>
#include <ESP32Encoder.h>

#include "config.h"
#include "display.h"

namespace input {

static ESP32Encoder g_enc;
static int64_t g_lastDetentCount = 0;

static bool g_rawDown = false;
static bool g_down = false;
static uint32_t g_changeMs = 0;
static uint32_t g_downMs = 0;
static bool g_longFired = false;

// Touch: a tap is reported where the stylus first landed.
static bool g_touchDown = false;
static uint32_t g_touchDownMs = 0;
static uint32_t g_touchSeenMs = 0;
static int g_touchX = 0, g_touchY = 0;
static bool g_touchLongFired = false;

static void pollTouch(uint32_t now, Events& e) {
  lgfx::touch_point_t tp;
  const bool touched = display::panel().getTouch(&tp, 1) > 0;
  if (touched) {
    if (!g_touchDown) {
      g_touchDown = true;
      g_touchDownMs = now;
      g_touchX = tp.x;
      g_touchY = tp.y;
      g_touchLongFired = false;
    }
    g_touchSeenMs = now;
    if (!g_touchLongFired && now - g_touchDownMs >= cfg::kLongPressMs) {
      g_touchLongFired = true;
      e.longPress = true;
      Serial.println("[touch] hold -> long press");
    }
    return;
  }
  if (!g_touchDown || now - g_touchSeenMs < cfg::kTouchReleaseMs) return;
  g_touchDown = false;
  if (g_touchLongFired) return;
  e.tap = true;
  e.x = (int16_t)g_touchX;
  e.y = (int16_t)g_touchY;
  Serial.printf("[touch] tap x=%d y=%d\n", g_touchX, g_touchY);
}

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

  pollTouch(now, e);
  return e;
}

bool bootHeld() { return digitalRead(cfg::kPinBoot) == LOW; }

}  // namespace input
