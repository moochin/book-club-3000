// Native unit tests for the state engine: `pio test -e native`.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unity.h>

#include "bc_data.h"
#include "bc_date.h"
#include "bc_state.h"
#include "bc_text.h"

using namespace bc;

static Date D(const char* s) {
  Date d;
  TEST_ASSERT_TRUE_MESSAGE(parseDate(s, d), s);
  return d;
}

static LocalNow at(const char* date, int hh = 12, int mm = 0) {
  LocalNow n;
  n.valid = true;
  n.date = D(date);
  n.minuteOfDay = (int16_t)(hh * 60 + mm);
  return n;
}

// Builds the file the way the friends would write it, and runs it through
// the real parser.
static BookClubData file(const char* last, const char* next, const char* time = nullptr) {
  char json[512];
  char nextPart[160];
  if (next) {
    if (time)
      snprintf(nextPart, sizeof nextPart, "{\"date\":\"%s\",\"time\":\"%s\",\"plan\":\"TBC\"}", next, time);
    else
      snprintf(nextPart, sizeof nextPart, "{\"date\":\"%s\"}", next);
  } else {
    strcpy(nextPart, "null");
  }
  snprintf(json, sizeof json,
           "{\"version\":1,\"last\":{\"date\":\"%s\",\"plan\":\"Retro games + curry\"},"
           "\"next\":%s}",
           last, nextPart);
  BookClubData d;
  TEST_ASSERT_EQUAL_MESSAGE((int)ParseError::None, (int)parseBookClub(json, strlen(json), d), json);
  return d;
}

static void expect(const DisplayState& s, int days, Level lvl, Quest q) {
  TEST_ASSERT_TRUE(s.clockValid);
  TEST_ASSERT_EQUAL_INT32(days, s.exposureDays);
  TEST_ASSERT_EQUAL_INT((int)lvl, (int)s.level);
  TEST_ASSERT_EQUAL_STRING(questName(q), questName(s.quest));
}

// ---- The handover's state engine test table --------------------------------

void test_table_well_rested_no_date() {
  auto s = computeState(file("2026-08-15", nullptr), at("2026-08-20"));
  expect(s, 5, Level::WellRested, Quest::NoDate);
  TEST_ASSERT_EQUAL((int)Pose::NoDate, (int)s.pose);
}

void test_table_healthy_no_date() {
  auto s = computeState(file("2026-08-15", nullptr), at("2026-10-01"));
  expect(s, 47, Level::Healthy, Quest::NoDate);
}

void test_table_minor_scheduled_15_days() {
  auto s = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-20"));
  expect(s, 97, Level::MinorSickness, Quest::Scheduled);
  TEST_ASSERT_EQUAL_INT32(15, s.daysToNext);
  TEST_ASSERT_FALSE(s.showHours);
  TEST_ASSERT_EQUAL((int)Pose::Level2, (int)s.pose);
}

void test_table_minor_final_week_5_days() {
  auto s = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-30"));
  expect(s, 107, Level::MinorSickness, Quest::FinalWeek);
  TEST_ASSERT_EQUAL_INT32(5, s.daysToNext);
  TEST_ASSERT_TRUE(s.antiRad);
  TEST_ASSERT_EQUAL((int)Pose::FinalWeek, (int)s.pose);
}

void test_table_meeting_day() {
  auto s = computeState(file("2026-08-15", "2026-12-05"), at("2026-12-05"));
  expect(s, 112, Level::MinorSickness, Quest::MeetingDay);
  TEST_ASSERT_EQUAL((int)Pose::MeetingDay, (int)s.pose);
}

void test_table_day_after_overdue_no_reset() {
  auto s = computeState(file("2026-08-15", "2026-12-05"), at("2026-12-06"));
  expect(s, 113, Level::MinorSickness, Quest::Overdue);
  TEST_ASSERT_EQUAL_INT32(-1, s.daysToNext);
}

void test_table_critical_no_date() {
  auto s = computeState(file("2026-08-15", nullptr), at("2027-03-01"));
  expect(s, 198, Level::Critical, Quest::NoDate);
  TEST_ASSERT_EQUAL_INT16(1000, s.barPermille);
  TEST_ASSERT_EQUAL((int)Pose::Level4, (int)s.pose);
  TEST_ASSERT_TRUE(s.geigerClicksPerMin > 0);
}

void test_table_next_before_last_is_no_date() {
  auto s = computeState(file("2026-08-15", "2026-08-10"), at("2026-08-20"));
  expect(s, 5, Level::WellRested, Quest::NoDate);
}

void test_table_last_in_future_clamps() {
  auto s = computeState(file("2026-08-15", nullptr), at("2026-08-10"));
  expect(s, 0, Level::WellRested, Quest::NoDate);
  TEST_ASSERT_EQUAL_INT16(0, s.barPermille);
}

void test_table_clocks_forward_local_date() {
  // 2027-03-28 00:30 local, the night the clocks go forward.
  auto s = computeState(file("2026-08-15", nullptr), at("2027-03-28", 0, 30));
  expect(s, 225, Level::Critical, Quest::NoDate);
}

// The same row, but through the C library and the POSIX TZ string the device
// uses, from a UTC epoch either side of the 01:00 UTC change.
void test_epoch_to_local_across_dst() {
  setenv("TZ", kLondonPosixTz, 1);
  tzset();
  // 2027-03-28 00:30 UTC = 00:30 GMT (still winter time).
  LocalNow n = localNowFromEpoch(1806193800);
  TEST_ASSERT_TRUE(n.valid);
  TEST_ASSERT_TRUE(n.date == D("2027-03-28"));
  TEST_ASSERT_EQUAL_INT16(30, n.minuteOfDay);
  // 2027-03-28 01:30 UTC = 02:30 BST.
  n = localNowFromEpoch(1806193800 + 3600);
  TEST_ASSERT_EQUAL_INT16(150, n.minuteOfDay);
  // 2027-03-27 23:30 UTC = 23:30 GMT on the 27th.
  n = localNowFromEpoch(1806193800 - 3600);
  TEST_ASSERT_TRUE(n.date == D("2027-03-27"));
  // Summer: 2026-08-15 23:30 UTC is already 00:30 BST on the 16th.
  n = localNowFromEpoch(1786836600);
  TEST_ASSERT_TRUE(n.date == D("2026-08-16"));
  TEST_ASSERT_EQUAL_INT16(30, n.minuteOfDay);
  auto s = computeState(file("2026-08-15", nullptr), n);
  TEST_ASSERT_EQUAL_INT32(1, s.exposureDays);
}

void test_epoch_before_sync_is_invalid() {
  LocalNow n = localNowFromEpoch(1000);  // the ESP32 boots at 1970
  TEST_ASSERT_FALSE(n.valid);
  auto s = computeState(file("2026-08-15", nullptr), n);
  TEST_ASSERT_FALSE(s.clockValid);
  TEST_ASSERT_EQUAL((int)Pose::Syncing, (int)s.pose);
}

// ---- Level boundaries and the bar ------------------------------------------

void test_level_boundaries() {
  BookClubData d = file("2026-01-01", nullptr);
  struct { int day; Level lvl; } cases[] = {
      {0, Level::WellRested},     {13, Level::WellRested},      {14, Level::Healthy},
      {89, Level::Healthy},       {90, Level::MinorSickness},   {136, Level::MinorSickness},
      {137, Level::AdvancedSickness}, {182, Level::AdvancedSickness}, {183, Level::Critical},
      {400, Level::Critical},
  };
  for (auto& c : cases) {
    LocalNow n;
    n.valid = true;
    n.date = civilFromDays(daysFromCivil(d.last.date) + c.day);
    auto s = computeState(d, n);
    char msg[16];
    snprintf(msg, sizeof msg, "day %d", c.day);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)c.lvl, (int)s.level, msg);
  }
}

void test_bar_is_linear_to_183() {
  BookClubData d = file("2026-01-01", nullptr);
  LocalNow n;
  n.valid = true;
  n.date = civilFromDays(daysFromCivil(d.last.date) + 61);
  TEST_ASSERT_EQUAL_INT16(333, computeState(d, n).barPermille);
  n.date = civilFromDays(daysFromCivil(d.last.date) + 182);
  TEST_ASSERT_EQUAL_INT16(994, computeState(d, n).barPermille);
  n.date = civilFromDays(daysFromCivil(d.last.date) + 183);
  TEST_ASSERT_EQUAL_INT16(1000, computeState(d, n).barPermille);
}

void test_custom_thresholds() {
  Thresholds th;
  th.levelStartDay[1] = 7;
  auto s = computeState(file("2026-08-15", nullptr), at("2026-08-22"), th);
  TEST_ASSERT_EQUAL((int)Level::Healthy, (int)s.level);
}

// ---- Countdown wording -----------------------------------------------------

void test_countdown_days() {
  char buf[32];
  auto s = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-20"));
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("15 DAYS", buf);
  questLine(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("QUEST: BOOK CLUB IN 15 DAYS", buf);
}

void test_countdown_under_two_days_shows_hours() {
  char buf[32];
  // Tomorrow at 19:30, now 10:00: 1 day 9 hours 30 minutes.
  auto s = computeState(file("2026-08-15", "2026-12-05", "19:30"), at("2026-12-04", 10, 0));
  TEST_ASSERT_TRUE(s.showHours);
  TEST_ASSERT_EQUAL_INT32(33 * 60 + 30, s.minutesToNext);
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("1 DAY 9 HRS", buf);
  // 23:00 the night before: 20 h 30 min.
  s = computeState(file("2026-08-15", "2026-12-05", "19:30"), at("2026-12-04", 23, 0));
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("20 HRS 30 MIN", buf);
  // No time given: count to midnight.
  s = computeState(file("2026-08-15", "2026-12-05"), at("2026-12-04", 23, 0));
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("1 HR 0 MIN", buf);
  // Two days out: days only.
  s = computeState(file("2026-08-15", "2026-12-05", "19:30"), at("2026-12-03", 10, 0));
  TEST_ASSERT_FALSE(s.showHours);
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("2 DAYS", buf);
}

void test_meeting_day_shows_time() {
  char buf[32];
  auto s = computeState(file("2026-08-15", "2026-12-05", "19:30"), at("2026-12-05", 9, 0));
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("TODAY 19:30", buf);
  s = computeState(file("2026-08-15", "2026-12-05"), at("2026-12-05", 9, 0));
  countdownText(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("TODAY", buf);
}

void test_final_week_boundaries() {
  auto s7 = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-28"));
  TEST_ASSERT_EQUAL((int)Quest::FinalWeek, (int)s7.quest);
  auto s8 = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-27"));
  TEST_ASSERT_EQUAL((int)Quest::Scheduled, (int)s8.quest);
}

void test_labels() {
  char buf[40];
  auto s = computeState(file("2026-08-15", nullptr), at("2027-03-01"));
  exposureLabel(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("SEEK BOOK CLUB IMMEDIATELY", buf);
  s = computeState(file("2026-08-15", "2026-12-05"), at("2026-11-30"));
  exposureLabel(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("ANTI-RAD DOSE ADMINISTERED", buf);
  s = computeState(file("2026-08-15", "2026-12-05"), at("2026-12-06"));
  questLine(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("DEBRIEF PENDING: UPDATE THE LOG", buf);
}

// ---- Parsing ---------------------------------------------------------------

static const char* kSample = R"({
  "version": 1,
  "last": { "date": "2026-08-15", "plan": "Retro games + curry", "location": "The Bell, Winchester" },
  "next": { "date": "2026-12-05", "time": "19:30", "plan": "TBC", "location": "TBC" },
  "history": [
    { "date": "2026-08-15", "plan": "Retro games + curry", "location": "The Bell, Winchester" },
    { "date": "2026-03-02", "plan": "Pub, then co-op games", "location": "Pete's house" }
  ],
  "message": "Bring snacks",
  "someFutureField": { "ignored": true }
})";

void test_parse_sample() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kSample, strlen(kSample), d));
  TEST_ASSERT_EQUAL(1, d.version);
  TEST_ASSERT_TRUE(d.last.date == D("2026-08-15"));
  TEST_ASSERT_EQUAL_STRING("Retro games + curry", d.last.plan);
  TEST_ASSERT_TRUE(d.hasNext);
  TEST_ASSERT_TRUE(d.next.hasTime);
  TEST_ASSERT_EQUAL_INT16(19 * 60 + 30, d.next.minute);
  TEST_ASSERT_EQUAL_STRING("Bring snacks", d.message);
  TEST_ASSERT_EQUAL(2, d.historyCount);
  TEST_ASSERT_EQUAL_STRING("Pub, then co-op games", d.history[1].plan);
  TEST_ASSERT_FALSE(d.firmware.present);
}

void test_parse_rejects_bad_files_and_keeps_old() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kSample, strlen(kSample), d));
  const char* bad[] = {
      "",
      "{ not json",
      "[1,2,3]",
      "{\"last\":null}",
      "{\"last\":{\"date\":\"2026-02-30\"}}",
      "{\"last\":{\"date\":\"15/08/2026\"}}",
      "{\"next\":{\"date\":\"2026-12-05\"}}",
  };
  for (const char* b : bad) {
    TEST_ASSERT_NOT_EQUAL_MESSAGE((int)ParseError::None, (int)parseBookClub(b, strlen(b), d), b);
  }
  TEST_ASSERT_EQUAL_STRING("Retro games + curry", d.last.plan);  // untouched
  TEST_ASSERT_TRUE(d.hasNext);
}

void test_parse_next_variants() {
  BookClubData d;
  const char* omitted = "{\"last\":{\"date\":\"2026-08-15\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(omitted, strlen(omitted), d));
  TEST_ASSERT_FALSE(d.hasNext);
  TEST_ASSERT_EQUAL(1, d.historyCount);  // `last` is always in the log
  const char* badNext = "{\"last\":{\"date\":\"2026-08-15\"},\"next\":{\"date\":\"soon\",\"plan\":\"X\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(badNext, strlen(badNext), d));
  TEST_ASSERT_FALSE(d.hasNext);
  const char* badTime = "{\"last\":{\"date\":\"2026-08-15\"},\"next\":{\"date\":\"2026-12-05\",\"time\":\"7pm\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(badTime, strlen(badTime), d));
  TEST_ASSERT_TRUE(d.hasNext);
  TEST_ASSERT_FALSE(d.next.hasTime);
}

void test_parse_last_wins_over_history() {
  // History is out of order and its first entry disagrees with `last`.
  const char* j = R"({"last":{"date":"2026-08-15","plan":"Right"},
    "history":[{"date":"2026-03-02","plan":"B"},{"date":"2026-08-15","plan":"Wrong"},
               {"date":"2025-11-20","plan":"A"},{"date":"2026-09-01","plan":"Future"}]})";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(3, d.historyCount);
  TEST_ASSERT_EQUAL_STRING("Right", d.history[0].plan);
  TEST_ASSERT_EQUAL_STRING("B", d.history[1].plan);
  TEST_ASSERT_EQUAL_STRING("A", d.history[2].plan);
  LogStats st = computeLogStats(d);
  TEST_ASSERT_EQUAL(3, st.totalMeetings);
  TEST_ASSERT_EQUAL_INT32(166, st.longestGapDays);  // 2026-03-02 -> 2026-08-15
  TEST_ASSERT_TRUE(st.gapFrom == D("2026-03-02"));
}

void test_parse_accepts_legacy_book_field() {
  const char* j = R"({"last":{"date":"2026-08-15","book":"Old style"},
    "next":{"date":"2026-12-05","plan":"New style","book":"ignored"}})";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL_STRING("Old style", d.last.plan);
  TEST_ASSERT_EQUAL_STRING("New style", d.next.plan);
}

void test_parse_truncates_long_text() {
  char j[600];
  char longPlan[300];
  memset(longPlan, 'x', sizeof longPlan - 1);
  longPlan[sizeof longPlan - 1] = 0;
  snprintf(j, sizeof j, "{\"last\":{\"date\":\"2026-08-15\",\"plan\":\"%s\"}}", longPlan);
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(kTextLen - 1, (int)strlen(d.last.plan));
}

void test_parse_utf8_truncation_on_boundary() {
  // 30 x "é" (2 bytes each) = 60 bytes; must cut to 46 bytes, not 47.
  char j[200] = "{\"last\":{\"date\":\"2026-08-15\",\"plan\":\"";
  for (int i = 0; i < 30; ++i) strcat(j, "\xC3\xA9");
  strcat(j, "\"}}");
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(46, (int)strlen(d.last.plan));
}

void test_parse_firmware_block() {
  const char* j = R"({"last":{"date":"2026-08-15"},
    "firmware":{"version":"1.2.0","url":"https://example.com/firmware.bin"}})";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_TRUE(d.firmware.present);
  TEST_ASSERT_EQUAL_STRING("1.2.0", d.firmware.version);
  TEST_ASSERT_EQUAL_STRING("https://example.com/firmware.bin", d.firmware.urlFor(false));
  TEST_ASSERT_NULL(d.firmware.urlFor(true));  // no ST7789 binary listed: never cross-flash
  const char* insecure = R"({"last":{"date":"2026-08-15"},
    "firmware":{"version":"1.2.0","url":"http://example.com/firmware.bin"}})";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(insecure, strlen(insecure), d));
  TEST_ASSERT_NULL(d.firmware.urlFor(false));
}

void test_parse_too_large() {
  static char big[kMaxFileBytes + 10];
  memset(big, ' ', sizeof big);
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::TooLarge, (int)parseBookClub(big, sizeof big, d));
}

// ---- Dates and versions ----------------------------------------------------

void test_date_parsing() {
  Date d;
  TEST_ASSERT_TRUE(parseDate("2028-02-29", d));
  TEST_ASSERT_FALSE(parseDate("2027-02-29", d));
  TEST_ASSERT_FALSE(parseDate("2026-13-01", d));
  TEST_ASSERT_FALSE(parseDate("2026-1-01", d));
  TEST_ASSERT_FALSE(parseDate(nullptr, d));
  int16_t m;
  TEST_ASSERT_TRUE(parseTime("07:05", m));
  TEST_ASSERT_EQUAL_INT16(425, m);
  TEST_ASSERT_TRUE(parseTime("7:05", m));
  TEST_ASSERT_FALSE(parseTime("24:00", m));
  TEST_ASSERT_FALSE(parseTime("19:3", m));
}

void test_civil_round_trip() {
  for (int32_t n = 20000; n < 22000; n += 7) {
    TEST_ASSERT_EQUAL_INT32(n, daysFromCivil(civilFromDays(n)));
  }
  TEST_ASSERT_EQUAL_STRING("SAT", weekdayName(D("2026-12-05")));
}

void test_versions() {
  TEST_ASSERT_TRUE(compareVersions("1.2.10", "1.2.9") > 0);
  TEST_ASSERT_TRUE(compareVersions("v1.0", "1.0.0") == 0);
  TEST_ASSERT_TRUE(compareVersions("0.9.9", "1.0.0") < 0);
  TEST_ASSERT_TRUE(compareVersions("1.1.0-beta", "1.1.0") == 0);
}

void test_ascii_fold() {
  char out[64];
  asciiFold("Caf\xC3\xA9 \xE2\x80\x9C" "Bon\xE2\x80\x9D \xE2\x80\x94 Pete\xE2\x80\x99s", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("Cafe \"Bon\" - Pete's", out);
  asciiFold("\xF0\x9F\x93\x9A books", out, sizeof out);  // emoji
  TEST_ASSERT_EQUAL_STRING("? books", out);
  asciiFold("abcdef", out, 4);
  TEST_ASSERT_EQUAL_STRING("abc", out);
  asciiFold("bad \xC3", out, sizeof out);  // truncated sequence
  TEST_ASSERT_EQUAL_STRING("bad ?", out);
  upperAscii(out);
  TEST_ASSERT_EQUAL_STRING("BAD ?", out);
}

// ---- Side quests -------------------------------------------------------------

static const char* kSideJson = R"J({
  "last": { "date": "2026-08-15", "plan": "Retro games + curry" },
  "next": { "date": "2026-12-05", "time": "19:30" },
  "side_quests": [
    { "date": "2026-12-20", "time": "14:00", "plan": "Xmas do, families welcome", "location": "Pete's house" },
    { "date": "2026-09-10", "plan": "BBQ with the kids" },
    { "date": "not a date", "plan": "ignored" },
    { "date": "2026-11-25", "plan": "Bonfire night (late)" }
  ]
})J";

static BookClubData sideData() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kSideJson, strlen(kSideJson), d));
  return d;
}

void test_side_quests_parsed_oldest_first() {
  BookClubData d = sideData();
  TEST_ASSERT_EQUAL(3, d.sideQuestCount);
  TEST_ASSERT_EQUAL_STRING("BBQ with the kids", d.sideQuests[0].plan);
  TEST_ASSERT_EQUAL_STRING("Bonfire night (late)", d.sideQuests[1].plan);
  TEST_ASSERT_EQUAL_STRING("Xmas do, families welcome", d.sideQuests[2].plan);
  TEST_ASSERT_TRUE(d.sideQuests[2].hasTime);
  // Side quests never join the meeting log or its stats.
  TEST_ASSERT_EQUAL(1, d.historyCount);
}

void test_side_quest_never_resets_exposure() {
  BookClubData d = sideData();
  // Day after the BBQ side quest: exposure still counts from `last`.
  auto s = computeState(d, at("2026-09-11"));
  TEST_ASSERT_EQUAL_INT32(27, s.exposureDays);
  // On the side quest day itself, too.
  s = computeState(d, at("2026-12-20"));
  TEST_ASSERT_EQUAL_INT32(127, s.exposureDays);
  TEST_ASSERT_EQUAL((int)Level::MinorSickness, (int)s.level);
  TEST_ASSERT_EQUAL((int)Quest::Overdue, (int)s.quest);  // the main quest is unaffected
}

void test_side_quest_next_upcoming() {
  BookClubData d = sideData();
  auto s = computeState(d, at("2026-11-20"));
  TEST_ASSERT_EQUAL((int)SideQuest::Upcoming, (int)s.side);
  TEST_ASSERT_EQUAL_STRING("Bonfire night (late)", d.sideQuests[s.sideIndex].plan);
  TEST_ASSERT_EQUAL_INT32(5, s.daysToSide);
  char buf[40];
  sideQuestLine(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("SIDE QUEST IN 5 DAYS", buf);
}

void test_side_quest_today_tomorrow_and_none() {
  BookClubData d = sideData();
  char buf[40];
  auto s = computeState(d, at("2026-12-19"));
  sideQuestLine(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("SIDE QUEST TOMORROW 14:00", buf);
  s = computeState(d, at("2026-12-20", 9, 0));
  TEST_ASSERT_EQUAL((int)SideQuest::Today, (int)s.side);
  sideQuestLine(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("SIDE QUEST TODAY 14:00", buf);
  s = computeState(d, at("2026-12-21"));
  TEST_ASSERT_EQUAL((int)SideQuest::None, (int)s.side);
  sideQuestLine(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_side_quest_and_meeting_same_day() {
  const char* j = R"({"last":{"date":"2026-08-15"},"next":{"date":"2026-12-05"},
    "side_quests":[{"date":"2026-12-05","plan":"Families join after"}]})";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  auto s = computeState(d, at("2026-12-05"));
  TEST_ASSERT_EQUAL((int)Quest::MeetingDay, (int)s.quest);  // the meeting still wins
  TEST_ASSERT_EQUAL((int)SideQuest::Today, (int)s.side);
}

void test_side_quests_keep_the_latest_when_too_many() {
  char j[2048] = "{\"last\":{\"date\":\"2026-08-15\"},\"side_quests\":[";
  for (int i = 1; i <= kMaxSideQuests + 3; ++i) {
    char e[64];
    snprintf(e, sizeof e, "%s{\"date\":\"2027-01-%02d\"}", i > 1 ? "," : "", i);
    strcat(j, e);
  }
  strcat(j, "]}");
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(kMaxSideQuests, d.sideQuestCount);
  TEST_ASSERT_TRUE(d.sideQuests[0].date == D("2027-01-04"));  // three oldest dropped
}

// ---- Fun Caps ------------------------------------------------------------------

static const char* kCapsJson = R"J({
  "last": { "date": "2026-08-15" },
  "history": [ { "date": "2026-08-15" }, { "date": "2026-03-02" } ],
  "side_quests": [ { "date": "2026-09-05", "plan": "BBQ with the families" } ],
  "fun_caps": {
    "pete": { "adjust": -4, "note": "Forgot the anniversary" },
    "Dave": 2,
    "Sam": { "adjust": -20 },
    "Bad": "lots"
  }
})J";

static BookClubData capsData() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kCapsJson, strlen(kCapsJson), d));
  return d;
}

void test_caps_parsed() {
  BookClubData d = capsData();
  TEST_ASSERT_EQUAL(3, d.capsAdjustCount);  // "Bad" has no number: skipped
  TEST_ASSERT_EQUAL_STRING("pete", d.capsAdjust[0].name);
  TEST_ASSERT_EQUAL_INT16(-4, d.capsAdjust[0].adjust);
  TEST_ASSERT_EQUAL_STRING("Forgot the anniversary", d.capsAdjust[0].note);
  TEST_ASSERT_EQUAL_INT16(2, d.capsAdjust[1].adjust);
}

void test_caps_replayed_from_history() {
  BookClubData d = capsData();
  // 5 - 3 = 2; +11 for the 166-day gap (capped at 10); -3 = 7; then +1 per fortnight.
  FunCaps f = computeFunCaps(d, at("2026-08-15"), "Nobody");
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_EQUAL_INT16(7, f.balance);
  TEST_ASSERT_EQUAL_INT16(0, f.earnedSinceLast);
  f = computeFunCaps(d, at("2026-09-01"), "Nobody");   // 17 days: +1
  TEST_ASSERT_EQUAL_INT16(8, f.balance);
  TEST_ASSERT_EQUAL_INT16(1, f.earnedSinceLast);
  f = computeFunCaps(d, at("2026-11-20"), "Nobody");   // 97 days: +6, capped at 10
  TEST_ASSERT_EQUAL_INT16(10, f.balance);
  TEST_ASSERT_EQUAL_INT16(3, f.earnedSinceLast);
}

void test_caps_side_quests_are_free() {
  BookClubData d = capsData();
  BookClubData none = d;
  none.sideQuestCount = 0;
  TEST_ASSERT_EQUAL_INT16(computeFunCaps(none, at("2026-09-10"), "x").balance,
                          computeFunCaps(d, at("2026-09-10"), "x").balance);
}

void test_caps_owner_adjustments() {
  BookClubData d = capsData();
  FunCaps f = computeFunCaps(d, at("2026-09-01"), " Pete ");  // case and spaces ignored
  TEST_ASSERT_EQUAL_INT16(4, f.balance);                       // 8 - 4
  TEST_ASSERT_EQUAL_INT16(-4, f.adjust);
  TEST_ASSERT_EQUAL_STRING("Forgot the anniversary", f.note);
  f = computeFunCaps(d, at("2026-09-01"), "Dave");
  TEST_ASSERT_EQUAL_INT16(10, f.balance);                      // 8 + 2
  f = computeFunCaps(d, at("2026-09-01"), "Sam");
  TEST_ASSERT_EQUAL_INT16(-5, f.balance);                      // the doghouse floor
}

void test_caps_before_clock_and_custom_rules() {
  BookClubData d = capsData();
  LocalNow unset;
  TEST_ASSERT_FALSE(computeFunCaps(d, unset, "Pete").valid);
  CapsRules r;
  r.costPerMeeting = 1;
  r.max = 20;
  // 5 - 1 = 4; +11 = 15; -1 = 14.
  TEST_ASSERT_EQUAL_INT16(14, computeFunCaps(d, at("2026-08-15"), "x", r).balance);
}

// ---- Birthdays -------------------------------------------------------------------

static const char* kBirthdayJson = R"J({
  "last": { "date": "2026-08-15" },
  "next": { "date": "2026-12-05" },
  "birthdays": {
    "Pete": "03-14",
    "Dave": "1984-11-20",
    "Leap": "1996-02-29",
    "Meet": "12-05",
    "Bad": "14-03",
    "Worse": 314
  }
})J";

static BookClubData birthdayData() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kBirthdayJson, strlen(kBirthdayJson), d));
  return d;
}

void test_birthdays_parsed() {
  BookClubData d = birthdayData();
  TEST_ASSERT_EQUAL(4, d.birthdayCount);  // "Bad" and "Worse" skipped
  TEST_ASSERT_EQUAL_STRING("Pete", d.birthdays[0].name);
  TEST_ASSERT_EQUAL(3, d.birthdays[0].month);
  TEST_ASSERT_EQUAL(14, d.birthdays[0].day);
  TEST_ASSERT_EQUAL(11, d.birthdays[1].month);  // the year is accepted and ignored
}

void test_birthday_pose_and_index() {
  BookClubData d = birthdayData();
  auto s = computeState(d, at("2027-03-14"));
  TEST_ASSERT_EQUAL((int)Pose::Birthday, (int)s.pose);
  TEST_ASSERT_EQUAL_STRING("Pete", d.birthdays[s.birthdayIndex].name);
  s = computeState(d, at("2026-11-20"));
  TEST_ASSERT_EQUAL_STRING("Dave", d.birthdays[s.birthdayIndex].name);
  TEST_ASSERT_EQUAL_INT32(97, s.exposureDays);  // birthdays don't touch exposure
  s = computeState(d, at("2026-11-21"));
  TEST_ASSERT_EQUAL(-1, s.birthdayIndex);
  TEST_ASSERT_EQUAL((int)Pose::Level2, (int)s.pose);
}

void test_birthday_meeting_day_wins() {
  BookClubData d = birthdayData();
  auto s = computeState(d, at("2026-12-05"));
  TEST_ASSERT_EQUAL((int)Quest::MeetingDay, (int)s.quest);
  TEST_ASSERT_EQUAL((int)Pose::MeetingDay, (int)s.pose);
  TEST_ASSERT_EQUAL_STRING("Meet", d.birthdays[s.birthdayIndex].name);  // still known
}

void test_birthday_leap_day() {
  BookClubData d = birthdayData();
  auto s = computeState(d, at("2027-02-28"));  // not a leap year
  TEST_ASSERT_EQUAL_STRING("Leap", d.birthdays[s.birthdayIndex].name);
  s = computeState(d, at("2028-02-28"));       // leap year: wait for the 29th
  TEST_ASSERT_EQUAL(-1, s.birthdayIndex);
  s = computeState(d, at("2028-02-29"));
  TEST_ASSERT_EQUAL_STRING("Leap", d.birthdays[s.birthdayIndex].name);
}

void test_same_name() {
  TEST_ASSERT_TRUE(sameName(" pete ", "Pete"));
  TEST_ASSERT_FALSE(sameName("Pete", "Peter"));
  TEST_ASSERT_FALSE(sameName("", "Pete"));
}

void setUp() {}
void tearDown() {}

void test_next_birthday_countdown() {
  BookClubData d = birthdayData();
  auto s = computeState(d, at("2026-11-01"));
  TEST_ASSERT_EQUAL_STRING("Dave", d.birthdays[s.nextBirthdayIndex].name);
  TEST_ASSERT_EQUAL_INT32(19, s.daysToNextBirthday);
  TEST_ASSERT_TRUE(s.nextBirthdayDate == D("2026-11-20"));
  char buf[48];
  nextBirthdayNames(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("DAVE", buf);
  s = computeState(d, at("2026-11-20"));       // the day itself counts
  TEST_ASSERT_EQUAL_INT32(0, s.daysToNextBirthday);
  s = computeState(d, at("2026-12-06"));       // wraps into next year; 2027 has no 29 Feb
  TEST_ASSERT_EQUAL_STRING("Leap", d.birthdays[s.nextBirthdayIndex].name);
  TEST_ASSERT_TRUE(s.nextBirthdayDate == D("2027-02-28"));
  TEST_ASSERT_EQUAL_INT32(84, s.daysToNextBirthday);
}

void test_next_birthday_shared_and_none() {
  auto s = computeState(file("2026-08-15", nullptr), at("2026-09-01"));
  TEST_ASSERT_EQUAL(-1, s.nextBirthdayIndex);
  const char* two = R"J({"last":{"date":"2026-08-15"},"birthdays":{"Al":"10-01","Bo":"10-01","Cy":"12-01"}})J";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(two, strlen(two), d));
  s = computeState(d, at("2026-09-01"));
  TEST_ASSERT_EQUAL(2, s.birthdaysOnNextDay);
  char buf[48];
  nextBirthdayNames(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("AL & BO", buf);
  const char* three = R"J({"last":{"date":"2026-08-15"},"birthdays":{"Al":"10-01","Bo":"10-01","Cy":"10-01"}})J";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(three, strlen(three), d));
  s = computeState(d, at("2026-09-01"));
  nextBirthdayNames(s, d, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("AL +2", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_table_well_rested_no_date);
  RUN_TEST(test_table_healthy_no_date);
  RUN_TEST(test_table_minor_scheduled_15_days);
  RUN_TEST(test_table_minor_final_week_5_days);
  RUN_TEST(test_table_meeting_day);
  RUN_TEST(test_table_day_after_overdue_no_reset);
  RUN_TEST(test_table_critical_no_date);
  RUN_TEST(test_table_next_before_last_is_no_date);
  RUN_TEST(test_table_last_in_future_clamps);
  RUN_TEST(test_table_clocks_forward_local_date);
  RUN_TEST(test_epoch_to_local_across_dst);
  RUN_TEST(test_epoch_before_sync_is_invalid);
  RUN_TEST(test_level_boundaries);
  RUN_TEST(test_bar_is_linear_to_183);
  RUN_TEST(test_custom_thresholds);
  RUN_TEST(test_countdown_days);
  RUN_TEST(test_countdown_under_two_days_shows_hours);
  RUN_TEST(test_meeting_day_shows_time);
  RUN_TEST(test_final_week_boundaries);
  RUN_TEST(test_labels);
  RUN_TEST(test_parse_sample);
  RUN_TEST(test_parse_rejects_bad_files_and_keeps_old);
  RUN_TEST(test_parse_next_variants);
  RUN_TEST(test_parse_last_wins_over_history);
  RUN_TEST(test_parse_accepts_legacy_book_field);
  RUN_TEST(test_parse_truncates_long_text);
  RUN_TEST(test_parse_utf8_truncation_on_boundary);
  RUN_TEST(test_parse_firmware_block);
  RUN_TEST(test_parse_too_large);
  RUN_TEST(test_date_parsing);
  RUN_TEST(test_civil_round_trip);
  RUN_TEST(test_versions);
  RUN_TEST(test_ascii_fold);
  RUN_TEST(test_side_quests_parsed_oldest_first);
  RUN_TEST(test_side_quest_never_resets_exposure);
  RUN_TEST(test_side_quest_next_upcoming);
  RUN_TEST(test_side_quest_today_tomorrow_and_none);
  RUN_TEST(test_side_quest_and_meeting_same_day);
  RUN_TEST(test_side_quests_keep_the_latest_when_too_many);
  RUN_TEST(test_caps_parsed);
  RUN_TEST(test_caps_replayed_from_history);
  RUN_TEST(test_caps_side_quests_are_free);
  RUN_TEST(test_caps_owner_adjustments);
  RUN_TEST(test_caps_before_clock_and_custom_rules);
  RUN_TEST(test_birthdays_parsed);
  RUN_TEST(test_birthday_pose_and_index);
  RUN_TEST(test_birthday_meeting_day_wins);
  RUN_TEST(test_birthday_leap_day);
  RUN_TEST(test_next_birthday_countdown);
  RUN_TEST(test_next_birthday_shared_and_none);
  RUN_TEST(test_same_name);
  return UNITY_END();
}
