// Screen power policy: when the backlight should be on, dimmed or off.
//
// Pure (no hardware), so the rules are unit-tested on the PC. The LCD can't
// burn in, but its LED backlight slowly wears with hours lit, and a picture
// held for weeks can leave a faint, temporary ghost. So the unit turns the
// backlight off at night and in a dark room, dims it when nobody has touched
// the knob for a while, and nudges the whole picture by a pixel each hour.
#pragma once

#include <stdint.h>

#include "bc_date.h"

namespace bc {

enum class ScreenPower : uint8_t { On, Dim, Off };

struct ScreenRules {
  uint32_t darkBlankMs = 10UL * 60 * 1000;  // off after this long in a dark room
  uint32_t nightWakeMs = 2UL * 60 * 1000;   // a knob turn at night wakes it this long
};

struct ScreenInputs {
  uint32_t msSinceActivity = 0;  // since the knob or touch screen was last used
  uint32_t msDark = 0;           // how long the room has been dark (0 = not dark)
  bool hold = false;             // keep on (e.g. the setup portal is open)
  LocalNow now;                  // local time; night mode is skipped if invalid
  bool nightEnabled = false;
  int16_t nightFrom = 23 * 60;   // minutes after midnight; may wrap past midnight
  int16_t nightTo = 7 * 60;
  uint32_t idleDimMs = 0;        // 0 = never dim
};

ScreenPower screenPower(const ScreenInputs& in, const ScreenRules& rules = ScreenRules());

// True if `minute` falls in [from, to), wrapping past midnight when from > to.
// An empty window (from == to) is never active.
bool inMinuteWindow(int minute, int from, int to);

// The hourly anti-retention nudge: cycles (0,0) (1,0) (1,1) (0,1).
void pixelShift(uint32_t hourIndex, int8_t& dx, int8_t& dy);

// Menu presets, shared by the settings menu and the backlight.
constexpr int kNightPresets = 4;  // index 0 = never
struct NightWindow {
  bool enabled;
  int16_t from, to;
  const char* label;
};
NightWindow nightPreset(int i);

constexpr int kIdlePresets = 4;  // index 0 = never
uint32_t idleDimPresetMs(int i);
const char* idleDimPresetLabel(int i);

}  // namespace bc
