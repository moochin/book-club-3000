// Nudges: short messages between the club's units, passed through a small
// Cloudflare Worker (worker/ in the repo). This is the pure part: parsing the
// hub's replies and the messages on offer. Networking lives in src/nudge.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bc_data.h"

namespace bc {

constexpr int kNudgeMsgLen = kNudgeTextLen;
constexpr int kMaxInbox = 20;      // as the hub keeps
constexpr int kMaxFriends = 8;

struct Nudge {
  char from[kNameLen] = "";
  char msg[kNudgeMsgLen] = "";
};

// GET /inbox -> {"nudges":[{"from","msg","at"}]}. Returns how many were read
// into `out` (at most `cap`), or -1 if the reply isn't valid.
int parseInbox(const char* json, size_t len, Nudge* out, int cap);

// GET /roster -> {"me":"Pete","friends":["Dave","Sam"]}. Returns the friend
// count (at most `cap`), or -1 if the reply isn't valid.
int parseRoster(const char* json, size_t len, char (*friends)[kNameLen], int cap, char* me,
                size_t meCap);

// The messages a unit can send: the club file's "nudges.messages" if it has
// any, else the built-in set.
int nudgeMessageCount(const BookClubData* data);
const char* nudgeMessage(const BookClubData* data, int i);

}  // namespace bc
