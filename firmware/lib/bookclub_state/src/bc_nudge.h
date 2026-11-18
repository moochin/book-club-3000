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
  uint64_t at = 0;  // when the hub took it (ms since 1970): /poll's ack
  bool re = false;  // a reply to one of ours: shown as one, with no reply back
};

// A nudge from a friend that isn't itself a reply can be answered. (The
// unit's own reminders have no sender.)
inline bool canReply(const Nudge& n) { return n.from[0] && !n.re; }

// The one-press replies offered on a received nudge.
int nudgeReplyCount();
const char* nudgeReply(int i);

// What a unit tells the hub about itself on each /poll, for the page's
// UNITS list (see worker/README.md).
struct UnitHealth {
  const char* fw = "";     // BC_FW_VERSION
  const char* fwst = "";   // "ON PROBATION", "1.6.0 ROLLED BACK", or ""
  const char* why = "";    // why it last started, as ABOUT says
  bool bad = false;        // that was a crash, watchdog or brownout
  const char* mem = "";    // ABOUT's MEMORY line
  uint32_t upMin = 0;      // minutes since it started
  const char* panel = "";  // "ili9341" or "st7789"
};

// "/poll?ack=AT&fw=...&..." with every value percent-encoded. False if it
// doesn't fit in `out`.
bool pollPath(uint64_t ack, const UnitHealth& h, char* out, size_t cap);

// GET /inbox or /poll -> {"nudges":[{"from","msg","at"}]}. Returns how many were read
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

// The hub's web page for the phone QR code: `hubUrl` with this unit's token
// after "#t=" (the page signs straight in, and the fragment never reaches the
// server), percent-encoded. With no token, just the page. False if it doesn't
// fit in `out`.
bool phoneLink(const char* hubUrl, const char* token, char* out, size_t cap);

}  // namespace bc
