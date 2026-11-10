#include <ArduinoJson.h>
#include <string.h>

#include <algorithm>
#include <memory>

#include "bc_data.h"

namespace bc {

const char* parseErrorText(ParseError e) {
  switch (e) {
    case ParseError::None: return "ok";
    case ParseError::Empty: return "empty file";
    case ParseError::TooLarge: return "file too large";
    case ParseError::BadJson: return "not valid JSON";
    case ParseError::NotAnObject: return "not a JSON object";
    case ParseError::NoLastDate: return "missing or bad last.date";
  }
  return "?";
}

const char* FirmwareInfo::urlFor(bool st7789) const {
  const char* u = st7789 ? urlSt7789 : url;
  return present && strncmp(u, "https://", 8) == 0 ? u : nullptr;
}

static void copyText(JsonVariantConst v, char* dst, size_t cap) {
  dst[0] = '\0';
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  if (!s) return;
  // Truncate on a UTF-8 character boundary.
  size_t n = strlen(s);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;
  }
  memcpy(dst, s, n);
  dst[n] = '\0';
}

// Returns false if the meeting has no valid date.
static bool readMeeting(JsonVariantConst v, Meeting& m) {
  m = Meeting();
  if (!v.is<JsonObjectConst>()) return false;
  if (!parseDate(v["date"].as<const char*>(), m.date)) return false;
  int16_t minute;
  if (parseTime(v["time"].as<const char*>(), minute)) {
    m.hasTime = true;
    m.minute = minute;
  }
  copyText(v["book"], m.book, sizeof m.book);
  copyText(v["location"], m.location, sizeof m.location);
  return true;
}

ParseError parseBookClub(const char* json, size_t len, BookClubData& out) {
  if (!json || len == 0) return ParseError::Empty;
  if (len > kMaxFileBytes) return ParseError::TooLarge;

  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return ParseError::BadJson;
  if (!doc.is<JsonObjectConst>()) return ParseError::NotAnObject;
  JsonObjectConst root = doc.as<JsonObjectConst>();

  // Build into a heap copy so a failure leaves `out` untouched and the large
  // struct does not sit on a small task stack.
  std::unique_ptr<BookClubData> d(new BookClubData());
  d->version = root["version"] | 0;
  if (!readMeeting(root["last"], d->last)) return ParseError::NoLastDate;

  if (readMeeting(root["next"], d->next)) {
    // A next date before the last one is stale data: treat as "no date".
    d->hasNext = daysBetween(d->last.date, d->next.date) >= 0;
    if (!d->hasNext) d->next = Meeting();
  }

  // History: keep valid entries, newest first, and make sure `last` is in it
  // (if the two disagree, `last` wins).
  int n = 0;
  for (JsonVariantConst e : root["history"].as<JsonArrayConst>()) {
    if (n >= kMaxHistory) break;
    Meeting m;
    if (readMeeting(e, m)) d->history[n++] = m;
  }
  std::stable_sort(d->history, d->history + n, [](const Meeting& a, const Meeting& b) {
    return daysBetween(b.date, a.date) > 0;  // newest first
  });
  // Drop entries newer than `last` (they cannot have happened yet) and
  // replace an entry on the same date with `last` itself.
  int w = 0;
  for (int r = 0; r < n; ++r) {
    if (daysBetween(d->last.date, d->history[r].date) >= 0) continue;
    d->history[w++] = d->history[r];
  }
  n = w;
  if (n >= kMaxHistory) n = kMaxHistory - 1;
  memmove(&d->history[1], &d->history[0], sizeof(Meeting) * (size_t)n);
  d->history[0] = d->last;
  d->historyCount = n + 1;

  copyText(root["message"], d->message, sizeof d->message);

  JsonVariantConst fw = root["firmware"];
  if (fw.is<JsonObjectConst>()) {
    copyText(fw["version"], d->firmware.version, sizeof d->firmware.version);
    copyText(fw["url"], d->firmware.url, sizeof d->firmware.url);
    copyText(fw["url_st7789"], d->firmware.urlSt7789, sizeof d->firmware.urlSt7789);
    d->firmware.present = d->firmware.version[0] != 0;
  }

  out = *d;
  return ParseError::None;
}

}  // namespace bc
