#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <vector>

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

static void copyStr(const char* s, char* dst, size_t cap) {
  dst[0] = '\0';
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

static void copyText(JsonVariantConst v, char* dst, size_t cap) {
  copyStr(v.is<const char*>() ? v.as<const char*>() : nullptr, dst, cap);
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
  // "plan" is what's on that night; "book" is accepted from older files.
  copyText(v["plan"].is<const char*>() ? v["plan"] : v["book"], m.plan, sizeof m.plan);
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

  // Side quests: valid entries, oldest first, keeping the latest few.
  std::vector<Meeting> sq;
  for (JsonVariantConst e : root["side_quests"].as<JsonArrayConst>()) {
    if (sq.size() >= 64) break;
    Meeting m;
    if (readMeeting(e, m)) sq.push_back(m);
  }
  std::stable_sort(sq.begin(), sq.end(), [](const Meeting& a, const Meeting& b) {
    return daysBetween(a.date, b.date) > 0;  // oldest first
  });
  const size_t skip = sq.size() > (size_t)kMaxSideQuests ? sq.size() - kMaxSideQuests : 0;
  d->sideQuestCount = 0;
  for (size_t i = skip; i < sq.size(); ++i) d->sideQuests[d->sideQuestCount++] = sq[i];

  // Fun Caps adjustments: "Name": number, or "Name": {"adjust": n, "note": "..."}.
  d->capsAdjustCount = 0;
  for (JsonPairConst kv : root["fun_caps"].as<JsonObjectConst>()) {
    if (d->capsAdjustCount >= kMaxCapsPeople) break;
    CapsAdjust& a = d->capsAdjust[d->capsAdjustCount];
    a = CapsAdjust();
    copyStr(kv.key().c_str(), a.name, sizeof a.name);
    JsonVariantConst v = kv.value();
    long n = 0;
    if (v.is<long>()) {
      n = v.as<long>();
    } else if (v.is<JsonObjectConst>()) {
      n = v["adjust"] | 0L;
      copyText(v["note"], a.note, sizeof a.note);
    } else {
      continue;
    }
    if (n > 99) n = 99;
    if (n < -99) n = -99;
    a.adjust = (int16_t)n;
    if (a.name[0]) ++d->capsAdjustCount;
  }

  // Birthdays: "Name": "MM-DD" or "YYYY-MM-DD".
  d->birthdayCount = 0;
  for (JsonPairConst kv : root["birthdays"].as<JsonObjectConst>()) {
    if (d->birthdayCount >= kMaxBirthdays) break;
    const char* v = kv.value().is<const char*>() ? kv.value().as<const char*>() : nullptr;
    if (!v) continue;
    char full[11];
    if (strlen(v) == 5) {
      snprintf(full, sizeof full, "2000-%s", v);  // a leap year, so 02-29 is valid
    } else {
      snprintf(full, sizeof full, "%s", v);
      if (strlen(v) == 10) memcpy(full, "2000", 4);
    }
    Date dt;
    if (!parseDate(full, dt)) continue;
    Birthday& b = d->birthdays[d->birthdayCount];
    b = Birthday();
    copyStr(kv.key().c_str(), b.name, sizeof b.name);
    b.month = dt.month;
    b.day = dt.day;
    if (b.name[0]) ++d->birthdayCount;
  }

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
