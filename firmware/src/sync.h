// Sync: fetches bookclub.json over HTTPS in a background FreeRTOS task, so
// the knob and screen stay responsive during a slow fetch. Also performs
// pull-style firmware updates when the file advertises a newer version.
#pragma once

#include <stdint.h>

#include "bc_data.h"

namespace datasync {

enum class Status : uint8_t { Idle, Fetching, Ok, Failed, Updating };

void begin();
// Schedules fetches: on boot, daily (+ up to 30 min jitter), retries on failure.
void tick(uint32_t nowMs, bool wifiUp);
void requestNow();

Status status();
const char* lastError();  // human-readable, for the About screen
// Copies new data into `out` if a fetch produced some since the last call.
bool takeNewData(bc::BookClubData& out);

// Firmware update progress, 0..100, while status() == Updating.
int updateProgress();

}  // namespace datasync
