// LAN firmware updates with ArduinoOTA (`pio run -e cyd_ota -t upload`).
// Pull-style updates from a URL in bookclub.json are handled by sync.
#pragma once

#include <stdint.h>

namespace ota {

void tick(bool wifiUp);
bool active();
int progress();  // 0..100

}  // namespace ota
