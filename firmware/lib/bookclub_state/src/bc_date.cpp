#include "bc_date.h"

#include <stdio.h>
#include <string.h>

namespace bc {

const char* const kLondonPosixTz = "GMT0BST,M3.5.0/1,M10.5.0";

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
  static const int8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m - 1];
}

bool Date::valid() const {
  return year >= 1970 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
         day <= daysInMonth(year, month);
}

// Howard Hinnant's days_from_civil / civil_from_days.
int32_t daysFromCivil(const Date& d) {
  int y = d.year;
  const unsigned m = (unsigned)d.month;
  const unsigned dd = (unsigned)d.day;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

Date civilFromDays(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = (int32_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  Date out;
  out.year = (int16_t)(y + (m <= 2));
  out.month = (int8_t)m;
  out.day = (int8_t)d;
  return out;
}

int32_t daysBetween(const Date& from, const Date& to) {
  return daysFromCivil(to) - daysFromCivil(from);
}

static bool digits(const char* s, int n, int& out) {
  int v = 0;
  for (int i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  out = v;
  return true;
}

bool parseDate(const char* s, Date& out) {
  if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  int y, m, d;
  if (!digits(s, 4, y) || !digits(s + 5, 2, m) || !digits(s + 8, 2, d)) return false;
  Date t;
  t.year = (int16_t)y;
  t.month = (int8_t)m;
  t.day = (int8_t)d;
  if (!t.valid()) return false;
  out = t;
  return true;
}

bool parseTime(const char* s, int16_t& minute) {
  if (!s) return false;
  const size_t n = strlen(s);
  // Accept "H:MM" as well as "HH:MM"; people type both.
  if (n != 5 && n != 4) return false;
  const int hlen = (int)n - 3;
  if (s[hlen] != ':') return false;
  int h, m;
  if (!digits(s, hlen, h) || !digits(s + hlen + 1, 2, m)) return false;
  if (h > 23 || m > 59) return false;
  minute = (int16_t)(h * 60 + m);
  return true;
}

void formatDate(const Date& d, char* buf, int len) {
  snprintf(buf, (size_t)len, "%04d-%02d-%02d", d.year, d.month, d.day);
}

void formatDateShort(const Date& d, char* buf, int len) {
  static const char* const kMon[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  if (!d.valid()) {
    snprintf(buf, (size_t)len, "--");
    return;
  }
  snprintf(buf, (size_t)len, "%02d %s %04d", d.day, kMon[d.month - 1], d.year);
}

void formatTime(int16_t minute, char* buf, int len) {
  snprintf(buf, (size_t)len, "%02d:%02d", minute / 60, minute % 60);
}

const char* weekdayName(const Date& d) {
  static const char* const kDays[7] = {"THU", "FRI", "SAT", "SUN", "MON", "TUE", "WED"};
  int32_t n = daysFromCivil(d) % 7;  // 1970-01-01 was a Thursday
  if (n < 0) n += 7;
  return kDays[n];
}

LocalNow localNowFromEpoch(time_t epoch) {
  LocalNow now;
  if (epoch < kMinValidEpoch) return now;
  struct tm t;
  if (!localtime_r(&epoch, &t)) return now;
  now.valid = true;
  now.date.year = (int16_t)(t.tm_year + 1900);
  now.date.month = (int8_t)(t.tm_mon + 1);
  now.date.day = (int8_t)t.tm_mday;
  now.minuteOfDay = (int16_t)(t.tm_hour * 60 + t.tm_min);
  return now;
}

}  // namespace bc
