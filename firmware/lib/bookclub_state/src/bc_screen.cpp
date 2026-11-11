#include "bc_screen.h"

namespace bc {

bool inMinuteWindow(int minute, int from, int to) {
  if (from == to) return false;
  if (from < to) return minute >= from && minute < to;
  return minute >= from || minute < to;  // wraps past midnight
}

ScreenPower screenPower(const ScreenInputs& in, const ScreenRules& r) {
  if (in.hold) return ScreenPower::On;
  // Night: off, apart from a short wake after the knob is used.
  if (in.nightEnabled && in.now.valid &&
      inMinuteWindow(in.now.minuteOfDay, in.nightFrom, in.nightTo) &&
      in.msSinceActivity >= r.nightWakeMs) {
    return ScreenPower::Off;
  }
  // A dark room, with nobody using it, for long enough.
  if (in.msDark >= r.darkBlankMs && in.msSinceActivity >= r.darkBlankMs) return ScreenPower::Off;
  // Nobody has touched it for a while: dim, still readable at a glance.
  if (in.idleDimMs && in.msSinceActivity >= in.idleDimMs) return ScreenPower::Dim;
  return ScreenPower::On;
}

void pixelShift(uint32_t hourIndex, int8_t& dx, int8_t& dy) {
  static const int8_t kX[4] = {0, 1, 1, 0};
  static const int8_t kY[4] = {0, 0, 1, 1};
  dx = kX[hourIndex % 4];
  dy = kY[hourIndex % 4];
}

NightWindow nightPreset(int i) {
  static const NightWindow kPresets[kNightPresets] = {
      {false, 0, 0, "NEVER"},
      {true, 23 * 60, 7 * 60, "23:00-07:00"},
      {true, 22 * 60, 7 * 60, "22:00-07:00"},
      {true, 0, 7 * 60, "00:00-07:00"},
  };
  return kPresets[(i >= 0 && i < kNightPresets) ? i : 0];
}

uint32_t idleDimPresetMs(int i) {
  static const uint32_t kMs[kIdlePresets] = {0, 10UL * 60 * 1000, 30UL * 60 * 1000, 60UL * 60 * 1000};
  return kMs[(i >= 0 && i < kIdlePresets) ? i : 0];
}

const char* idleDimPresetLabel(int i) {
  static const char* const kLabels[kIdlePresets] = {"NEVER", "10 MIN", "30 MIN", "60 MIN"};
  return kLabels[(i >= 0 && i < kIdlePresets) ? i : 0];
}

}  // namespace bc
