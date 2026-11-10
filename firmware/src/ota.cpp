#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "net.h"
#include "settings.h"
#include "ui.h"

namespace ota {

static bool g_started = false;
static volatile bool g_active = false;
static volatile int g_pct = 0;

// Called from inside ArduinoOTA.handle() while an upload is running; the main
// loop is blocked, so the progress screen is drawn from here.
static void drawProgress(int pct) { ui::drawUpdating("LAN UPDATE", pct); }

static void start() {
  ArduinoOTA.setHostname(net::hostname());
  const char* pass = settings::get().otaPass;
  if (pass[0]) ArduinoOTA.setPassword(pass);
  ArduinoOTA.setMdnsEnabled(true);
  ArduinoOTA.onStart([]() {
    g_active = true;
    g_pct = 0;
    Serial.println("[ota] start");
    drawProgress(0);
  });
  ArduinoOTA.onProgress([](unsigned int cur, unsigned int total) {
    const int pct = total ? (int)(cur * 100ULL / total) : 0;
    if (pct != g_pct) {
      g_pct = pct;
      drawProgress(pct);
    }
  });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", (unsigned)e);
    g_active = false;
  });
  ArduinoOTA.begin();
  g_started = true;
  Serial.printf("[ota] ready as %s.local\n", net::hostname());
}

void tick(bool wifiUp) {
  if (!wifiUp) return;
  if (!g_started) start();
  ArduinoOTA.handle();
}

bool active() { return g_active; }
int progress() { return g_pct; }

}  // namespace ota
