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
      snprintf(nextPart, sizeof nextPart, "{\"date\":\"%s\",\"time\":\"%s\",\"book\":\"TBC\"}", next, time);
    else
      snprintf(nextPart, sizeof nextPart, "{\"date\":\"%s\"}", next);
  } else {
    strcpy(nextPart, "null");
  }
  snprintf(json, sizeof json,
           "{\"version\":1,\"last\":{\"date\":\"%s\",\"book\":\"Project Hail Mary\"},"
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
  "last": { "date": "2026-08-15", "book": "Project Hail Mary", "location": "The Bell, Winchester" },
  "next": { "date": "2026-12-05", "time": "19:30", "book": "TBC", "location": "TBC" },
  "history": [
    { "date": "2026-08-15", "book": "Project Hail Mary", "location": "The Bell, Winchester" },
    { "date": "2026-03-02", "book": "Piranesi", "location": "Pete's house" }
  ],
  "message": "Bring snacks",
  "someFutureField": { "ignored": true }
})";

void test_parse_sample() {
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(kSample, strlen(kSample), d));
  TEST_ASSERT_EQUAL(1, d.version);
  TEST_ASSERT_TRUE(d.last.date == D("2026-08-15"));
  TEST_ASSERT_EQUAL_STRING("Project Hail Mary", d.last.book);
  TEST_ASSERT_TRUE(d.hasNext);
  TEST_ASSERT_TRUE(d.next.hasTime);
  TEST_ASSERT_EQUAL_INT16(19 * 60 + 30, d.next.minute);
  TEST_ASSERT_EQUAL_STRING("Bring snacks", d.message);
  TEST_ASSERT_EQUAL(2, d.historyCount);
  TEST_ASSERT_EQUAL_STRING("Piranesi", d.history[1].book);
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
  TEST_ASSERT_EQUAL_STRING("Project Hail Mary", d.last.book);  // untouched
  TEST_ASSERT_TRUE(d.hasNext);
}

void test_parse_next_variants() {
  BookClubData d;
  const char* omitted = "{\"last\":{\"date\":\"2026-08-15\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(omitted, strlen(omitted), d));
  TEST_ASSERT_FALSE(d.hasNext);
  TEST_ASSERT_EQUAL(1, d.historyCount);  // `last` is always in the log
  const char* badNext = "{\"last\":{\"date\":\"2026-08-15\"},\"next\":{\"date\":\"soon\",\"book\":\"X\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(badNext, strlen(badNext), d));
  TEST_ASSERT_FALSE(d.hasNext);
  const char* badTime = "{\"last\":{\"date\":\"2026-08-15\"},\"next\":{\"date\":\"2026-12-05\",\"time\":\"7pm\"}}";
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(badTime, strlen(badTime), d));
  TEST_ASSERT_TRUE(d.hasNext);
  TEST_ASSERT_FALSE(d.next.hasTime);
}

void test_parse_last_wins_over_history() {
  // History is out of order and its first entry disagrees with `last`.
  const char* j = R"({"last":{"date":"2026-08-15","book":"Right"},
    "history":[{"date":"2026-03-02","book":"B"},{"date":"2026-08-15","book":"Wrong"},
               {"date":"2025-11-20","book":"A"},{"date":"2026-09-01","book":"Future"}]})";
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(3, d.historyCount);
  TEST_ASSERT_EQUAL_STRING("Right", d.history[0].book);
  TEST_ASSERT_EQUAL_STRING("B", d.history[1].book);
  TEST_ASSERT_EQUAL_STRING("A", d.history[2].book);
  LogStats st = computeLogStats(d);
  TEST_ASSERT_EQUAL(3, st.totalMeetings);
  TEST_ASSERT_EQUAL_INT32(166, st.longestGapDays);  // 2026-03-02 -> 2026-08-15
  TEST_ASSERT_TRUE(st.gapFrom == D("2026-03-02"));
}

void test_parse_truncates_long_text() {
  char j[600];
  char longBook[300];
  memset(longBook, 'x', sizeof longBook - 1);
  longBook[sizeof longBook - 1] = 0;
  snprintf(j, sizeof j, "{\"last\":{\"date\":\"2026-08-15\",\"book\":\"%s\"}}", longBook);
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(kTextLen - 1, (int)strlen(d.last.book));
}

void test_parse_utf8_truncation_on_boundary() {
  // 30 x "é" (2 bytes each) = 60 bytes; must cut to 46 bytes, not 47.
  char j[200] = "{\"last\":{\"date\":\"2026-08-15\",\"book\":\"";
  for (int i = 0; i < 30; ++i) strcat(j, "\xC3\xA9");
  strcat(j, "\"}}");
  BookClubData d;
  TEST_ASSERT_EQUAL((int)ParseError::None, (int)parseBookClub(j, strlen(j), d));
  TEST_ASSERT_EQUAL(46, (int)strlen(d.last.book));
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

void setUp() {}
void tearDown() {}

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
  RUN_TEST(test_parse_truncates_long_text);
  RUN_TEST(test_parse_utf8_truncation_on_boundary);
  RUN_TEST(test_parse_firmware_block);
  RUN_TEST(test_parse_too_large);
  RUN_TEST(test_date_parsing);
  RUN_TEST(test_civil_round_trip);
  RUN_TEST(test_versions);
  RUN_TEST(test_ascii_fold);
  return UNITY_END();
}
