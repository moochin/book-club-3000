#include "bc_signal.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "bc_text.h"

namespace bc {

// Built in: lines from books old enough to be anyone's, picked for life after
// the bombs, and a few of our own. The club file can replace them with its
// own ("signals": {"quotes": [...]}).
static const char* const kDefaults[] = {
    "Look on my works, ye Mighty, and despair! - Shelley",
    "Round the decay of that colossal wreck, boundless and bare - Shelley",
    "Water, water, every where, nor any drop to drink. - Coleridge",
    "Things fall apart; the centre cannot hold. - Yeats",
    "Hope is the thing with feathers that perches in the soul. - Dickinson",
    "It was the best of times, it was the worst of times. - Dickens",
    "Once more unto the breach, dear friends, once more. - Shakespeare",
    "We few, we happy few, we band of brothers. - Shakespeare",
    "Hell is empty and all the devils are here. - Shakespeare",
    "All hope abandon, ye who enter here. - Dante",
    "I am not afraid of storms, for I am learning how to sail my ship. - Alcott",
    "Whatever our souls are made of, his and mine are the same. - Bronte",
    "STILL ALIVE OUT HERE. BARELY. SEND SNACKS.",
    "SCAVENGED A GOOD BOOK TODAY. WILL REPORT BACK.",
    "THE RADS ARE HIGH BUT MORALE IS HIGHER",
    "IF YOU CAN READ THIS, THE CLUB LIVES ON",
};
static constexpr int kDefaultCount = sizeof kDefaults / sizeof kDefaults[0];

int parseSignals(const char* json, size_t len, Signal* out, int cap) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json, len)) return -1;
  JsonArrayConst list = doc["signals"].as<JsonArrayConst>();
  if (list.isNull()) return -1;
  int n = 0;
  for (JsonVariantConst e : list) {
    if (n >= cap) break;
    const char* from = e["from"] | "";
    const char* msg = e["msg"] | "";
    const double at = e["at"] | 0.0;   // ms since 1970: too big for 32 bits
    const double left = e["left"] | 0.0;
    if (!from[0] || !msg[0] || at <= 0 || left <= 0) continue;
    out[n] = Signal();
    asciiFold(from, out[n].from, sizeof out[n].from);
    asciiFold(msg, out[n].msg, sizeof out[n].msg);
    out[n].at = (uint32_t)(at / 1000);
    out[n].left = left > kSignalLifeSecs ? kSignalLifeSecs : (uint32_t)left;
    ++n;
  }
  return n;
}

int signalQuoteCount(const BookClubData* data) {
  if (data && data->signals.quoteCount > 0) return data->signals.quoteCount;
  return kDefaultCount;
}

const char* signalQuote(const BookClubData* data, int i) {
  if (data && data->signals.quoteCount > 0) {
    return (i >= 0 && i < data->signals.quoteCount) ? data->signals.quotes[i] : "";
  }
  return (i >= 0 && i < kDefaultCount) ? kDefaults[i] : "";
}

void signalLeftText(uint32_t leftSecs, char* buf, size_t cap) {
  if (leftSecs == 0) snprintf(buf, cap, "GONE");
  else if (leftSecs >= 3600) snprintf(buf, cap, "%luH LEFT", (unsigned long)(leftSecs / 3600));
  else snprintf(buf, cap, "%luM LEFT", (unsigned long)((leftSecs + 59) / 60));
}

}  // namespace bc
