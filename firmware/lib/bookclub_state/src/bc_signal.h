// Signals: quotes broadcast to the whole club through the nudge hub, each one
// gone 24 hours after it was sent ("a signal from the wasteland"). This is
// the pure part: parsing the hub's reply, the quotes on offer and the "time
// left" text. Networking lives in src/nudge, next to the nudges.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bc_data.h"

namespace bc {

constexpr int kSignalMsgLen = kSignalTextLen;
constexpr int kMaxSignals = 8;            // one per unit, as the hub keeps
constexpr uint32_t kSignalLifeSecs = 24UL * 60 * 60;

struct Signal {
  char from[kNameLen] = "";
  char msg[kSignalMsgLen] = "";
  uint32_t at = 0;    // when it was sent, seconds since 1970 (also its id)
  uint32_t left = 0;  // seconds until it's gone, as of the hub's reply
};

// GET /signals -> {"signals":[{"from","msg","at","left"}]} ("at" in ms).
// Returns how many live ones were read into `out` (at most `cap`), or -1 if
// the reply isn't valid.
int parseSignals(const char* json, size_t len, Signal* out, int cap);

// The quotes a unit can broadcast: the club file's "signals.quotes" if it has
// any, else the built-in set.
int signalQuoteCount(const BookClubData* data);
const char* signalQuote(const BookClubData* data, int i);

// "23H LEFT", "45M LEFT", "1M LEFT"; "GONE" at zero.
void signalLeftText(uint32_t leftSecs, char* buf, size_t cap);

}  // namespace bc
