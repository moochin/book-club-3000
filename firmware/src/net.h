// Wi-Fi: first-time setup through a WiFiManager captive portal (with the
// per-unit settings as extra fields), then quiet background reconnection.
#pragma once

#include <stdint.h>

namespace net {

void begin();
void tick(uint32_t nowMs);

bool connected();
// True when the network has been down for more than 5 minutes.
bool offlineLong();
// The portal is open: the screen should show setup instructions.
bool portalActive();
const char* apName();
const char* hostname();
const char* ipString();

// Opens the setup portal (menu > Wi-Fi reset). The saved network is kept
// until a new one is saved, so cancelling leaves the unit as it was.
void openPortal();
void closePortal();

// Set when the portal saved new settings; the caller applies and clears it.
bool takeSettingsChanged();

}  // namespace net
