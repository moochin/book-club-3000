// The advent calendar: 24 doors, one a day from 1 December, on a tab of its
// own that shows for all of December. This is the pure part: what's behind
// each door, which doors may open, and the year's record of opened doors.
// The tab itself is drawn by src/ui; src/settings keeps the record in NVS.
//
// Behind each door is a built-in line, unless the club file's "advent" block
// gives that day its own ("advent": {"7": "Bring a jumper to the Xmas do"}).
// Each unit keeps its own record: opening a door here opens it nowhere else.
#pragma once

#include <stdint.h>

#include "bc_data.h"
#include "bc_date.h"

namespace bc {

// The calendar's day for `now`: 1..24 from 1 to 24 December, 25 from
// Christmas Day to New Year's Eve (every door may open, doors missed while
// away included), 0 outside December or before the clock is set.
int adventDay(const LocalNow& now);

// Whether door `door` (1..24) may be opened on calendar day `today`.
bool adventCanOpen(int door, int today);

// The opened doors, one bit per door (bit 0 = door 1), kept with the year
// they belong to. A record from an earlier year means nothing is open yet.
uint32_t adventOpenedThisYear(int16_t recordYear, uint32_t mask, int16_t year);
inline bool adventIsOpen(uint32_t mask, int door) {
  return door >= 1 && door <= kAdventDoors && (mask >> (door - 1)) & 1;
}
inline uint32_t adventWithOpen(uint32_t mask, int door) {
  return door >= 1 && door <= kAdventDoors ? mask | (1UL << (door - 1)) : mask;
}
// True while a door that may open now is still shut (the tab's name lights up).
bool adventWaiting(uint32_t mask, int today);
// How many of the 24 doors are open.
int adventOpenCount(uint32_t mask);

// What's behind door `door` (1..24): the club file's line for that day if it
// has one, else the built-in one. "" for a door out of range.
const char* adventText(const BookClubData* data, int door);
// The built-in line alone (for tests and the checker).
const char* adventDefaultText(int door);

// The little picture on an opened door: one of kAdventIcons, by door.
enum class AdventIcon : uint8_t { Star, Tree, Bauble, Present, Snowflake, Candle, Count };
AdventIcon adventIcon(int door);

}  // namespace bc
