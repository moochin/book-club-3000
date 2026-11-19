#include "bc_sync_help.h"

#include <string.h>

namespace bc {

static bool starts(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

const char* syncAdvice(const char* error, bool stale) {
  const char* e = error ? error : "";
  if (starts(e, "rejected file: not valid JSON") || starts(e, "rejected file: not a JSON")) {
    return "The club file has a typo, usually a missing or extra comma or bracket. Fix the gist; "
           "the club editor checks it for you.";
  }
  if (starts(e, "rejected file: no meeting date") || starts(e, "rejected file: missing or bad last.date")) {
    return "The club file needs a meeting in \"history\" with a date like 2026-08-15. Fix the gist.";
  }
  if (starts(e, "rejected file: empty")) return "The club file came back empty. Check the gist still has bookclub.json.";
  if (starts(e, "rejected file") || strstr(e, "too large")) {
    return "The club file was rejected. Check it with the club editor (it must be under 12 KB).";
  }
  if (starts(e, "update ")) {
    return "A firmware update couldn't install. Check the release has firmware.bin attached and "
           "the gist's firmware block matches it.";
  }
  if (strstr(e, "HTTP 404")) {
    return "Nothing at the data URL. Set the gist's Raw link on the setup page (SETTINGS > WI-FI "
           "RESET > OPEN).";
  }
  if (starts(e, "no data URL") || starts(e, "bad URL")) {
    return "Set the gist's Raw link on the setup page (SETTINGS > WI-FI RESET > OPEN).";
  }
  if (starts(e, "no Wi-Fi")) return "Not connected to Wi-Fi. It retries every minute.";
  if (strstr(e, "low memory")) {
    return "Memory was too low for a secure connection, so it waited. It tries again soon, and "
           "restarts itself quietly if memory stays low.";
  }
  if (starts(e, "out of memory") || strstr(e, "lloc")) {
    return "The unit ran short of memory for a secure connection. It restarts itself quietly "
           "if this goes on for 10 minutes while nobody is using it. If it keeps happening, "
           "note ABOUT's MEMORY line for whoever looks after the firmware.";
  }
  if (strstr(e, "X509") || strstr(e, "ertificate")) {
    return "Couldn't check GitHub's certificate. If the clock is wrong, wait for it to set; it "
           "tries again in a few minutes.";
  }
  if (strstr(e, "SSL") || strstr(e, "TLS")) {
    return "The secure connection to GitHub failed. It tries again in a few minutes.";
  }
  if (strstr(e, "can't look up")) {
    return "Couldn't look up GitHub's address (DNS). Check the router's internet; it tries again "
           "in a few minutes.";
  }
  if (strstr(e, "can't connect")) {
    return "Couldn't connect to GitHub. If the nudge hub is OK, memory may be short: check "
           "ABOUT's MEMORY line. It tries again in a few minutes.";
  }
  if (starts(e, "HTTP ") || starts(e, "fetch failed") || starts(e, "download cut short")) {
    return "Couldn't fetch the club file from GitHub. It tries again in a few minutes.";
  }
  if (e[0]) return "The last fetch failed. It tries again in a few minutes.";
  if (stale) return "No good sync for over 7 days. Check the Wi-Fi, then REFRESH NOW.";
  return "All good: the club file is up to date.";
}

}  // namespace bc
