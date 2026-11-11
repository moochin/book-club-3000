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
static bool g_swArmed = false;  // the switch has been seen released

// Touch: a tap is reported at the average of the readings taken while the
// stylus was down. The first couple are skipped: a resistive panel reads off
// while contact pressure is still building.
constexpr int kTouchSettleSamples = 2;
static bool g_touchDown = false;
static uint32_t g_touchDownMs = 0;
static uint32_t g_touchSeenMs = 0;
static int g_touchSamples = 0;
static int32_t g_touchSumX = 0, g_touchSumY = 0;
static int g_touchFirstX = 0, g_touchFirstY = 0;
static bool g_touchLongFired = false;

static void pollTouch(uint32_t now, Events& e) {
  auto& p = display::panel();
  lgfx::touch_point_t tp;
  if (p.getTouch(&tp, 1) > 0) {
    int x = tp.x, y = tp.y;
    if (cfg::kTouchFlip) {
      // Landscape rotations (1, 3): that axis is screen Y; portrait: X.
      if (p.getRotation() & 1) y = p.height() - 1 - y;
      else x = p.width() - 1 - x;
    }
    if (!g_touchDown) {
      g_touchDown = true;
      g_touchDownMs = now;
      g_touchSamples = 0;
      g_touchSumX = g_touchSumY = 0;
      g_touchFirstX = x;
      g_touchFirstY = y;
      g_touchLongFired = false;
    }
    if (++g_touchSamples > kTouchSettleSamples) {
      g_touchSumX += x;
      g_touchSumY += y;
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
  const int n = g_touchSamples - kTouchSettleSamples;
  e.tap = true;
  e.x = (int16_t)(n > 0 ? g_touchSumX / n : g_touchFirstX);
  e.y = (int16_t)(n > 0 ? g_touchSumY / n : g_touchFirstY);
  Serial.printf("[touch] tap x=%d y=%d (first %d,%d, %d samples)\n", e.x, e.y, g_touchFirstX,
                g_touchFirstY, g_touchSamples);
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
  // GPIO 35 has no internal pull-up: with no knob wired it floats low and
  // looks held, which opened settings 2 s after every boot. Only listen once
  // the switch has read released for a while, as a wired knob does at once.
  if (!g_swArmed) {
    if (raw || now - g_changeMs < cfg::kKnobArmMs) {
      pollTouch(now, e);
      return e;
    }
    g_swArmed = true;
    Serial.println("[input] knob switch detected");
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
