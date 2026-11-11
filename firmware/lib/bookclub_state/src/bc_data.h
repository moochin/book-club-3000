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
constexpr int kMessageLen = 96;
constexpr int kMaxHistory = 40;
// Side quests kept (the latest by date; older ones drop off first).
constexpr int kMaxSideQuests = 12;
constexpr int kMaxCapsPeople = 8;
constexpr int kNameLen = 24;
constexpr int kMaxBirthdays = 12;
constexpr int kVersionLen = 16;
constexpr int kUrlLen = 160;
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
  // The https URL for this build's panel, or nullptr.
  const char* urlFor(bool st7789) const;
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

struct BookClubData {
  int version = 0;
  Meeting last;           // always has a valid date after a successful parse
  bool hasNext = false;   // false when "next" is null, missing or has no valid date
  Meeting next;
  int historyCount = 0;   // newest first; always contains `last`
  Meeting history[kMaxHistory];
  char message[kMessageLen] = "";
  // Side quests: get-togethers that aren't a proper club meeting (families
  // invited, say). Oldest first. They never reset exposure.
  int sideQuestCount = 0;
  Meeting sideQuests[kMaxSideQuests];
  int capsAdjustCount = 0;
  CapsAdjust capsAdjust[kMaxCapsPeople];
  int birthdayCount = 0;
  Birthday birthdays[kMaxBirthdays];
  FirmwareInfo firmware;  // optional "firmware" block for over-the-air updates
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
