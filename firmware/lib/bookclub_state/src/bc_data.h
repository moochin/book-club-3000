// The parsed contents of bookclub.json.
//
// Fixed-size buffers throughout, so parsing never fragments the ESP32 heap and
// a hostile or oversized file can only be truncated, never crash the unit.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bc_date.h"

namespace bc {

constexpr int kTextLen = 48;
constexpr int kMaxHistory = 40;
// Side quests kept (the latest by date; older ones drop off first).
constexpr int kMaxSideQuests = 12;
constexpr int kMaxCapsPeople = 8;
constexpr int kNameLen = 24;
constexpr int kMaxBirthdays = 12;
constexpr int kVersionLen = 16;
constexpr int kUrlLen = 160;
constexpr int kNudgeTextLen = 41;      // 40 characters, as the nudge hub enforces
constexpr int kMaxNudgeMessages = 16;
constexpr int kSignalTextLen = 101;    // 100 characters, as the nudge hub enforces
constexpr int kMaxSignalQuotes = 16;
// Largest bookclub.json the firmware accepts (bytes).
constexpr size_t kMaxFileBytes = 12 * 1024;

struct Meeting {
  Date date;
  bool hasTime = false;
  int16_t minute = 0;  // minutes after local midnight, when hasTime
  char plan[kTextLen] = "";      // what's on: games, film, food...
  char location[kTextLen] = "";
};

struct FirmwareInfo {
  bool present = false;
  char version[kVersionLen] = "";
  char url[kUrlLen] = "";         // ILI9341 build
  char urlSt7789[kUrlLen] = "";   // ST7789 build
  // Optional "only": the owners whose units should take it (try a release on
  // one unit first). Empty: every unit.
  // `onlyBad`: "only" was there but named nobody usable (not a list, or no
  // names in it): no unit takes it, rather than every unit.
  int onlyCount = 0;
  bool onlyBad = false;
  char only[kMaxCapsPeople][kNameLen] = {};
  // The https URL for this build's panel, or nullptr.
  const char* urlFor(bool st7789) const;
  // Whether the unit owned by `owner` should install it ("only" allows it).
  bool forUnit(const char* owner) const;
};

// A per-person Fun Caps bonus or penalty from the "fun_caps" section.
struct CapsAdjust {
  char name[kNameLen] = "";
  int16_t adjust = 0;
  char note[kTextLen] = "";
};

// A birthday from the "birthdays" section (the year is optional and unused).
struct Birthday {
  char name[kNameLen] = "";
  int8_t month = 0;  // 1..12
  int8_t day = 0;    // 1..31
};

// The optional "nudges" block: where the nudge hub lives, and the club's own
// messages (if none, the units use their built-in set).
struct NudgeConfig {
  char url[kUrlLen] = "";  // https only
  int messageCount = 0;
  char messages[kMaxNudgeMessages][kNudgeTextLen] = {};
};

// The optional "signals" block: the club's own quotes to broadcast (if none,
// the units use their built-in set). Signals go through the nudge hub.
struct SignalConfig {
  int quoteCount = 0;
  char quotes[kMaxSignalQuotes][kSignalTextLen] = {};
};

// The optional "advent" block: the club's own lines for some of the advent
// calendar's doors ("advent": {"7": "..."}); the rest keep their built-in
// ones. The lines share one pool, so a few long ones don't cost 24 slots.
constexpr int kAdventDoors = 24;
constexpr int kAdventTextLen = 101;  // 100 characters, as the screen fits
constexpr int kAdventPoolLen = 1600;
struct AdventConfig {
  static constexpr uint16_t kNone = 0xFFFF;
  uint16_t offset[kAdventDoors];  // into pool, or kNone: the built-in line
  char pool[kAdventPoolLen] = "";
  int count = 0;                  // doors with the club's own line
  AdventConfig() {
    for (auto& o : offset) o = kNone;
  }
};

struct BookClubData {
  int version = 0;
  Meeting last;           // always has a valid date after a successful parse
  bool hasNext = false;   // false when "next" is null, missing or has no valid date
  Meeting next;
  int historyCount = 0;   // newest first; always contains `last`
  Meeting history[kMaxHistory];
  // Side quests: get-togethers that aren't a proper club meeting (families
  // invited, say). Oldest first. They never reset exposure.
  int sideQuestCount = 0;
  Meeting sideQuests[kMaxSideQuests];
  int capsAdjustCount = 0;
  CapsAdjust capsAdjust[kMaxCapsPeople];
  int birthdayCount = 0;
  Birthday birthdays[kMaxBirthdays];
  FirmwareInfo firmware;  // optional "firmware" block for over-the-air updates
  NudgeConfig nudges;     // optional "nudges" block
  SignalConfig signals;   // optional "signals" block
  AdventConfig advent;    // optional "advent" block
};

enum class ParseError : uint8_t {
  None = 0,
  Empty,
  TooLarge,
  BadJson,
  NotAnObject,
  NoLastDate,
};

const char* parseErrorText(ParseError e);

// Parses bookclub.json. On failure returns the error and leaves `out`
// untouched, so the caller can keep showing its cached copy.
ParseError parseBookClub(const char* json, size_t len, BookClubData& out);

}  // namespace bc
