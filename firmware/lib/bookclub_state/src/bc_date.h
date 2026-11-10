// Calendar maths for the Book-Club 3000 state engine.
//
// Pure C++: no Arduino or ESP-IDF headers, so it builds and runs on a PC
// for `pio test -e native`.
#pragma once

#include <stdint.h>
#include <time.h>

namespace bc {

// A calendar date in local UK time. A zero year means "not set".
struct Date {
  int16_t year = 0;
  int8_t month = 0;  // 1..12
  int8_t day = 0;    // 1..31

  bool valid() const;
  bool operator==(const Date& o) const {
    return year == o.year && month == o.month && day == o.day;
  }
  bool operator!=(const Date& o) const { return !(*this == o); }
};

// Local wall-clock "now": the date plus minutes since local midnight.
struct LocalNow {
  bool valid = false;  // false until the clock has synced (NTP)
  Date date;
  int16_t minuteOfDay = 0;  // 0..1439
};

bool isLeapYear(int year);
int daysInMonth(int year, int month);

// Day number counted from 1970-01-01 (proleptic Gregorian). Date must be valid.
int32_t daysFromCivil(const Date& d);
Date civilFromDays(int32_t days);

// Whole days from `from` to `to` (positive when `to` is later).
int32_t daysBetween(const Date& from, const Date& to);

// Strict parsers. They return false and leave the output untouched on any
// malformed or out-of-range input.
bool parseDate(const char* s, Date& out);        // "YYYY-MM-DD"
bool parseTime(const char* s, int16_t& minute);  // "HH:MM" -> minutes 0..1439

// Format helpers. `buf` must hold at least 11 / 6 bytes.
void formatDate(const Date& d, char* buf, int len);         // "2026-12-05"
void formatDateShort(const Date& d, char* buf, int len);    // "05 DEC 2026"
void formatTime(int16_t minute, char* buf, int len);        // "19:30"
const char* weekdayName(const Date& d);                     // "SAT"

// The POSIX TZ rule for Europe/London (GMT/BST, change at 01:00 UTC on the
// last Sundays of March and October).
extern const char* const kLondonPosixTz;

// Earliest epoch accepted as a real clock reading; anything earlier means
// NTP has not synced yet.
constexpr time_t kMinValidEpoch = 1735689600;  // 2025-01-01T00:00:00Z

// Converts a UTC epoch to local wall-clock time using the C library and the
// process TZ (set it with setenv("TZ", kLondonPosixTz) + tzset(), or
// configTzTime() on the ESP32). Returns an invalid LocalNow for epochs before
// kMinValidEpoch.
LocalNow localNowFromEpoch(time_t epoch);

}  // namespace bc
