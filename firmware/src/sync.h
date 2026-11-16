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

// Before a release download (the update itself, or fwguard's check on
// probation) the sync task asks for the screen's 38 KB back: true while it
// wants it. The main loop frees the canvas, then calls ramGiven().
bool wantsRam();
void ramGiven();

// Firmware update progress, 0..100, while status() == Updating.
int updateProgress();

// fwguard's proof that the update path works, counted per fetch since
// start-up. On probation, when the club file names a release for this panel,
// it counts only once that release's download URL has answered (any HTTP
// status, through GitHub's redirect). Otherwise, any HTTP answer for the club
// file counts: a typo in it mustn't roll back a good firmware.
uint32_t reachedCount();
// Fetches finished (good or bad) since start-up.
uint32_t fetchesDone();
// How long the current fetch has been running (0 when idle), so a stuck one
// can be spotted. Excludes a firmware download, which can take minutes.
uint32_t busyForMs(uint32_t nowMs);

// One HTTPS connection at a time: each TLS session needs ~40 KB of RAM, and
// this board has no PSRAM. The sync task and the nudge task both take this.
// begin() creates it, so call datasync::begin() before nudge::begin().
void lockTls();
void unlockTls();

}  // namespace datasync
