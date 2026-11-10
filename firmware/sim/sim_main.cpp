// Host simulator: runs the real UI, mascot, display and state-engine code,
// drives it with knob events and saves every screen as a PPM image.
//   ./sim/build.sh && ./sim/out/sim sim/out
#include <stdio.h>
#include <string.h>
#include <string>

#include "bc_state.h"
#include "display.h"
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

int main(int argc, char** argv) {
  if (argc > 1) g_dir = argv[1];
  display::begin(1, settings::kThemeGreen);
  g_simMillis = 1000;

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
  ev(m, 0, true); ev(m, +1);  // back to STAT

  // Settings menu and its screens.
  ev(m, 0, false, true); shot("menu", m);
  ev(m, +5); ev(m, 0, true); shot("owner_card", m);
  ev(m, 0, true); ev(m, +1); ev(m, 0, true); shot("about", m);
  for (int i = 0; i < 5; ++i) ev(m, 0, true);
  shot("debug_menu", m);
  ev(m, 0, false, true); ev(m, +1); ev(m, 0, true); shot("wifi_confirm", m);
  ev(m, +1); shot("wifi_confirm_open", m);
  ev(m, 0, false, true); ev(m, 0, false, true);  // back to tabs

  // Stale sync + offline on DATA.
  ui::Model stale = m;
  stale.lastSyncEpoch = 1795000000 - 9 * 86400;
  stale.syncStale = true;
  stale.wifiUp = false;
  stale.offlineLong = true;
  ev(m, +1); shot("data_stale", stale); ev(m, -1);

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
  return 0;
}
