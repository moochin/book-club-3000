// Host simulator: runs the real UI, mascot, display and state-engine code,
// drives it with knob events and saves every screen as a PPM image.
//   ./sim/build.sh && ./sim/out/sim sim/out
#include <stdio.h>
#include <string.h>
#include <string>

#include "bc_state.h"
#include "display.h"
#include "mascot.h"
#include "scenarios.h"
#include "settings.h"
#include "ui.h"

extern uint32_t g_simMillis;
static std::string g_dir = ".";
static std::string g_next;

namespace display {
void simPresent(LGFX_Sprite& c) {
  if (g_next.empty()) return;
  const std::string path = g_dir + "/" + g_next + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
  const lgfx::bgr888_t* pal = c.getPalette();
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) {
      const auto p = pal[c.readPixelValue(x, y) & 15];
      const uint8_t rgb[3] = {p.r, p.g, p.b};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
  printf("wrote %s\n", path.c_str());
  g_next.clear();
}
}  // namespace display

static uint8_t g_stats[7] = {8, 10, 4, 2, 9, 7, 3};

static ui::Model model(const bc::BookClubData* d, const bc::LocalNow& now) {
  ui::Model m;
  m.data = d;
  m.now = now;
  if (d) m.state = bc::computeState(*d, now);
  else m.state.clockValid = now.valid;
  m.wifiUp = true;
  m.apName = "BookClub-Pete";
  m.hostname = "bookclub-pete";
  m.ip = "192.168.1.42";
  m.lastSyncEpoch = 1795000000 - 3 * 3600;
  m.sync = ui::SyncInfo::Ok;
  m.owner = "Pete";
  m.stats = g_stats;
  m.effects = false;
  m.lightRaw = 312;
  if (d) m.caps = bc::computeFunCaps(*d, now, "Pete");
  return m;
}

static void shot(const char* name, const ui::Model& m) {
  g_next = name;
  ui::render(m, g_simMillis);
}

static void ev(const ui::Model& m, int turn, bool shortPress = false, bool longPress = false) {
  input::Events e;
  e.turn = (int8_t)turn;
  e.shortPress = shortPress;
  e.longPress = longPress;
  g_simMillis += 100;
  ui::handle(e, m, g_simMillis);
}

// Writes each placeholder pose as a 64 x 96 image, as a starting point for art.
static void exportTemplates(const std::string& dir) {
  static const char* const kNames[] = {"level0", "level1", "level2", "level3", "level4",
                                       "final_week", "meeting_day", "no_date", "syncing"};
  auto& c = display::canvas();
  for (int p = 0; p < 9; ++p) {
    c.fillScreen(display::kBg);
    mascot::drawPlaceholder(0, 0, (bc::Pose)p, 1000, false);
    c.drawRect(0, 0, mascot::kW, mascot::kH, display::kBg);  // drop the guide box
    const std::string path = dir + "/" + kNames[p] + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) continue;
    fprintf(f, "P6\n%d %d\n255\n", mascot::kW, mascot::kH);
    const lgfx::bgr888_t* pal = c.getPalette();
    for (int y = 0; y < mascot::kH; ++y)
      for (int x = 0; x < mascot::kW; ++x) {
        const auto px = pal[c.readPixelValue(x, y) & 15];
        const uint8_t rgb[3] = {px.r, px.g, px.b};
        fwrite(rgb, 1, 3, f);
      }
    fclose(f);
    printf("wrote %s\n", path.c_str());
  }
}

int main(int argc, char** argv) {
  if (argc > 1) g_dir = argv[1];
  display::begin(1, settings::kThemeGreen);
  g_simMillis = 1000;
  if (argc > 2 && std::string(argv[2]) == "--templates") {
    exportTemplates(g_dir);
    return 0;
  }

  bc::BookClubData d;
  bc::LocalNow now;

  // STAT for every preview scenario.
  for (int i = 0; i < scenarios::count(); ++i) {
    scenarios::build(i, d, now);
    char name[32];
    snprintf(name, sizeof name, "stat_%02d", i);
    shot(name, model(&d, now));
  }

  // DATA and LOG with the scheduled scenario.
  scenarios::build(2, d, now);
  ui::Model m = model(&d, now);
  ev(m, +1); shot("data", m);
  ev(m, +1); shot("log", m);
  ev(m, 0, true); ev(m, +2); shot("log_scrolled", m);
  ev(m, 0, true); ev(m, +1); shot("radio_unset", m);  // RADIO, no hub yet
  ev(m, 0, true); shot("radio_menu", m);                   // press: the RADIO list
  ev(m, +3); ev(m, 0, true); shot("radio_settings", m);    // SETTINGS >
  ev(m, 0, false, true); shot("radio_menu_back", m);       // hold: back on SETTINGS >
  ev(m, 0, false, true); ev(m, +1);                        // out, round to STAT

  // Settings menu, its groups and their screens. Top level: REFRESH,
  // SCREEN >, SOUND >, THIS UNIT >, BACK.
  ev(m, 0, false, true); shot("menu", m);
  ev(m, +1); ev(m, 0, true); shot("menu_screen", m);   // SCREEN >
  ev(m, 0, false, true); shot("menu_back_on_screen", m);  // hold: up to the top, on SCREEN >
  ev(m, +1); ev(m, 0, true); ev(m, +2); shot("menu_quiet", m);  // SOUND > QUIET HOURS
  ev(m, 0, false, true); ev(m, +1); ev(m, 0, true); shot("menu_unit", m);  // THIS UNIT >
  ev(m, +1); shot("menu_knob", m);                     // THIS UNIT > KNOB
  ev(m, -1); ev(m, 0, true); shot("owner_card", m);    // THIS UNIT > OWNER CARD
  // Stat pages: press for the first, turn through the rest (stats 8 10 4 2 ...).
  ev(m, 0, true); shot("stat_gaming", m);               // 8: a stock pose
  ev(m, +1); shot("stat_trivia", m);                    // a perfect 10
  ev(m, +2); shot("stat_snacks", m);                    // 2: the low band
  ev(m, 0, true); shot("owner_card_sel", m);            // back on the card, SNACK SUPPLY picked
  ev(m, -4); shot("owner_card_back", m);                // turned onto < BACK
  ev(m, 0, false, true); ev(m, +2); ev(m, 0, true);     // hold: THIS UNIT; HOW TO USE
  shot("help_getting_around", m);                        // page 1
  for (int i = 0; i < 3; ++i) ev(m, +1);
  shot("help_nudges", m);                                // page 4: NUDGES
  ev(m, +2);
  shot("help_screen", m);                                // page 6: THE SCREEN
  ev(m, 0, false, true); ev(m, +1); ev(m, 0, true); shot("about", m);
  for (int i = 0; i < 5; ++i) ev(m, 0, true);
  shot("debug_menu", m);
  ev(m, 0, false, true); ev(m, +1); ev(m, 0, true); shot("wifi_confirm", m);
  ev(m, +1); shot("wifi_confirm_open", m);
  ev(m, 0, false, true); ev(m, 0, false, true); ev(m, 0, false, true);  // back to tabs

  // SEND FROM PHONE: the hub's page as a QR code, and without a hub.
  {
    ui::Model pm = m;
    pm.hubUrl = "https://bookclub-nudge.example.workers.dev/";
    ev(pm, -1); ev(pm, 0, true); ev(pm, +2); shot("menu_phone", pm);  // RADIO > PHONE
    ev(pm, 0, true); shot("phone_qr", pm);
    pm.nudgeToken = "pete-7f3a9c2e41b84d0e9a6c5b2f1e8d7c40";
    shot("phone_qr_token", pm);
    pm.nudgeToken = "";
    pm.hubUrl = "";
    shot("phone_qr_unset", pm);
    {
      input::Events tap;  // a tap on the QR screen goes back to the menu
      tap.tap = true;
      tap.x = 160;
      tap.y = 120;
      ui::handle(tap, pm, g_simMillis += 100);
      shot("phone_qr_tapped_back", pm);
    }
    ev(pm, 0, false, true); ev(pm, +1);                    // back to the tabs, STAT
  }

  // A knob press on STAT opens the owner card; a hold goes back.
  ev(m, 0, true); shot("owner_card_from_stat", m);
  ev(m, 0, false, true);

  // Quiet hours muting the sound: the crossed-out speaker in the header.
  {
    ui::Model qm = m;
    qm.quietNow = true;
    shot("stat_quiet", qm);
  }

  // Stale sync + offline on DATA.
  ui::Model stale = m;
  stale.lastSyncEpoch = 1795000000 - 9 * 86400;
  stale.syncStale = true;
  stale.wifiUp = false;
  stale.offlineLong = true;
  ev(m, +1); shot("data_stale", stale); ev(m, -1);

  // Side quest day: STAT shows both teaser phases, DATA the details.
  {
    bc::BookClubData sd;
    bc::LocalNow sn;
    scenarios::build(scenarios::count() - 1, sd, sn);
    ui::Model sm = model(&sd, sn);
    g_simMillis = 1000;
    shot("side_today_stat_a", sm);
    g_simMillis = 4000;
    shot("side_today_stat_b", sm);
    ev(sm, +1);
    shot("side_today_data", sm);
    ev(sm, -1);
    g_simMillis = 1000;
  }

  // Fun Caps on the owner card: a penalty, and the doghouse.
  {
    ui::Model cm = m;
    ev(cm, 0, false, true); ev(cm, +3); ev(cm, 0, true); ev(cm, 0, true);  // THIS UNIT > OWNER CARD
    cm.caps.adjust = -4;
    strcpy(cm.caps.note, "Forgot the anniversary");
    cm.caps.balance = 6;
    shot("owner_card_penalty", cm);
    cm.caps.adjust = -12;
    strcpy(cm.caps.note, "Missed the school play for club night");
    cm.caps.balance = -2;
    shot("owner_card_overdrawn", cm);
    ev(cm, 0, false, true); ev(cm, 0, false, true); ev(cm, 0, false, true);  // back to tabs
  }

  // Nudges: who, what, the note after sending, an incoming one, not set up.
  {
    static const char* const kFriends[] = {"Dave", "Sam", "Jonno"};
    ui::Model nm = m;
    nm.nudgesConfigured = true;
    nm.friendCount = 3;
    for (int i = 0; i < 3; ++i) nm.friends[i] = kFriends[i];
    ev(nm, -1); ev(nm, 0, true); ev(nm, 0, true);         // RADIO > SEND NUDGE
    ev(nm, +1); shot("nudge_who", nm);                      // highlight DAVE
    ev(nm, 0, true); ev(nm, +1); shot("nudge_what", nm);    // TO DAVE:, 2nd message
    nm.toast = "NUDGE SENT TO DAVE";
    ev(nm, 0, true); shot("nudge_sent", nm);                // sends, back to RADIO
    ev(nm, +1);                                             // round to STAT
    nm.toast = nullptr;
    bc::Nudge in;
    strcpy(in.from, "Sam");
    strcpy(in.msg, "GET YOUR ASS ONLINE");
    nm.incoming = &in;
    g_simMillis = 1000;
    shot("nudge_incoming", nm);
    strcpy(in.from, "Jonno");
    strcpy(in.msg, "I'M ON. YOU'RE NOT. SHAME.");
    shot("nudge_incoming_long", nm);
    nm.incoming = nullptr;
    in.at = 1794996400000ULL;  // an hour before the sim's clock
    nm.lastNudge = &in;
    ev(nm, -1); g_simMillis = 5000; shot("radio_silent", nm);  // RADIO: no signals, the last nudge
    nm.lastNudge = nullptr;
    nm.nudgesConfigured = false;
    ev(nm, 0, true); ev(nm, 0, true);
    shot("nudge_setup", nm);
    ev(nm, 0, true); ev(nm, 0, false, true); ev(nm, +1);    // back to the tabs, STAT
  }

  // Signals: the arrival screen, the viewer, BROADCAST, the quote picker.
  {
    static bc::Signal sigs[3];
    strcpy(sigs[0].from, "Dave");
    strcpy(sigs[0].msg, "Look on my works, ye Mighty, and despair! - Shelley");
    sigs[0].at = 1795000000; sigs[0].left = 20 * 3600;
    strcpy(sigs[1].from, "Pete");
    strcpy(sigs[1].msg, "STILL ALIVE OUT HERE. BARELY. SEND SNACKS.");
    sigs[1].at = 1794990000; sigs[1].left = 9 * 3600;
    strcpy(sigs[2].from, "Sam");
    strcpy(sigs[2].msg, "I am not afraid of storms, for I am learning how to sail my ship. - Alcott");
    sigs[2].at = 1794950000; sigs[2].left = 40 * 60;
    ui::Model sm = m;
    sm.nudgesConfigured = true;
    sm.me = "Pete";
    sm.signals = sigs;
    sm.signalCount = 3;
    g_simMillis = 3000;                                      // the teaser's turn (after the side quest)
    shot("signal_teaser", sm);                               // STAT, the teaser line
    sm.incomingSignal = &sigs[0];
    g_simMillis = 400;
    shot("signal_incoming", sm);
    sm.incomingSignal = &sigs[2];
    shot("signal_incoming_long", sm);
    sm.incomingSignal = nullptr;
    ev(sm, -1); shot("radio", sm);                           // RADIO: Dave's on top
    {
      input::Events tap;  // tap the SIGNALS tile: straight in; a hold goes back to RADIO
      tap.tap = true;
      tap.x = 160;
      tap.y = 184;
      ui::handle(tap, sm, g_simMillis += 100);
      shot("radio_tap_signals", sm);
      ev(sm, 0, false, true);
      shot("radio_tap_back", sm);
    }
    ev(sm, 0, true); ev(sm, +1); shot("signal_menu", sm);    // RADIO > SIGNALS
    ev(sm, 0, true); shot("signal_view", sm);                // Dave's
    ev(sm, +2); shot("signal_view_long", sm);                 // Sam's, fading
    ev(sm, +1); shot("signal_broadcast", sm);                // BROADCAST (yours is on)
    ev(sm, 0, true); ev(sm, +1); shot("signal_quote", sm);   // the quote picker, 2nd quote
    sm.toast = "SIGNAL ON THE AIR FOR 24 HOURS";
    ev(sm, 0, true); shot("signal_sent", sm);                // broadcast, back to the tabs
    sm.toast = nullptr;
    sm.signalCount = 0;
    sm.signalsError = "the hub needs updating for signals";
    ev(sm, 0, true); ev(sm, +1); ev(sm, 0, true);
    g_simMillis = 5000;
    shot("signal_silent", sm);                               // nothing on the air
    ev(sm, 0, false, true); ev(sm, 0, false, true); ev(sm, +1);  // back to the tabs, STAT
    sm.signalsError = "";
  }

  // The warning triangle: DATA says so, a press explains it.
  {
    ui::Model wm = m;
    wm.sync = ui::SyncInfo::Failed;
    wm.syncError = "fetch failed: can't connect to gist.githubusercontent.com";
    wm.nudgesConfigured = true;
    while (ui::view().tab != 1) ev(wm, +1);                  // to DATA
    shot("data_sync_failed", wm);
    ev(wm, 0, true); shot("sync_status_json", wm);           // press: SYNC STATUS
    wm.syncError = "update 1.4.1 failed: HTTP 404";
    wm.hubError = "can't reach the hub";
    shot("sync_status_update", wm);
    ev(wm, +1); shot("sync_status_back", wm);                 // turn: BACK
    ev(wm, 0, true);                                          // back to DATA
    wm.sync = ui::SyncInfo::Ok; wm.syncError = ""; wm.hubError = "";
    ev(wm, 0, true); shot("sync_status_ok", wm);
    ev(wm, 0, false, true);
    while (ui::view().tab != 0) ev(wm, +1);                  // back to STAT
  }

  // Full-screen states.
  ui::Model portal = m; portal.portal = true; portal.portalCancellable = false;
  shot("portal_first_setup", portal);
  ui::Model nodata = model(nullptr, now); nodata.wifiUp = false;
  shot("no_data", nodata);
  bc::LocalNow unsynced;
  ui::Model clock = model(&d, unsynced);
  shot("syncing_clock", clock);
  ui::Model upd = m; upd.updatePct = 42;
  shot("updating", upd);
  // The idle clock face (WHEN IDLE: CLOCK), green, colon lit and blinked off.
  ui::Model face = m; face.clockFace = true;
  g_simMillis = 4000;
  shot("clock_face", face);
  g_simMillis = 5000;
  face.effects = true;
  shot("clock_face_blink_fx", face);
  ui::Model faceNoTime = model(&d, unsynced); faceNoTime.clockFace = true;
  shot("clock_face_no_time", faceNoTime);

  // Amber theme, critical, with scanlines.
  display::setTheme(settings::kThemeAmber);
  scenarios::build(9, d, now);
  ui::Model amber = model(&d, now);
  amber.effects = true;
  g_simMillis = 3000;
  shot("amber_critical_fx", amber);
  scenarios::build(5, d, now);
  amber = model(&d, now);
  shot("amber_meeting_day", amber);
  amber.clockFace = true;
  shot("amber_clock_face", amber);
  return 0;
}
