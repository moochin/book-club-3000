#include "backlight.h"

#include <Arduino.h>

#include "bc_screen.h"
#include "config.h"
#include "display.h"
#include "settings.h"

namespace backlight {

static int g_raw = 0;
static int g_level = 1000;  // smoothed 0..1000
static uint32_t g_lastSampleMs = 0;
static uint32_t g_darkSinceMs = 0;
static bool g_dark = false;
static uint32_t g_lastActivityMs = 0;
static bool g_blanked = false;
static bool g_hold = false;
static int g_currentDuty = -1;

void begin() {
  analogSetPinAttenuation(cfg::kPinLdr, ADC_0db);  // the LDR divider swings low
  pinMode(cfg::kPinLdr, INPUT);
  g_raw = analogRead(cfg::kPinLdr);
}

static int levelFromRaw(int raw) {
  long l = map(raw, cfg::kLdrDark, cfg::kLdrBright, 0, 1000);
  if (!cfg::kLdrBrightIsLow) l = 1000 - l;
  return (int)constrain(l, 0, 1000);
}

static bool rawIsDark(int raw) {
  return cfg::kLdrBrightIsLow ? raw >= cfg::kLdrDarkRoom : raw <= cfg::kLdrDarkRoom;
}

void tick(uint32_t now, bool activity, const bc::LocalNow& local) {
  if (now - g_lastSampleMs >= 200) {
    g_lastSampleMs = now;
    g_raw = analogRead(cfg::kPinLdr);
    // Exponential smoothing so a passing shadow does not pump the backlight.
    g_level += (levelFromRaw(g_raw) - g_level) / 8;
    const bool dark = rawIsDark(g_raw);
    if (dark && !g_dark) g_darkSinceMs = now;
    g_dark = dark;
  }
  if (activity) g_lastActivityMs = now;

  const settings::Unit& u = settings::get();
  bc::ScreenInputs in;
  in.msSinceActivity = now - g_lastActivityMs;
  in.msDark = g_dark ? now - g_darkSinceMs : 0;
  in.hold = g_hold;
  in.now = local;
  const bc::NightWindow w = bc::nightPreset(u.nightPreset);
  in.nightEnabled = w.enabled;
  in.nightFrom = w.from;
  in.nightTo = w.to;
  in.idleDimMs = bc::idleDimPresetMs(u.idlePreset);
  const bc::ScreenPower power = bc::screenPower(in);
  g_blanked = power == bc::ScreenPower::Off;

  int duty;
  if (power == bc::ScreenPower::Off) {
    duty = 0;
  } else if (power == bc::ScreenPower::Dim) {
    duty = cfg::kBacklightFloor;
  } else if (u.autoBrightness) {
    duty = cfg::kBacklightFloor + (cfg::kBacklightMax - cfg::kBacklightFloor) * g_level / 1000;
  } else {
    duty = max<int>(u.fixedBrightness, cfg::kBacklightFloor);
  }
  if (duty != g_currentDuty) {
    g_currentDuty = duty;
    display::setBrightness((uint8_t)duty);
  }
}

bool blanked() { return g_blanked; }
void holdAwake(bool hold) { g_hold = hold; }
int lightLevel() { return g_level; }
int rawReading() { return g_raw; }

}  // namespace backlight
