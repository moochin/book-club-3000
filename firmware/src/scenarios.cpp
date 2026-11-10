#include "scenarios.h"

#include <stdio.h>
#include <string.h>

namespace scenarios {

const char* const kSampleJson = R"JSON({
  "version": 1,
  "last": { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
  "next": { "date": "2026-12-05", "time": "19:30", "plan": "Film night + pizza", "location": "TBC" },
  "history": [
    { "date": "2026-08-15", "plan": "Retro games + curry", "location": "Pete's house" },
    { "date": "2026-03-02", "plan": "Pub, then co-op games", "location": "The Bell, Winchester" },
    { "date": "2025-11-20", "plan": "Horror double bill", "location": "Pete's house" },
    { "date": "2025-07-12", "plan": "BBQ + beers", "location": "Riverside" },
    { "date": "2025-03-08", "plan": "Racing game tournament", "location": "Pete's house" },
    { "date": "2024-10-19", "plan": "Pub quiz", "location": "The Bell, Winchester" }
  ],
  "message": "Bring snacks"
})JSON";

// The handover's state engine test table, plus a few extra states.
static const Scenario kScenarios[] = {
    {"L0 WELL RESTED / NO DATE", "2026-08-15", nullptr, nullptr, "2026-08-20", 12, 0},
    {"L1 HEALTHY / NO DATE", "2026-08-15", nullptr, nullptr, "2026-10-01", 12, 0},
    {"L2 MINOR / SCHEDULED 15D", "2026-08-15", "2026-12-05", "19:30", "2026-11-20", 12, 0},
    {"L2 MINOR / FINAL WEEK 5D", "2026-08-15", "2026-12-05", "19:30", "2026-11-30", 12, 0},
    {"FINAL WEEK / UNDER 2 DAYS", "2026-08-15", "2026-12-05", "19:30", "2026-12-04", 21, 15},
    {"MEETING DAY", "2026-08-15", "2026-12-05", "19:30", "2026-12-05", 12, 0},
    {"OVERDUE (DAY AFTER)", "2026-08-15", "2026-12-05", "19:30", "2026-12-06", 12, 0},
    {"L3 ADVANCED / SCHEDULED", "2026-08-15", "2027-02-20", nullptr, "2027-01-05", 12, 0},
    {"L3 ADVANCED / NO DATE", "2026-08-15", nullptr, nullptr, "2027-01-20", 12, 0},
    {"L4 CRITICAL / NO DATE", "2026-08-15", nullptr, nullptr, "2027-03-01", 12, 0},
    {"NEXT BEFORE LAST", "2026-08-15", "2026-08-10", nullptr, "2026-08-20", 12, 0},
    {"LAST IN FUTURE", "2026-08-15", nullptr, nullptr, "2026-08-10", 12, 0},
    {"CLOCKS FORWARD 00:30", "2026-08-15", nullptr, nullptr, "2027-03-28", 0, 30},
};

int count() { return sizeof kScenarios / sizeof kScenarios[0]; }
const Scenario& get(int i) { return kScenarios[i]; }

bool build(int i, bc::BookClubData& data, bc::LocalNow& now) {
  if (i < 0 || i >= count()) return false;
  const Scenario& s = kScenarios[i];
  if (bc::parseBookClub(kSampleJson, strlen(kSampleJson), data) != bc::ParseError::None) return false;
  // Override last and next from the scenario, keep the sample's other fields.
  if (!bc::parseDate(s.last, data.last.date)) return false;
  data.history[0].date = data.last.date;
  data.hasNext = s.next && bc::parseDate(s.next, data.next.date);
  data.next.hasTime = data.hasNext && s.time && bc::parseTime(s.time, data.next.minute);
  now.valid = bc::parseDate(s.today, now.date);
  now.minuteOfDay = (int16_t)(s.hh * 60 + s.mm);
  return now.valid;
}

}  // namespace scenarios
