// UI: draws what it is given and turns knob events into actions. It never
// decides what state the club is in (that is the state engine's job) and
// never touches the network or settings storage itself.
#pragma once

#include <stdint.h>

#include "bc_data.h"
#include "bc_nudge.h"
#include "bc_state.h"
#include "input.h"

namespace ui {

enum class SyncInfo : uint8_t { Idle, Fetching, Ok, Failed, Updating };

// Everything a frame needs.
struct Model {
  const bc::BookClubData* data = nullptr;  // null: no data yet
  bc::DisplayState state;
  bc::LocalNow now;
  bool wifiUp = false;
  bool offlineLong = false;
  bool portal = false;
  bool portalCancellable = false;
  const char* apName = "";
  const char* hostname = "";
  const char* ip = "-";
  uint32_t lastSyncEpoch = 0;
  bool syncStale = false;
  SyncInfo sync = SyncInfo::Idle;
  const char* syncError = "";
  int updatePct = -1;                      // >= 0: a firmware update is running
  const char* previewName = nullptr;       // set while a debug scenario is shown
  // Settings shown in the menu.
  bool sound = true;
  bool autoBrightness = true;
  uint8_t fixedBrightness = 160;
  uint8_t theme = 0;
  bool effects = true;
  uint8_t nightPreset = 1;
  uint8_t idlePreset = 2;
  const char* owner = "";
  const uint8_t* stats = nullptr;
  int lightRaw = 0;
  bc::FunCaps caps;                        // this unit owner's Fun Caps
  // Nudges.
  bool nudgesConfigured = false;           // hub URL and token both set
  int friendCount = 0;
  const char* friends[bc::kMaxFriends] = {};
  const bc::Nudge* incoming = nullptr;     // a received nudge waiting to be seen
  const char* toast = nullptr;             // a short note at the bottom ("NUDGE SENT")
  bool toastWarn = false;
};

enum class Action : uint8_t {
  None,
  RefreshNow,
  ToggleSound,
  CycleBrightness,
  CycleTheme,
  ToggleEffects,
  CycleNight,
  CycleIdleDim,
  OpenPortal,
  CancelPortal,
  Preview,      // arg = scenario index
  ExitPreview,
  SendNudge,    // arg = friend index (-1 = everyone), arg2 = message index
  DismissNudge,
};

struct Result {
  Action action = Action::None;
  int arg = 0;
  int arg2 = 0;
  bool redraw = false;  // the screen changed and should be redrawn now
};

void begin();
Result handle(const input::Events& e, const Model& m, uint32_t nowMs);
void render(const Model& m, uint32_t nowMs);
// True when the current screen animates (needs frequent redraws).
bool animating(const Model& m);

// Where the UI is, so the main loop can pick sounds for changes.
struct View {
  bool settings = false;  // the settings menu or a screen opened from it
  bool menu = false;      // the settings list itself
  int menuSel = 0;
  uint8_t tab = 0;
};
View view();
constexpr int kMenuItems = 13;  // rows in the settings list

// One frame of the start-up screen, msSinceStart into kBootSplashMs.
void drawBoot(uint32_t msSinceStart);

// Full-screen firmware update progress; safe to call from OTA callbacks.
void drawUpdating(const char* title, int pct);

}  // namespace ui
