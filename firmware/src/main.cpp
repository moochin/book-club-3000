// Book-Club 3000 firmware.
//
// A non-blocking loop that ticks each module. The sync and clock modules only
// gather inputs; the state engine (lib/bookclub_state) alone decides what the
// screen shows; the UI only draws what it is given. Slow work (HTTPS) runs in
// its own FreeRTOS task, so the knob stays responsive during a fetch.
#include <Arduino.h>

#include <memory>

#include "audio.h"
#include "backlight.h"
#include "bc_data.h"
#include "bc_state.h"
#include "bringup.h"
#include "clock.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "ota.h"
#include "scenarios.h"
#include "settings.h"
#include "sync.h"
#include "ui.h"

namespace {

bc::BookClubData g_data;
bool g_hasData = false;

int g_preview = -1;  // debug scenario being previewed, or -1 for live data
bc::BookClubData g_previewData;
bc::LocalNow g_previewNow;

bc::Date g_jingledOn;  // meeting-day jingle plays once per day
uint32_t g_lastFrameMs = 0;
bool g_dirty = true;

void ledsOff() {
  for (int pin : {cfg::kPinLedR, cfg::kPinLedG, cfg::kPinLedB}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);  // active low
  }
}

// BOOT held at power-on: count down, then wipe everything.
void checkFactoryReset() {
  if (!input::bootHeld()) return;
  const uint32_t start = millis();
  auto& c = display::canvas();
  while (input::bootHeld()) {
    const uint32_t held = millis() - start;
    if (held >= cfg::kFactoryResetHoldMs) {
      c.fillScreen(display::kBg);
      display::textCentered(160, 100, "FACTORY RESET", display::kWarn, 2, 2);
      display::present(false, 0);
      settings::factoryReset();
    }
    c.fillScreen(display::kBg);
    display::textCentered(160, 70, "KEEP HOLDING BOOT", display::kBright, 2, 2);
    display::textCentered(160, 100, "TO FACTORY RESET", display::kBright, 2, 2);
    char buf[8];
    snprintf(buf, sizeof buf, "%lu", (unsigned long)((cfg::kFactoryResetHoldMs - held + 999) / 1000));
    display::textCentered(160, 140, buf, display::kWarn, 4, 4);
    display::textCentered(160, 210, "RELEASE TO CANCEL", display::kDim);
    display::present(false, 0);
    delay(50);  // setup only, before the main loop starts
  }
}

void loadCache() {
  std::unique_ptr<char[]> buf(new (std::nothrow) char[bc::kMaxFileBytes + 1]);
  if (!buf) return;
  const size_t n = settings::loadCachedFile(buf.get(), bc::kMaxFileBytes + 1);
  if (n && bc::parseBookClub(buf.get(), n, g_data) == bc::ParseError::None) {
    g_hasData = true;
    Serial.printf("[main] cached file loaded (%u bytes)\n", (unsigned)n);
  } else {
    Serial.println("[main] no cached file yet");
  }
}

void applySettings() {
  const settings::Unit& u = settings::get();
  display::setRotation(u.rotation);
  display::setTheme(u.theme);
  audio::setEnabled(u.sound);
}

void doAction(const ui::Result& r) {
  settings::Unit& u = settings::get();
  switch (r.action) {
    case ui::Action::None: return;
    case ui::Action::RefreshNow: datasync::requestNow(); break;
    case ui::Action::ToggleSound:
      u.sound = !u.sound;
      audio::setEnabled(u.sound);
      if (u.sound) audio::confirmBeep();
      break;
    case ui::Action::CycleBrightness: {
      // AUTO -> 25 -> 50 -> 75 -> 100 % -> AUTO
      static const uint8_t kSteps[] = {64, 128, 192, 255};
      if (u.autoBrightness) {
        u.autoBrightness = false;
        u.fixedBrightness = kSteps[0];
      } else {
        int i = 0;
        while (i < 4 && kSteps[i] <= u.fixedBrightness) ++i;
        if (i >= 4) u.autoBrightness = true;
        else u.fixedBrightness = kSteps[i];
      }
      break;
    }
    case ui::Action::CycleTheme:
      u.theme = (uint8_t)((u.theme + 1) % settings::kThemeCount);
      display::setTheme(u.theme);
      break;
    case ui::Action::ToggleEffects: u.effects = !u.effects; break;
    case ui::Action::OpenPortal: net::openPortal(); return;
    case ui::Action::CancelPortal: net::closePortal(); return;
    case ui::Action::Preview:
      if (scenarios::build(r.arg, g_previewData, g_previewNow)) g_preview = r.arg;
      return;
    case ui::Action::ExitPreview: g_preview = -1; return;
  }
  settings::save();
}

bool quietHours(const bc::LocalNow& n) {
  if (!n.valid) return true;  // unknown time: stay quiet
  return n.minuteOfDay >= cfg::kQuietFromMinute || n.minuteOfDay < cfg::kQuietToMinute;
}

ui::SyncInfo syncInfo(datasync::Status s) {
  switch (s) {
    case datasync::Status::Fetching: return ui::SyncInfo::Fetching;
    case datasync::Status::Ok: return ui::SyncInfo::Ok;
    case datasync::Status::Failed: return ui::SyncInfo::Failed;
    case datasync::Status::Updating: return ui::SyncInfo::Updating;
    default: return ui::SyncInfo::Idle;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[main] Book-Club 3000 firmware %s\n", BC_FW_VERSION);
  ledsOff();
  settings::begin();
  input::begin();
  audio::begin();
  const settings::Unit& u = settings::get();
  display::begin(u.rotation, u.theme);
  display::setBrightness(160);
#if BC_BRINGUP
  bringup::run();
#endif
  checkFactoryReset();
  backlight::begin();
  audio::setEnabled(u.sound);
  clockmod::begin();
  loadCache();
  net::begin();
  datasync::begin();
  ui::begin();
}

void loop() {
  const uint32_t now = millis();
  input::Events ev = input::poll(now);

  // A knob event on a blank screen only wakes it.
  const bool wasBlank = backlight::blanked();
  backlight::holdAwake(net::portalActive());
  backlight::tick(now, ev.any());
  if (wasBlank && ev.any()) {
    ev = input::Events();
    g_dirty = true;
  }
  if (ev.turn || ev.tap) audio::knobClick();

  net::tick(now);
  const bool up = net::connected();
  clockmod::tick(up);
  ota::tick(up);
  if (ota::active()) return;  // ArduinoOTA draws its own progress

  datasync::tick(now, up);
  if (datasync::takeNewData(g_data)) {
    g_hasData = true;
    g_dirty = true;
  }
  if (net::takeSettingsChanged()) {
    applySettings();
    datasync::requestNow();  // the data URL or token may have changed
    g_dirty = true;
  }

  // The state engine decides; everything below just follows it.
  const bool preview = g_preview >= 0;
  const bc::LocalNow liveNow = clockmod::localNow();
  const bc::LocalNow& now_ = preview ? g_previewNow : liveNow;
  const bc::BookClubData* data = preview ? &g_previewData : (g_hasData ? &g_data : nullptr);
  bc::DisplayState state;
  if (data) state = bc::computeState(*data, now_);
  else state.clockValid = now_.valid;

  audio::setQuietHours(quietHours(liveNow));
  audio::setGeigerRate(data && state.clockValid ? state.geigerClicksPerMin : 0);
  if (!preview && state.clockValid && state.quest == bc::Quest::MeetingDay &&
      g_jingledOn != liveNow.date && audio::canPlay()) {
    g_jingledOn = liveNow.date;
    audio::jingle();
  }
  audio::tick(now);

  const settings::Unit& u = settings::get();
  ui::Model m;
  m.data = data;
  m.state = state;
  m.now = now_;
  m.wifiUp = up;
  m.offlineLong = net::offlineLong();
  m.portal = net::portalActive();
  m.portalCancellable = g_hasData;  // first setup cannot be skipped
  m.apName = net::apName();
  m.hostname = net::hostname();
  m.ip = net::ipString();
  static uint32_t lastSync = settings::lastSyncEpoch();
  if (g_dirty) lastSync = settings::lastSyncEpoch();
  m.lastSyncEpoch = lastSync;
  m.syncStale = clockmod::ageSeconds(lastSync) > cfg::kSyncStaleSecs || !lastSync;
  m.sync = syncInfo(datasync::status());
  m.syncError = datasync::lastError();
  m.updatePct = datasync::status() == datasync::Status::Updating ? datasync::updateProgress() : -1;
  m.previewName = preview ? scenarios::get(g_preview).name : nullptr;
  m.sound = u.sound;
  m.autoBrightness = u.autoBrightness;
  m.fixedBrightness = u.fixedBrightness;
  m.theme = u.theme;
  m.effects = u.effects;
  m.owner = u.owner;
  m.stats = u.stats;
  m.lightRaw = backlight::rawReading();
  if (data) m.caps = bc::computeFunCaps(*data, now_, u.owner);

  const ui::Result r = ui::handle(ev, m, now);
  if (r.action != ui::Action::None) {
    doAction(r);
    return;  // redraw next pass with the updated model
  }
  if (r.redraw) g_dirty = true;

  if (backlight::blanked()) return;
  const uint32_t interval = (ui::animating(m) || m.updatePct >= 0) ? cfg::kFrameMsEffects : cfg::kFrameMsIdle;
  if (g_dirty || now - g_lastFrameMs >= interval) {
    ui::render(m, now);
    g_lastFrameMs = now;
    g_dirty = false;
  }
}
