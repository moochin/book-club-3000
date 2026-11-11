#include "bc_state.h"

#include <stdio.h>

namespace bc {

const Thresholds& defaultThresholds() {
  static const Thresholds th;
  return th;
}

static Level levelForDays(int32_t days, const Thresholds& th) {
  int lvl = 0;
  for (int i = 1; i < kLevelCount; ++i) {
    if (days >= th.levelStartDay[i]) lvl = i;
  }
  return (Level)lvl;
}

DisplayState computeState(const BookClubData& data, const LocalNow& now, const Thresholds& th) {
  DisplayState s;
  if (!now.valid || !now.date.valid() || !data.last.date.valid()) {
    s.clockValid = now.valid && now.date.valid();
    s.pose = Pose::Syncing;
    return s;
  }
  s.clockValid = true;

  // Exposure. Only an edit to `last` resets it; a future `last` clamps to 0.
  int32_t days = daysBetween(data.last.date, now.date);
  if (days < 0) days = 0;
  s.exposureDays = days;
  s.level = levelForDays(days, th);
  const int32_t full = th.barFullDays > 0 ? th.barFullDays : 1;
  s.barPermille = (int16_t)(days >= full ? 1000 : (days * 1000) / full);

  // Quest. A next date before last is treated as no date (the parser already
  // drops it, but the engine must not rely on that).
  if (!data.hasNext || !data.next.date.valid() ||
      daysBetween(data.last.date, data.next.date) < 0) {
    s.quest = Quest::NoDate;
  } else {
    const int32_t until = daysBetween(now.date, data.next.date);
    s.daysToNext = until;
    s.hasMeetingTime = data.next.hasTime;
    s.meetingMinute = data.next.minute;
    if (until == 0) {
      s.quest = Quest::MeetingDay;
    } else if (until > 0 && until <= th.finalWeekDays) {
      s.quest = Quest::FinalWeek;
    } else if (until > 0) {
      s.quest = Quest::Scheduled;
    } else {
      s.quest = Quest::Overdue;
    }
    if (until > 0 && until < th.showHoursUnderDays) {
      const int32_t target = data.next.hasTime ? data.next.minute : 0;
      int32_t mins = until * 1440 + target - now.minuteOfDay;
      if (mins < 0) mins = 0;
      s.showHours = true;
      s.minutesToNext = mins;
    }
  }

  s.antiRad = s.quest == Quest::FinalWeek || s.quest == Quest::MeetingDay;

  switch (s.quest) {
    case Quest::MeetingDay: s.pose = Pose::MeetingDay; break;
    case Quest::FinalWeek: s.pose = Pose::FinalWeek; break;
    case Quest::NoDate:
      // While healthy the mascot waits for orders; once sick, the sickness shows.
      s.pose = s.level <= Level::Healthy ? Pose::NoDate : (Pose)((int)Pose::Level0 + (int)s.level);
      break;
    default: s.pose = (Pose)((int)Pose::Level0 + (int)s.level); break;
  }

  s.geigerClicksPerMin = s.antiRad ? 0 : th.geigerClicksPerMin[(int)s.level];

  // Side quest: the first one today or later. Deliberately ignored by the
  // exposure maths above: only a proper meeting (`last`) resets the counter.
  for (int i = 0; i < data.sideQuestCount && i < kMaxSideQuests; ++i) {
    const int32_t until = daysBetween(now.date, data.sideQuests[i].date);
    if (until < 0) continue;
    s.side = until == 0 ? SideQuest::Today : SideQuest::Upcoming;
    s.sideIndex = (int8_t)i;
    s.daysToSide = until;
    break;
  }
  return s;
}

const char* levelName(Level l) {
  switch (l) {
    case Level::WellRested: return "WELL RESTED";
    case Level::Healthy: return "HEALTHY";
    case Level::MinorSickness: return "MINOR SICKNESS";
    case Level::AdvancedSickness: return "ADVANCED SICKNESS";
    case Level::Critical: return "CRITICAL";
  }
  return "?";
}

const char* questName(Quest q) {
  switch (q) {
    case Quest::MeetingDay: return "MEETING DAY";
    case Quest::FinalWeek: return "FINAL WEEK";
    case Quest::Scheduled: return "SCHEDULED";
    case Quest::Overdue: return "OVERDUE";
    case Quest::NoDate: return "NO DATE";
  }
  return "?";
}

const char* poseName(Pose p) {
  switch (p) {
    case Pose::Level0: return "LVL0 GRIN";
    case Pose::Level1: return "LVL1 IDLE";
    case Pose::Level2: return "LVL2 QUEASY";
    case Pose::Level3: return "LVL3 SLUMP";
    case Pose::Level4: return "LVL4 GHOUL";
    case Pose::FinalWeek: return "EXCITED";
    case Pose::MeetingDay: return "PARTY";
    case Pose::NoDate: return "WAITING";
    case Pose::Syncing: return "SYNCING";
  }
  return "?";
}

static const char* plural(int32_t n, const char* one, const char* many) {
  return n == 1 ? one : many;
}

void countdownText(const DisplayState& s, char* buf, int len) {
  char t[6];
  switch (s.quest) {
    case Quest::MeetingDay:
      if (s.hasMeetingTime) {
        formatTime(s.meetingMinute, t, sizeof t);
        snprintf(buf, (size_t)len, "TODAY %s", t);
      } else {
        snprintf(buf, (size_t)len, "TODAY");
      }
      return;
    case Quest::FinalWeek:
    case Quest::Scheduled:
      if (s.showHours) {
        const int32_t h = s.minutesToNext / 60;
        const int32_t d = h / 24;
        if (d > 0) {
          snprintf(buf, (size_t)len, "%ld %s %ld %s", (long)d, plural(d, "DAY", "DAYS"),
                   (long)(h % 24), plural(h % 24, "HR", "HRS"));
        } else {
          snprintf(buf, (size_t)len, "%ld %s %ld MIN", (long)h, plural(h, "HR", "HRS"),
                   (long)(s.minutesToNext % 60));
        }
      } else {
        snprintf(buf, (size_t)len, "%ld %s", (long)s.daysToNext,
                 plural(s.daysToNext, "DAY", "DAYS"));
      }
      return;
    case Quest::Overdue:
      snprintf(buf, (size_t)len, "%ld %s AGO", (long)-s.daysToNext,
               plural(-s.daysToNext, "DAY", "DAYS"));
      return;
    case Quest::NoDate:
      snprintf(buf, (size_t)len, "--");
      return;
  }
  buf[0] = '\0';
}

void questLine(const DisplayState& s, char* buf, int len, uint32_t variant) {
  char c[24];
  switch (s.quest) {
    case Quest::MeetingDay:
      snprintf(buf, (size_t)len, "PERK UNLOCKED: BOOK CLUB TODAY");
      return;
    case Quest::FinalWeek:
      countdownText(s, c, sizeof c);
      snprintf(buf, (size_t)len, "FINAL WEEK: %s TO GO", c);
      return;
    case Quest::Scheduled:
      countdownText(s, c, sizeof c);
      snprintf(buf, (size_t)len, "QUEST: BOOK CLUB IN %s", c);
      return;
    case Quest::Overdue:
      snprintf(buf, (size_t)len, "DEBRIEF PENDING: UPDATE THE LOG");
      return;
    case Quest::NoDate:
      snprintf(buf, (size_t)len, (variant & 1) ? "AWAITING ORDERS FROM THE OVERSEER"
                                               : "OBJECTIVE: SCHEDULE BOOK CLUB");
      return;
  }
  buf[0] = '\0';
}

void exposureLabel(const DisplayState& s, char* buf, int len) {
  if (s.antiRad) {
    snprintf(buf, (size_t)len, "ANTI-RAD DOSE ADMINISTERED");
  } else if (s.level == Level::Critical) {
    snprintf(buf, (size_t)len, "SEEK BOOK CLUB IMMEDIATELY");
  } else {
    snprintf(buf, (size_t)len, "LVL %d  %s", (int)s.level, levelName(s.level));
  }
}

void sideQuestLine(const DisplayState& s, const BookClubData& data, char* buf, int len) {
  buf[0] = '\0';
  if (s.side == SideQuest::None || s.sideIndex < 0 || s.sideIndex >= data.sideQuestCount) return;
  const Meeting& m = data.sideQuests[s.sideIndex];
  char t[8] = "";
  if (m.hasTime) {
    t[0] = ' ';
    formatTime(m.minute, t + 1, sizeof t - 1);
  }
  if (s.side == SideQuest::Today) snprintf(buf, (size_t)len, "SIDE QUEST TODAY%s", t);
  else if (s.daysToSide == 1) snprintf(buf, (size_t)len, "SIDE QUEST TOMORROW%s", t);
  else snprintf(buf, (size_t)len, "SIDE QUEST IN %ld DAYS", (long)s.daysToSide);
}

LogStats computeLogStats(const BookClubData& data) {
  LogStats st;
  st.totalMeetings = data.historyCount;
  for (int i = 0; i + 1 < data.historyCount; ++i) {
    const int32_t gap = daysBetween(data.history[i + 1].date, data.history[i].date);
    if (gap > st.longestGapDays) {
      st.longestGapDays = gap;
      st.gapFrom = data.history[i + 1].date;
      st.gapTo = data.history[i].date;
    }
  }
  return st;
}

static const char* readPart(const char* p, long& out) {
  out = 0;
  while (*p >= '0' && *p <= '9') out = out * 10 + (*p++ - '0');
  while (*p && *p != '.') ++p;  // ignore suffixes like "-beta"
  if (*p == '.') ++p;
  return p;
}

int compareVersions(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  if (*a == 'v' || *a == 'V') ++a;
  if (*b == 'v' || *b == 'V') ++b;
  for (int i = 0; i < 4; ++i) {
    long x, y;
    a = readPart(a, x);
    b = readPart(b, y);
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

}  // namespace bc
