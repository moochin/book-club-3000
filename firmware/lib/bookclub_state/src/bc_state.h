// The Book-Club 3000 state engine.
//
// One pure function turns (file contents, local "now") into everything the
// screen needs to know. No hardware calls: the sync and clock modules only
// gather inputs, the UI only draws what this returns.
#pragma once

#include <stdint.h>

#include "bc_data.h"
#include "bc_date.h"

namespace bc {

constexpr int kLevelCount = 5;

enum class Level : uint8_t {
  WellRested = 0,
  Healthy = 1,
  MinorSickness = 2,
  AdvancedSickness = 3,
  Critical = 4,
};

enum class Quest : uint8_t {
  MeetingDay,  // next.date is today
  FinalWeek,   // next.date is 1..7 days away
  Scheduled,   // next.date is 8+ days away
  Overdue,     // next.date has passed and the file still lists it
  NoDate,      // next missing, null, or before last
};

// The next side quest (a get-together that isn't a proper club meeting).
enum class SideQuest : uint8_t {
  None,      // nothing on or after today
  Upcoming,  // the next one is 1+ days away
  Today,
};

// Which mascot sprite to draw.
enum class Pose : uint8_t {
  Level0,      // thumbs up, grinning
  Level1,      // idle, occasional blink
  Level2,      // queasy, green tinge
  Level3,      // slumped, limbs flagging
  Level4,      // mid-transformation
  FinalWeek,   // excited
  MeetingDay,  // celebrating
  NoDate,      // waiting for orders
  Syncing,     // clock not set yet
  Birthday,    // someone in the club has a birthday today
};

// Tunable thresholds. Change the defaults here, nowhere else.
struct Thresholds {
  // First day of each exposure level.
  int16_t levelStartDay[kLevelCount] = {0, 14, 90, 137, 183};
  // The exposure bar is full from this many days.
  int16_t barFullDays = 183;
  // Days out at which the countdown becomes "final week".
  int16_t finalWeekDays = 7;
  // Under this many days, the countdown shows hours too.
  int16_t showHoursUnderDays = 2;
  // Average Geiger clicks per minute at each level (0 = silent).
  int16_t geigerClicksPerMin[kLevelCount] = {0, 0, 0, 8, 40};
};

struct DisplayState {
  bool clockValid = false;  // false: show the "syncing clock" screen

  // Exposure, from days since last.date.
  int32_t exposureDays = 0;  // clamped to >= 0
  Level level = Level::WellRested;
  int16_t barPermille = 0;   // 0..1000
  bool antiRad = false;      // final week / meeting day: "anti-rad dose administered"

  // Quest, from next.date.
  Quest quest = Quest::NoDate;
  int32_t daysToNext = 0;     // FinalWeek / Scheduled: whole days; Overdue: negative
  bool showHours = false;     // countdown is under showHoursUnderDays
  int32_t minutesToNext = 0;  // when showHours: minutes until next.time (or midnight)
  bool hasMeetingTime = false;
  int16_t meetingMinute = 0;

  Pose pose = Pose::Syncing;
  int16_t geigerClicksPerMin = 0;

  // Side quest. Shown alongside the main quest; never touches exposure.
  SideQuest side = SideQuest::None;
  int8_t sideIndex = -1;      // index into data.sideQuests
  int32_t daysToSide = 0;

  // Birthday: index into data.birthdays of someone whose birthday is today.
  int8_t birthdayIndex = -1;
};

const Thresholds& defaultThresholds();

// The core function. `now` is local Europe/London time.
DisplayState computeState(const BookClubData& data, const LocalNow& now,
                          const Thresholds& th = defaultThresholds());

// Text for the UI. All return static strings or write into `buf`.
const char* levelName(Level l);   // "MINOR SICKNESS"
const char* questName(Quest q);   // "SCHEDULED"
const char* poseName(Pose p);     // "LVL2 QUEASY" (placeholder-art labels)

// The one-line quest message for the STAT tab, e.g. "BOOK CLUB IN 15 DAYS".
// `variant` picks between alternative phrasings (pass a slow-changing counter).
void questLine(const DisplayState& s, char* buf, int len, uint32_t variant = 0);
// The countdown alone, e.g. "15 DAYS", "1 DAY 4 HRS", "20 HRS 5 MIN", "TODAY 19:30".
void countdownText(const DisplayState& s, char* buf, int len);
// The label under the exposure bar, e.g. "LVL 2  MINOR SICKNESS".
void exposureLabel(const DisplayState& s, char* buf, int len);
// The side-quest teaser, e.g. "SIDE QUEST IN 12 DAYS", "SIDE QUEST TOMORROW",
// "SIDE QUEST TODAY 14:00". Empty when there is none.
void sideQuestLine(const DisplayState& s, const BookClubData& data, char* buf, int len);

struct LogStats {
  int totalMeetings = 0;
  int32_t longestGapDays = 0;  // longest gap between consecutive meetings
  Date gapFrom, gapTo;         // the meetings either side of that gap
};
LogStats computeLogStats(const BookClubData& data);

// True if the birthday falls on `d` (29 Feb is marked on 28 Feb in other years).
bool isBirthdayOn(const Birthday& b, const Date& d);
// Case- and space-insensitive name match ("pete " == "Pete").
bool sameName(const char* a, const char* b);

// ---- Fun Caps ----------------------------------------------------------------
// The joke currency of goodwill from home. Everyone earns caps while they're
// at home, each proper club night spends some, side quests are free. The
// balance is replayed from the meeting history, so every unit agrees, then
// the owner's bonus or penalty from the file's "fun_caps" section is added.

struct CapsRules {
  int16_t start = 5;           // balance before the first meeting in the log
  int16_t daysPerCap = 14;     // one cap earned per fortnight at home
  int16_t costPerMeeting = 3;  // what a proper club night costs
  int16_t max = 10;            // the most you can bank
  int16_t min = -5;            // how far into the doghouse you can go
};
const CapsRules& defaultCapsRules();

struct FunCaps {
  bool valid = false;          // false before the clock is set
  int16_t balance = 0;         // after the adjustment, clamped to [min, max]
  int16_t earnedSinceLast = 0; // banked since the last club night
  int16_t adjust = 0;          // the owner's bonus (+) or penalty (-)
  char note[kTextLen] = "";    // the reason given for it, if any
  int16_t costPerMeeting = 0;
  int16_t max = 0;
};

// `owner` is matched case-insensitively against the "fun_caps" names.
FunCaps computeFunCaps(const BookClubData& data, const LocalNow& now, const char* owner,
                       const CapsRules& rules = defaultCapsRules());

// Compares dotted numeric versions ("1.2.10" > "1.2.9"). Returns <0, 0, >0.
// A leading 'v' is ignored; missing parts count as 0.
int compareVersions(const char* a, const char* b);

}  // namespace bc
