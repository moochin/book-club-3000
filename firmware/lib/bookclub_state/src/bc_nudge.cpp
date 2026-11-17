#include "bc_nudge.h"

#include <ArduinoJson.h>
#include <string.h>

#include "bc_text.h"

namespace bc {

// Built in, for a club that lives too far apart for the pub. The club file can
// replace them with its own in-jokes ("nudges": {"messages": [...]}).
static const char* const kDefaults[] = {
    "GET YOUR ASS ONLINE",
    "THINKING OF YOU, LOSER",
    "PARTY UP. NOW.",
    "WHERE ARE YOU, COWARD?",
    "I'M ON. YOU'RE NOT. SHAME.",
    "ONE MORE GAME?",
    "LAG IS NOT AN EXCUSE",
    "GG. YOU WERE TERRIBLE",
    "MISS YOUR STUPID FACE",
    "STOP HIDING. I SEE YOU",
    "CHECK THE GROUP CHAT",
    "FUN CAPS LOW. SEND HELP",
};
static constexpr int kDefaultCount = sizeof kDefaults / sizeof kDefaults[0];

static void copyFolded(const char* s, char* dst, size_t cap) {
  asciiFold(s ? s : "", dst, cap);  // the units' font is ASCII
}

int parseInbox(const char* json, size_t len, Nudge* out, int cap) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json, len)) return -1;
  JsonArrayConst list = doc["nudges"].as<JsonArrayConst>();
  if (list.isNull()) return -1;
  int n = 0;
  for (JsonVariantConst e : list) {
    if (n >= cap) break;
    const char* from = e["from"] | "";
    const char* msg = e["msg"] | "";
    if (!from[0] || !msg[0]) continue;
    out[n] = Nudge();
    copyFolded(from, out[n].from, sizeof out[n].from);
    copyFolded(msg, out[n].msg, sizeof out[n].msg);
    out[n].at = e["at"] | (uint64_t)0;
    ++n;
  }
  return n;
}

int parseRoster(const char* json, size_t len, char (*friends)[kNameLen], int cap, char* me,
                size_t meCap) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json, len)) return -1;
  JsonArrayConst list = doc["friends"].as<JsonArrayConst>();
  if (list.isNull()) return -1;
  if (me && meCap) copyFolded(doc["me"] | "", me, meCap);
  int n = 0;
  for (JsonVariantConst e : list) {
    if (n >= cap) break;
    const char* name = e.as<const char*>();
    if (!name || !name[0]) continue;
    // Keep the name exactly as the hub knows it: it's sent back as "to".
    strncpy(friends[n], name, kNameLen - 1);
    friends[n][kNameLen - 1] = '\0';
    ++n;
  }
  return n;
}

int nudgeMessageCount(const BookClubData* data) {
  if (data && data->nudges.messageCount > 0) return data->nudges.messageCount;
  return kDefaultCount;
}

const char* nudgeMessage(const BookClubData* data, int i) {
  if (data && data->nudges.messageCount > 0) {
    return (i >= 0 && i < data->nudges.messageCount) ? data->nudges.messages[i] : "";
  }
  return (i >= 0 && i < kDefaultCount) ? kDefaults[i] : "";
}

bool phoneLink(const char* hubUrl, const char* token, char* out, size_t cap) {
  if (!cap) return false;
  out[0] = 0;
  size_t n = 0;
  auto put = [&](char ch) {
    if (n + 1 >= cap) return false;
    out[n++] = ch;
    out[n] = 0;
    return true;
  };
  for (const char* p = hubUrl; *p && *p != '#'; ++p)  // any fragment of its own goes
    if (!put(*p)) return false;
  if (!token || !token[0]) return true;
  for (const char* p = "#t="; *p; ++p)
    if (!put(*p)) return false;
  static const char kHex[] = "0123456789ABCDEF";
  for (const unsigned char* p = (const unsigned char*)token; *p; ++p) {
    const bool plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                       *p == '-' || *p == '_' || *p == '.' || *p == '~';
    if (plain) {
      if (!put((char)*p)) return false;
    } else if (!put('%') || !put(kHex[*p >> 4]) || !put(kHex[*p & 15])) {
      return false;
    }
  }
  return true;
}

}  // namespace bc
