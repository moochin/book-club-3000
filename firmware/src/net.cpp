#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ctype.h>

#include "config.h"
#include "settings.h"

namespace net {

static WiFiManager g_wm;
static char g_apName[33];
static char g_hostname[33];
static char g_ip[16] = "-";
static char g_rotationStr[2];
static char g_statsStr[32];

// Portal fields. Values are refreshed from settings each time the portal opens.
static WiFiManagerParameter g_header("<h3>Book-Club 3000 unit settings</h3>");
static WiFiManagerParameter g_pOwner("owner", "Owner name", "", 23);
static WiFiManagerParameter g_pTheme("theme", "Theme colour: green or amber", "green", 6);
static WiFiManagerParameter g_pStats("stats", "Owner card stats: seven numbers 1-10, e.g. 7,3,9,5,8,2,6", "", 31);
static WiFiManagerParameter g_pUrl("url", "Data file URL (raw bookclub.json)", "", 159);
static WiFiManagerParameter g_pToken("token", "GitHub token (private repo only). Blank keeps the saved one, '-' clears it", "", 127,
                                     "type='password' autocomplete='off'");
static WiFiManagerParameter g_pRotation("rot", "Screen rotation 0-3", "1", 1);
static WiFiManagerParameter g_pOta("otapass", "OTA password (optional). Blank keeps, '-' clears", "", 31,
                                   "type='password' autocomplete='off'");

static bool g_portal = false;
static bool g_settingsChanged = false;
static bool g_wasConnected = false;
static uint32_t g_downSinceMs = 0;
static uint32_t g_lastRetryMs = 0;

static void statsToString() {
  const auto& u = settings::get();
  snprintf(g_statsStr, sizeof g_statsStr, "%d,%d,%d,%d,%d,%d,%d", u.stats[0], u.stats[1],
           u.stats[2], u.stats[3], u.stats[4], u.stats[5], u.stats[6]);
}

static void refreshFields() {
  const auto& u = settings::get();
  statsToString();
  g_rotationStr[0] = (char)('0' + (u.rotation & 3));
  g_rotationStr[1] = 0;
  g_pOwner.setValue(u.owner, 23);
  g_pTheme.setValue(u.theme == settings::kThemeAmber ? "amber" : "green", 6);
  g_pStats.setValue(g_statsStr, 31);
  g_pUrl.setValue(u.dataUrl, 159);
  g_pToken.setValue("", 127);  // never echo secrets back into the page
  g_pRotation.setValue(g_rotationStr, 1);
  g_pOta.setValue("", 31);
}

static void trimCopy(char* dst, size_t cap, const char* src) {
  while (*src == ' ') ++src;
  strlcpy(dst, src, cap);
  size_t n = strlen(dst);
  while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\r' || dst[n - 1] == '\n')) dst[--n] = 0;
}

static void onSaveParams() {
  settings::Unit& u = settings::get();
  trimCopy(u.owner, sizeof u.owner, g_pOwner.getValue());

  char theme[8];
  trimCopy(theme, sizeof theme, g_pTheme.getValue());
  for (char* p = theme; *p; ++p) *p = (char)tolower(*p);
  u.theme = strncmp(theme, "amber", 5) == 0 ? settings::kThemeAmber : settings::kThemeGreen;

  // Stats: up to seven numbers separated by anything non-numeric.
  const char* s = g_pStats.getValue();
  for (int i = 0; i < settings::kStatCount && *s; ++i) {
    while (*s && !isdigit((unsigned char)*s)) ++s;
    if (!*s) break;
    u.stats[i] = (uint8_t)constrain(atoi(s), 1, 10);
    while (isdigit((unsigned char)*s)) ++s;
  }

  char url[160];
  trimCopy(url, sizeof url, g_pUrl.getValue());
  if (strncmp(url, "https://", 8) == 0) strlcpy(u.dataUrl, url, sizeof u.dataUrl);

  char secret[128];
  trimCopy(secret, sizeof secret, g_pToken.getValue());
  if (strcmp(secret, "-") == 0) u.token[0] = 0;
  else if (secret[0]) strlcpy(u.token, secret, sizeof u.token);

  trimCopy(secret, sizeof secret, g_pOta.getValue());
  if (strcmp(secret, "-") == 0) u.otaPass[0] = 0;
  else if (secret[0]) strlcpy(u.otaPass, secret, sizeof u.otaPass);

  const char* r = g_pRotation.getValue();
  if (r[0] >= '0' && r[0] <= '3') u.rotation = (uint8_t)(r[0] - '0');

  settings::save();
  g_settingsChanged = true;
  Serial.printf("[net] settings saved for %s\n", settings::displayName());
}

static void makeNames() {
  // AP: "BookClub-<owner or chip id>". Hostname: lower-case, DNS-safe.
  snprintf(g_apName, sizeof g_apName, "BookClub-%s", settings::displayName());
  char h[33] = "bookclub-";
  size_t n = strlen(h);
  for (const char* p = settings::displayName(); *p && n < sizeof h - 1; ++p) {
    const char c = (char)tolower((unsigned char)*p);
    if (isalnum((unsigned char)c)) h[n++] = c;
  }
  h[n] = 0;
  if (n == strlen("bookclub-")) strlcat(h, settings::chipId(), sizeof h);
  strlcpy(g_hostname, h, sizeof g_hostname);
}

static void startPortal(uint32_t timeoutS) {
  makeNames();
  refreshFields();
  g_wm.setConfigPortalTimeout(timeoutS);
  g_wm.startConfigPortal(g_apName);
  g_portal = true;
  Serial.printf("[net] setup portal open: %s\n", g_apName);
}

void begin() {
  makeNames();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(g_hostname);
  WiFi.setAutoReconnect(true);

  g_wm.setDebugOutput(false);
  g_wm.setConfigPortalBlocking(false);
  g_wm.setTitle("Book-Club 3000");
  g_wm.setDarkMode(true);
  g_wm.setHostname(g_hostname);
  g_wm.setConnectTimeout(20);
  g_wm.setSaveConnectTimeout(20);
  g_wm.setBreakAfterConfig(true);  // close the portal once saved, even if the join fails
  g_wm.setSaveParamsCallback(onSaveParams);
  std::vector<const char*> menu = {"wifi", "info", "exit"};
  g_wm.setMenu(menu);
  g_wm.addParameter(&g_header);
  g_wm.addParameter(&g_pOwner);
  g_wm.addParameter(&g_pTheme);
  g_wm.addParameter(&g_pStats);
  g_wm.addParameter(&g_pUrl);
  g_wm.addParameter(&g_pToken);
  g_wm.addParameter(&g_pRotation);
  g_wm.addParameter(&g_pOta);

  if (g_wm.getWiFiIsSaved()) {
    Serial.println("[net] connecting to saved network");
    WiFi.begin();
  } else {
    startPortal(0);  // first setup: stay open until done
  }
  g_downSinceMs = millis();
}

void tick(uint32_t now) {
  if (g_portal) {
    g_wm.process();
    if (!g_wm.getConfigPortalActive()) {
      g_portal = false;
      Serial.println("[net] portal closed");
      if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
      if (!WiFi.isConnected() && g_wm.getWiFiIsSaved()) WiFi.begin();
    }
  }

  const bool up = WiFi.isConnected();
  if (up && !g_wasConnected) {
    strlcpy(g_ip, WiFi.localIP().toString().c_str(), sizeof g_ip);
    Serial.printf("[net] connected, ip %s\n", g_ip);
  } else if (!up && g_wasConnected) {
    g_downSinceMs = now;
    strlcpy(g_ip, "-", sizeof g_ip);
    Serial.println("[net] disconnected");
  }
  g_wasConnected = up;

  // Background retry. The stack reconnects on its own after short drops; this
  // covers routers that were off for a long time.
  if (!up && !g_portal && g_wm.getWiFiIsSaved() && now - g_lastRetryMs >= cfg::kWifiRetryMs) {
    g_lastRetryMs = now;
    Serial.println("[net] retrying saved network");
    WiFi.disconnect();
    WiFi.begin();
  }
}

bool connected() { return WiFi.isConnected(); }
bool offlineLong() { return !connected() && millis() - g_downSinceMs >= cfg::kWifiOfflineNoticeMs; }
bool portalActive() { return g_portal; }
const char* apName() { return g_apName; }
const char* hostname() { return g_hostname; }
const char* ipString() { return g_ip; }

void openPortal() {
  if (g_portal) return;
  startPortal(cfg::kMenuPortalTimeoutS);
}

void closePortal() {
  if (!g_portal) return;
  g_wm.stopConfigPortal();
  g_portal = false;
  WiFi.mode(WIFI_STA);
  if (g_wm.getWiFiIsSaved()) WiFi.begin();
}

bool takeSettingsChanged() {
  const bool c = g_settingsChanged;
  g_settingsChanged = false;
  if (c) makeNames();
  return c;
}

}  // namespace net
