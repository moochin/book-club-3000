#include "ui.h"

#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "bc_text.h"
#include "clock.h"
#include "config.h"
#include "display.h"
#include "logo_data.h"
#include "mascot.h"
#include "scenarios.h"
#include "settings.h"

using namespace display;

namespace ui {

namespace {

enum class Screen : uint8_t { Tabs, Menu, OwnerCard, Help, About, Debug, ConfirmPortal };
enum Tab : uint8_t { kStat = 0, kData = 1, kLog = 2, kTabCount = 3 };

enum MenuItem : uint8_t {
  kMiRefresh,
  kMiSound,
  kMiBrightness,
  kMiTheme,
  kMiEffects,
  kMiOwner,
  kMiHelp,
  kMiAbout,
  kMiWifi,
  kMiBack,
  kMenuCount
};

Screen g_screen = Screen::Tabs;
uint8_t g_tab = kStat;
bool g_logScroll = false;
int g_logTop = 0;
int g_menuSel = 0;
int g_debugSel = 0;
bool g_confirmYes = false;
int g_helpPage = 0;
Screen g_cardReturn = Screen::Menu;  // where the owner card goes back to
uint8_t g_aboutPresses = 0;
uint32_t g_aboutFirstPressMs = 0;

constexpr int kLogRows = 4;
constexpr int kLogRowH = 36;
constexpr int kContentTop = 30;

// ---- Small drawing helpers ------------------------------------------------

void folded(int x, int y, const char* s, uint8_t ink, int maxChars, bool upper = false) {
  char buf[64];
  bc::asciiFold(s, buf, sizeof buf);
  if (upper) bc::upperAscii(buf);
  if (maxChars > 0 && maxChars < (int)sizeof buf && (int)strlen(buf) > maxChars) {
    buf[maxChars - 1] = '~';
    buf[maxChars] = 0;
  }
  text(x, y, buf, ink);
}

void foldedWrapped(int x, int y, const char* s, int maxChars, int maxLines, uint8_t ink) {
  char buf[bc::kMessageLen];
  bc::asciiFold(s, buf, sizeof buf);
  textWrapped(x, y, buf, maxChars, maxLines, ink);
}

void frame(int x, int y, int w, int h, uint8_t ink = kMid) {
  auto& c = canvas();
  c.drawRect(x, y, w, h, ink);
  // Little corner ticks for the terminal look.
  c.drawFastHLine(x - 2, y, 2, ink);
  c.drawFastHLine(x + w, y + h - 1, 2, ink);
}

void wifiIcon(int x, int y, bool up) {
  auto& c = canvas();
  for (int i = 0; i < 3; ++i) {
    const int h = 4 + i * 4;
    c.fillRect(x + i * 5, y + 12 - h, 3, h, up ? kText : kFaint);
  }
  if (!up) {
    c.drawLine(x - 1, y, x + 14, y + 12, kWarn);
  }
}

void warnIcon(int x, int y) {
  auto& c = canvas();
  c.fillTriangle(x + 7, y, x, y + 13, x + 14, y + 13, kWarn);
  c.fillRect(x + 6, y + 4, 3, 5, kBg);
  c.fillRect(x + 6, y + 10, 3, 2, kBg);
}

void header(const Model& m) {
  auto& c = canvas();
  static const char* const kNames[kTabCount] = {"STAT", "DATA", "LOG"};
  for (int i = 0; i < kTabCount; ++i) {
    const int x = 8 + i * 70;
    const bool cur = i == g_tab && g_screen == Screen::Tabs;
    if (cur) {
      c.drawRect(x, 2, 62, 22, kBright);
      textCentered(x + 31, 5, kNames[i], kHot, 2, 2);
    } else {
      textCentered(x + 31, 5, kNames[i], kDim, 2, 2);
    }
  }
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);

  // Right side: sync warning, Wi-Fi, clock.
  char clock[8] = "--:--";
  if (m.now.valid) bc::formatTime(m.now.minuteOfDay, clock, sizeof clock);
  textRight(cfg::kScreenW - 4, 6, clock, kText);
  wifiIcon(cfg::kScreenW - 54, 6, m.wifiUp);
  if (m.syncStale || m.sync == SyncInfo::Failed) warnIcon(cfg::kScreenW - 76, 5);
  if (m.sync == SyncInfo::Fetching) text(cfg::kScreenW - 72, 6, "~", kBright);
}

void previewBanner(const Model& m) {
  if (!m.previewName) return;
  auto& c = canvas();
  c.fillRect(0, cfg::kScreenH - 14, cfg::kScreenW, 14, kWarnDim);
  char buf[56];
  snprintf(buf, sizeof buf, "PREVIEW: %s", m.previewName);
  text(4, cfg::kScreenH - 11, buf, kHot, 1, 1);
}

void bar(int x, int y, int w, int h, int permille, uint8_t ink) {
  auto& c = canvas();
  c.drawRect(x, y, w, h, kMid);
  const int segs = 20;
  const int segW = (w - 4) / segs;
  const int filled = (permille * segs + 999) / 1000;
  for (int i = 0; i < segs; ++i) {
    c.fillRect(x + 2 + i * segW, y + 2, segW - 2, h - 4, i < filled ? ink : kFaint);
  }
}

// ---- Full-screen states ---------------------------------------------------

void drawCentredScreen(const char* title, const char* l1, const char* l2, const char* l3, uint32_t t,
                       bc::Pose pose = bc::Pose::Syncing) {
  mascot::draw(16, 60, pose, t, false);
  text(96, 64, title, kBright, 2, 2);
  if (l1) textWrapped(96, 96, l1, 36, 2, kText);
  if (l2) textWrapped(96, 136, l2, 36, 2, kDim);
  if (l3) textWrapped(96, 176, l3, 36, 3, kDim);
}

void drawPortal(const Model& m) {
  auto& c = canvas();
  text(12, 8, "WI-FI SETUP", kBright, 2, 2);
  c.drawFastHLine(0, 30, cfg::kScreenW, kMid);
  char buf[64];
  text(12, 42, "1. ON YOUR PHONE, JOIN THE WI-FI:", kText);
  frame(24, 62, 272, 28, kBright);
  textCentered(160, 68, m.apName, kHot, 1, 2);
  text(12, 100, "2. THE SETUP PAGE OPENS (OR BROWSE", kText);
  text(12, 118, "   TO 192.168.4.1) - TAP CONFIGURE", kText);
  text(12, 140, "3. PICK YOUR HOME NETWORK, FILL IN", kText);
  text(12, 158, "   YOUR NAME AND SETTINGS, SAVE", kText);
  if (m.portalCancellable) {
    text(12, 192, "THE UNIT KEEPS ITS OLD WI-FI UNTIL", kDim);
    text(12, 210, "YOU SAVE.  HOLD KNOB 2 S TO CANCEL", kDim);
  } else {
    snprintf(buf, sizeof buf, "UNIT ID %s   FW %s", settings::chipId(), BC_FW_VERSION);
    text(12, 210, buf, kDim);
  }
}

// ---- STAT -----------------------------------------------------------------

void drawCelebration(const Model& m, uint32_t t) {
  const auto& s = m.state;
  mascot::draw(12, 36, bc::Pose::MeetingDay, t, false);
  const bool flash = (t / 600) % 2;
  textCentered(200, 34, "** PERK UNLOCKED **", flash ? kHot : kBright, 1, 2);
  textCentered(200, 60, "BOOK CLUB", kHot, 3, 4);
  char buf[32];
  bc::countdownText(s, buf, sizeof buf);
  textCentered(200, 100, buf, kBright, 2, 2);
  if (m.data) {
    canvas().drawFastHLine(92, 128, 216, kFaint);
    text(92, 136, "PLAN", kDim);
    folded(128, 136, m.data->next.plan[0] ? m.data->next.plan : "TBC", kText, 30);
    text(92, 156, "AT", kDim);
    folded(128, 156, m.data->next.location, kText, 30);
  }
  frame(8, 184, 304, 48, kBright);
  text(16, 192, "+1 TO ALL STATS FOR THE EVENING", kText);
  if (m.caps.valid) {
    char cost[48];
    snprintf(cost, sizeof cost, "TONIGHT COSTS %d FUN CAPS - WORTH IT", m.caps.costPerMeeting);
    text(16, 210, cost, kWarn);
  } else {
    text(16, 210, "EXPOSURE PAUSED - ENJOY THE NIGHT", kDim);
  }
}

void drawStat(const Model& m, uint32_t t) {
  auto& c = canvas();
  const auto& s = m.state;
  if (s.quest == bc::Quest::MeetingDay) {
    drawCelebration(m, t);
    return;
  }
  mascot::draw(12, 34, s.pose, t);
  if (s.quest == bc::Quest::Scheduled) {
    // Hopeful badge on the mascot.
    c.fillRoundRect(52, 32, 30, 14, 3, kBright);
    text(55, 35, "HOPE", kBg, 1, 1);
  }

  text(92, 32, "EXPOSURE", kDim);
  char buf[48];
  snprintf(buf, sizeof buf, "%ld", (long)s.exposureDays);
  const bool sick = s.level >= bc::Level::MinorSickness && !s.antiRad;
  text(92, 52, buf, sick ? kWarn : kHot, 4, 5);
  text(92 + textWidth(buf, 4) + 8, 72, s.exposureDays == 1 ? "DAY" : "DAYS", kText, 2, 2);
  text(240, 52, "SINCE", kDim, 1, 1);
  text(240, 62, "LAST", kDim, 1, 1);
  text(240, 72, "MEETING", kDim, 1, 1);

  bar(92, 100, 220, 14, s.barPermille, sick ? kWarn : kBright);
  bc::exposureLabel(s, buf, sizeof buf);
  uint8_t ink = kText;
  if (s.level == bc::Level::Critical && !s.antiRad) ink = (t / 500) % 2 ? kWarn : kWarnDim;
  else if (sick) ink = kWarn;
  text(92, 120, buf, ink);

  // Side quest teaser. On the day it alternates with the family-friendly
  // reassurance; it never changes the exposure above.
  if (m.data && s.side != bc::SideQuest::None) {
    bc::sideQuestLine(s, *m.data, buf, sizeof buf);
    const bool today = s.side == bc::SideQuest::Today;
    if (today && (t / 3000) % 2) snprintf(buf, sizeof buf, "NO FUN CAPS SPENT");
    text(92, 139, buf, today ? kHot : kDim, 1, 1);
  }

  // Quest box.
  frame(8, 150, 304, 82, s.quest == bc::Quest::Overdue ? kWarn : kMid);
  text(16, 154, "QUEST", kDim, 1, 1);
  bc::questLine(s, buf, sizeof buf, t / 8000);
  text(16, 166, buf, s.quest == bc::Quest::Overdue ? kWarn : kText);
  switch (s.quest) {
    case bc::Quest::Scheduled:
    case bc::Quest::FinalWeek: {
      char cd[24];
      bc::countdownText(s, cd, sizeof cd);
      textCentered(160, 190, cd, kHot, 3, 2);
      if (m.data) {
        char d[16];
        bc::formatDateShort(m.data->next.date, d, sizeof d);
        snprintf(buf, sizeof buf, "%s %s", bc::weekdayName(m.data->next.date), d);
        textCentered(160, 214, buf, kDim);
      }
      break;
    }
    case bc::Quest::Overdue: {
      char cd[24];
      bc::countdownText(s, cd, sizeof cd);
      snprintf(buf, sizeof buf, "MEETING WAS %s", cd);
      text(16, 188, buf, kText);
      text(16, 208, "EDIT bookclub.json: SET last + next", kDim);
      break;
    }
    case bc::Quest::NoDate:
      text(16, 188, "NO NEXT MEETING ON FILE", kText);
      text(16, 208, "ADD \"next\" TO bookclub.json", kDim);
      break;
    default: break;
  }
}

// ---- DATA -----------------------------------------------------------------

void drawData(const Model& m) {
  auto& c = canvas();
  const bc::BookClubData& d = *m.data;
  const auto& st = m.state;
  const bool sq = st.side != bc::SideQuest::None && st.sideIndex >= 0;
  char buf[64], date[16];
  // Row positions: the side-quest block squeezes the rest when present.
  const int yPlan = sq ? 72 : 78, yAt = sq ? 88 : 96, yMsg = sq ? 106 : 118;

  text(8, 32, "NEXT MEETING", kBright);
  if (d.hasNext && st.quest != bc::Quest::NoDate) {
    bc::formatDateShort(d.next.date, date, sizeof date);
    snprintf(buf, sizeof buf, "%s %s", bc::weekdayName(d.next.date), date);
    text(8, 50, buf, kHot, 2, 2);
    if (d.next.hasTime) {
      char tm[6];
      bc::formatTime(d.next.minute, tm, sizeof tm);
      text(8 + textWidth(buf, 2) + 12, 50, tm, kHot, 2, 2);
    }
    text(8, yPlan, "PLAN ", kDim);
    folded(50, yPlan, d.next.plan[0] ? d.next.plan : "TBC", kText, 44);
    text(8, yAt, "AT   ", kDim);
    folded(50, yAt, d.next.location[0] ? d.next.location : "TBC", kText, 44);
  } else {
    text(8, 50, "NOT SET", kWarn, 2, 2);
    text(8, yPlan, "Edit bookclub.json to schedule one.", kDim);
  }
  if (d.message[0]) {
    text(8, yMsg, "MSG", kDim);
    foldedWrapped(50, yMsg, d.message, 44, sq ? 1 : 2, kText);
  }

  if (sq) {
    const bc::Meeting& q = d.sideQuests[st.sideIndex];
    c.drawFastHLine(0, 124, cfg::kScreenW, kFaint);
    const bool today = st.side == bc::SideQuest::Today;
    text(8, 128, "SIDE QUEST", today ? kHot : kBright);
    bc::formatDateShort(q.date, date, sizeof date);
    if (today) snprintf(buf, sizeof buf, "TODAY");
    else snprintf(buf, sizeof buf, "%s %s", bc::weekdayName(q.date), date);
    if (q.hasTime) {
      char tm[8];
      bc::formatTime(q.minute, tm, sizeof tm);
      strlcat(buf, " ", sizeof buf);
      strlcat(buf, tm, sizeof buf);
    }
    textRight(cfg::kScreenW - 8, 128, buf, today ? kHot : kText);
    char what[96];
    snprintf(what, sizeof what, "%s%s%s", q.plan[0] ? q.plan : "Get-together", q.location[0] ? " @ " : "",
             q.location);
    folded(8, 144, what, kText, 51);
  }

  const int yLast = 166;
  c.drawFastHLine(0, yLast - 6, cfg::kScreenW, kFaint);
  text(8, yLast, "LAST ", kDim);
  bc::formatDateShort(d.last.date, date, sizeof date);
  if (sq) {
    text(50, yLast, date, kText);
    folded(122, yLast, d.last.plan[0] ? d.last.plan : "-", kText, 32);
  } else {
    text(50, yLast, date, kText);
    folded(50, yLast + 18, d.last.plan[0] ? d.last.plan : "-", kText, 44);
  }
  if (sq) text(8, 186, "Side quests don't reset exposure.", kDim);

  c.drawFastHLine(0, 206, cfg::kScreenW, kFaint);
  char age[20];
  clockmod::formatAge(m.lastSyncEpoch, age, sizeof age);
  snprintf(buf, sizeof buf, "SYNCED %s", age);
  text(8, 214, buf, m.syncStale ? kWarn : kDim);
  if (m.syncStale) {
    warnIcon(cfg::kScreenW - 20, 214);
    text(cfg::kScreenW - 104, 214, "NO SYNC 7D+", kWarn);
  } else if (!m.wifiUp && m.offlineLong) {
    textRight(cfg::kScreenW - 8, 214, "OFFLINE", kWarn);
  }
}

// ---- LOG ------------------------------------------------------------------

struct LogEntry {
  const bc::Meeting* m;
  int historyIndex;  // -1 for a side quest
};

// History (newest first) merged with side quests that have happened.
int buildLog(const Model& m, LogEntry* out, int cap) {
  const bc::BookClubData& d = *m.data;
  int h = 0, n = 0;
  int q = d.sideQuestCount - 1;  // side quests are oldest first
  if (m.now.valid) {
    while (q >= 0 && bc::daysBetween(m.now.date, d.sideQuests[q].date) > 0) --q;  // future ones
  } else {
    q = -1;
  }
  while (n < cap && (h < d.historyCount || q >= 0)) {
    const bool takeSide = q >= 0 && (h >= d.historyCount ||
                                     bc::daysBetween(d.history[h].date, d.sideQuests[q].date) > 0);
    if (takeSide) out[n++] = {&d.sideQuests[q--], -1};
    else { out[n] = {&d.history[h], h}; ++h; ++n; }
  }
  return n;
}

void drawLog(const Model& m) {
  auto& c = canvas();
  const bc::BookClubData& d = *m.data;
  static LogEntry log[bc::kMaxHistory + bc::kMaxSideQuests];
  const int count = buildLog(m, log, bc::kMaxHistory + bc::kMaxSideQuests);
  text(8, 32, "MEETING LOG", kBright);
  textRight(cfg::kScreenW - 8, 32, g_logScroll ? "[TURN: SCROLL]" : "[TURN: TABS]",
            g_logScroll ? kHot : kDim);
  const int maxTop = count > kLogRows ? count - kLogRows : 0;
  if (g_logTop > maxTop) g_logTop = maxTop;
  for (int r = 0; r < kLogRows; ++r) {
    const int i = g_logTop + r;
    if (i >= count) break;
    const bc::Meeting& e = *log[i].m;
    const bool side = log[i].historyIndex < 0;
    const int y = 52 + r * kLogRowH;
    char date[16];
    bc::formatDateShort(e.date, date, sizeof date);
    text(8, y, date, log[i].historyIndex == 0 ? kHot : (side ? kDim : kText));
    folded(86, y, e.plan[0] ? e.plan : (side ? "Get-together" : "-"), side ? kDim : kText, 36);
    folded(86, y + 16, e.location, kDim, 36);
    const int hi = log[i].historyIndex;
    if (side) {
      text(8, y + 16, "SIDE QUEST", kWarnDim, 1, 2);
    } else if (hi + 1 < d.historyCount) {
      char gap[24];
      snprintf(gap, sizeof gap, "+%ldD", (long)bc::daysBetween(d.history[hi + 1].date, e.date));
      text(8, y + 16, gap, kDim);
    }
  }
  // Scrollbar.
  if (count > kLogRows) {
    const int top = 52, h = kLogRows * kLogRowH - 4;
    c.drawFastVLine(cfg::kScreenW - 3, top, h, kFaint);
    const int th = max(8, h * kLogRows / count);
    const int ty = top + (h - th) * g_logTop / maxTop;
    c.fillRect(cfg::kScreenW - 4, ty, 3, th, kBright);
  }
  c.drawFastHLine(0, 198, cfg::kScreenW, kFaint);
  const bc::LogStats st = bc::computeLogStats(d);
  char buf[56];
  snprintf(buf, sizeof buf, "MEETINGS %d   LONGEST GAP %ld DAYS", st.totalMeetings,
           (long)st.longestGapDays);
  text(8, 204, buf, kText);
  if (st.longestGapDays > 0) {
    char a[16], b[16];
    bc::formatDateShort(st.gapFrom, a, sizeof a);
    bc::formatDateShort(st.gapTo, b, sizeof b);
    snprintf(buf, sizeof buf, "(%s - %s)", a, b);
    text(8, 222, buf, kDim);
  }
}

// ---- Menu screens -----------------------------------------------------------

void menuLabel(int i, const Model& m, char* buf, int len) {
  switch (i) {
    case kMiRefresh: snprintf(buf, len, "REFRESH NOW"); break;
    case kMiSound: snprintf(buf, len, "SOUND: %s", m.sound ? "ON" : "OFF"); break;
    case kMiBrightness:
      if (m.autoBrightness) snprintf(buf, len, "BRIGHTNESS: AUTO");
      else snprintf(buf, len, "BRIGHTNESS: FIXED %d%%", m.fixedBrightness * 100 / 255);
      break;
    case kMiTheme: snprintf(buf, len, "THEME: %s", m.theme == 1 ? "AMBER" : "GREEN"); break;
    case kMiEffects: snprintf(buf, len, "SCANLINES: %s", m.effects ? "ON" : "OFF"); break;
    case kMiOwner: snprintf(buf, len, "OWNER CARD"); break;
    case kMiHelp: snprintf(buf, len, "HOW TO USE"); break;
    case kMiAbout: snprintf(buf, len, "ABOUT"); break;
    case kMiWifi: snprintf(buf, len, "WI-FI RESET"); break;
    case kMiBack: snprintf(buf, len, "< BACK"); break;
    default: buf[0] = 0;
  }
}

void drawList(const char* title, int count, int sel, void (*label)(int, const Model&, char*, int),
              const Model& m, int rowH = 20) {
  auto& c = canvas();
  text(8, 4, title, kBright, 2, 2);
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);
  const int visible = (cfg::kScreenH - 34) / rowH;
  int top = sel - visible + 1;
  if (top < 0) top = 0;
  for (int r = 0; r < visible && top + r < count; ++r) {
    const int i = top + r;
    const int y = 32 + r * rowH;
    char buf[48];
    label(i, m, buf, sizeof buf);
    if (i == sel) {
      c.fillRect(4, y - 1, cfg::kScreenW - 8, rowH - 2, kFaint);
      c.drawRect(4, y - 1, cfg::kScreenW - 8, rowH - 2, kBright);
      text(12, y + 1, ">", kHot);
      text(24, y + 1, buf, kHot);
    } else {
      text(24, y + 1, buf, kText);
    }
  }
}

void debugLabel(int i, const Model&, char* buf, int len) {
  if (i == 0) snprintf(buf, len, "LIVE DATA (END PREVIEW)");
  else snprintf(buf, len, "%s", scenarios::get(i - 1).name);
}

void drawOwnerCard(const Model& m) {
  auto& c = canvas();
  text(8, 4, "OWNER CARD", kBright, 2, 2);
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);
  char name[24];
  bc::asciiFold(m.owner[0] ? m.owner : settings::chipId(), name, sizeof name);
  bc::upperAscii(name);
  text(8, 34, name, kHot, 3, 3);
  text(8, 60, "BOOK CLUB OPERATIVE, GRADE III", kDim);
  for (int i = 0; i < settings::kStatCount; ++i) {
    const int y = 80 + i * 18;
    text(8, y, settings::statName(i), kText);
    const int v = m.stats ? m.stats[i] : 5;
    for (int b = 0; b < 10; ++b) {
      c.fillRect(140 + b * 14, y + 3, 11, 10, b < v ? kBright : kFaint);
    }
    char n[4];
    snprintf(n, sizeof n, "%d", v);
    textRight(cfg::kScreenW - 6, y, n, kHot);
  }

  // Fun Caps: round tokens in the warning colour (a plain generic coin).
  c.drawFastHLine(0, 207, cfg::kScreenW, kFaint);
  if (!m.caps.valid) {
    text(8, 212, "FUN CAPS", kText);
    text(140, 212, "COUNTING...", kDim);
    return;
  }
  const int bal = m.caps.balance;
  text(8, 212, "FUN CAPS", kWarn);
  for (int b = 0; b < m.caps.max && b < 10; ++b) {
    const int cx = 145 + b * 14, cy = 220;
    if (b < bal) {
      c.fillCircle(cx, cy, 5, kWarn);
      c.drawCircle(cx, cy, 3, kWarnDim);
    } else {
      c.drawCircle(cx, cy, 5, kFaint);
    }
  }
  char n[8];
  snprintf(n, sizeof n, "%d", bal);
  textRight(cfg::kScreenW - 6, 212, n, bal < 0 ? kWarn : kHot);
  // The small print: what was banked, and any bonus or penalty with its reason.
  char small[96], note[bc::kTextLen];
  if (bal < 0) {
    snprintf(small, sizeof small, "OVERDRAWN - IN THE DOGHOUSE");
  } else {
    snprintf(small, sizeof small, "+%d BANKED SINCE THE LAST CLUB NIGHT", m.caps.earnedSinceLast);
  }
  if (m.caps.adjust) {
    bc::asciiFold(m.caps.note, note, sizeof note);
    bc::upperAscii(note);
    snprintf(small, sizeof small, "%s%+d %s", bal < 0 ? "OVERDRAWN: " : "", m.caps.adjust,
             note[0] ? note : (m.caps.adjust > 0 ? "BONUS" : "PENALTY"));
  }
  small[52] = '\0';
  text(8, 230, small, bal < 0 ? kWarn : kDim, 1, 1);
}

void drawAbout(const Model& m) {
  auto& c = canvas();
  text(8, 4, "ABOUT", kBright, 2, 2);
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);
  char buf[64], age[20];
  int y = 34;
  auto line = [&](const char* k, const char* v, uint8_t ink = kText) {
    text(8, y, k, kDim);
    char f[44];
    bc::asciiFold(v, f, sizeof f);
    text(92, y, f, ink);
    y += 18;
  };
  line("FIRMWARE", BC_FW_VERSION " (" __DATE__ ")");
  line("UNIT", m.owner[0] ? m.owner : settings::chipId());
  line("WI-FI", m.wifiUp ? "CONNECTED" : (m.portal ? "SETUP PORTAL OPEN" : "OFFLINE"),
       m.wifiUp ? kText : kWarn);
  line("IP", m.ip);
  snprintf(buf, sizeof buf, "%s.local", m.hostname);
  line("HOSTNAME", buf);
  clockmod::formatAge(m.lastSyncEpoch, age, sizeof age);
  line("LAST SYNC", age, m.syncStale ? kWarn : kText);
  const char* st = "IDLE";
  switch (m.sync) {
    case SyncInfo::Fetching: st = "FETCHING..."; break;
    case SyncInfo::Ok: st = "OK"; break;
    case SyncInfo::Failed: st = m.syncError[0] ? m.syncError : "FAILED"; break;
    case SyncInfo::Updating: st = "UPDATING FIRMWARE"; break;
    default: break;
  }
  line("SYNC", st, m.sync == SyncInfo::Failed ? kWarn : kText);
  snprintf(buf, sizeof buf, "%u KB FREE, UP %lu MIN", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned long)(millis() / 60000));
  line("MEMORY", buf);
  snprintf(buf, sizeof buf, "%d", m.lightRaw);
  line("LIGHT", buf);
  text(8, 222, "TURN, OR TAP THE TITLE, TO GO BACK", kDim);
}

// ---- How to use -------------------------------------------------------------

struct HelpPage {
  const char* title;
  const char* paras[5];
};

const HelpPage kHelp[] = {
    {"GETTING AROUND",
     {"TABS: tap STAT, DATA or LOG, or turn the knob.",
      "SETTINGS: tap the clock, or hold 2 s anywhere.",
      "BACK: tap a screen's title, or hold 2 s.",
      "LOG: tap the top or bottom half to page.",
      "OWNER CARD: tap the mascot."}},
    {"YOUR UNIT",
     {"Owner name, card stats, theme, rotation and the data URL are set on the setup page.",
      "To open it: SETTINGS > WI-FI RESET > OPEN, then join the BookClub network on your phone "
      "(or browse to 192.168.4.1) and tap Configure WiFi.",
      "Your Wi-Fi is kept until you save a new one.", nullptr}},
    {"THE CLUB FILE",
     {"Dates come from the club's bookclub.json (the data URL), checked once a day. REFRESH NOW "
      "fetches it straight away.",
      "A warning triangle by the clock means the last fetch failed: ABOUT shows why.",
      "Factory reset: hold BOOT while plugging in, for 10 s.", nullptr}},
};
constexpr int kHelpPages = sizeof kHelp / sizeof kHelp[0];

void drawHelp() {
  auto& c = canvas();
  const HelpPage& pg = kHelp[g_helpPage];
  text(8, 4, "HOW TO USE", kBright, 2, 2);
  char n[8];
  snprintf(n, sizeof n, "%d/%d", g_helpPage + 1, kHelpPages);
  textRight(cfg::kScreenW - 8, 6, n, kDim);
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);
  text(8, 32, pg.title, kHot);
  int y = 54;
  for (const char* p : pg.paras) {
    if (!p) break;
    y += textWrapped(8, y, p, 51, 4, kText) * 16 + 6;
  }
  text(8, 222, "TAP LOWER HALF: NEXT  UPPER: BACK", kDim);
}

void drawConfirmPortal() {
  auto& c = canvas();
  text(8, 4, "WI-FI RESET", kBright, 2, 2);
  c.drawFastHLine(0, 26, cfg::kScreenW, kMid);
  text(8, 44, "OPEN THE WI-FI SETUP PORTAL?", kText);
  text(8, 70, "Use it to join a new network or to", kDim);
  text(8, 88, "change this unit's settings. The", kDim);
  text(8, 106, "current Wi-Fi is kept until you save.", kDim);
  const int y = 150;
  c.drawRect(40, y, 100, 32, g_confirmYes ? kHot : kFaint);
  c.drawRect(180, y, 100, 32, !g_confirmYes ? kHot : kFaint);
  textCentered(90, y + 8, "OPEN", g_confirmYes ? kHot : kDim, 2, 2);
  textCentered(230, y + 8, "CANCEL", !g_confirmYes ? kHot : kDim, 2, 2);
  text(8, 206, "TAP A BUTTON, OR TURN AND PRESS", kDim);
}

}  // namespace

Result handleTap(int x, int y, const Model& m, uint32_t now);

void begin() {}

bool animating(const Model& m) {
  if (m.portal || m.updatePct >= 0) return false;
  return g_screen == Screen::Tabs && g_tab == kStat;
}

Result handle(const input::Events& e, const Model& m, uint32_t now) {
  Result r;
  if (!e.any()) return r;
  r.redraw = true;

  if (m.portal) {
    if (e.longPress && m.portalCancellable) r.action = Action::CancelPortal;
    return r;
  }
  if (e.tap) return handleTap(e.x, e.y, m, now);

  switch (g_screen) {
    case Screen::Tabs:
      if (e.longPress) {
        g_screen = Screen::Menu;
        g_menuSel = 0;
        break;
      }
      if (g_tab == kLog && e.shortPress) g_logScroll = !g_logScroll;
      if (e.turn) {
        if (g_tab == kLog && g_logScroll) {
          g_logTop += e.turn;
          if (g_logTop < 0) g_logTop = 0;  // upper clamp in drawLog
        } else {
          g_tab = (uint8_t)((g_tab + kTabCount + (e.turn % kTabCount)) % kTabCount);
          g_logScroll = false;
        }
      }
      break;

    case Screen::Menu:
      if (e.longPress) {
        g_screen = Screen::Tabs;
        break;
      }
      g_menuSel = constrain(g_menuSel + e.turn, 0, kMenuCount - 1);
      if (e.shortPress) {
        switch (g_menuSel) {
          case kMiRefresh: r.action = Action::RefreshNow; g_screen = Screen::Tabs; g_tab = kData; break;
          case kMiSound: r.action = Action::ToggleSound; break;
          case kMiBrightness: r.action = Action::CycleBrightness; break;
          case kMiTheme: r.action = Action::CycleTheme; break;
          case kMiEffects: r.action = Action::ToggleEffects; break;
          case kMiOwner: g_screen = Screen::OwnerCard; g_cardReturn = Screen::Menu; break;
          case kMiHelp: g_screen = Screen::Help; g_helpPage = 0; break;
          case kMiAbout: g_screen = Screen::About; g_aboutPresses = 0; break;
          case kMiWifi: g_screen = Screen::ConfirmPortal; g_confirmYes = false; break;
          case kMiBack: g_screen = Screen::Tabs; break;
        }
      }
      break;

    case Screen::OwnerCard:
      if (e.shortPress || e.longPress) g_screen = g_cardReturn;
      break;

    case Screen::Help:
      // Turn or press to page; past either end, or a hold, goes back.
      if (e.longPress) {
        g_screen = Screen::Menu;
        break;
      }
      g_helpPage += e.turn + (e.shortPress ? 1 : 0);
      if (g_helpPage < 0 || g_helpPage >= kHelpPages) {
        g_helpPage = 0;
        g_screen = Screen::Menu;
      }
      break;

    case Screen::About:
      // Hidden: five quick presses open the preview menu.
      if (e.shortPress) {
        if (g_aboutPresses == 0 || now - g_aboutFirstPressMs > 3000) {
          g_aboutPresses = 0;
          g_aboutFirstPressMs = now;
        }
        if (++g_aboutPresses >= 5) {
          g_screen = Screen::Debug;
          g_debugSel = 0;
        }
      }
      if (e.longPress || e.turn) g_screen = Screen::Menu;
      break;

    case Screen::Debug:
      if (e.longPress) {
        g_screen = Screen::Menu;
        break;
      }
      g_debugSel = constrain(g_debugSel + e.turn, 0, scenarios::count());
      if (e.shortPress) {
        if (g_debugSel == 0) {
          r.action = Action::ExitPreview;
        } else {
          r.action = Action::Preview;
          r.arg = g_debugSel - 1;
        }
        g_screen = Screen::Tabs;
        g_tab = kStat;
      }
      break;

    case Screen::ConfirmPortal:
      if (e.turn) g_confirmYes = !g_confirmYes;
      if (e.longPress) g_screen = Screen::Menu;
      if (e.shortPress) {
        if (g_confirmYes) r.action = Action::OpenPortal;
        g_screen = g_confirmYes ? Screen::Tabs : Screen::Menu;
      }
      break;
  }
  return r;
}

// A touch-screen tap: pick what was tapped, then reuse the knob logic.
// Tapping a screen's title goes back (as a long press does).
Result handleTap(int x, int y, const Model& m, uint32_t now) {
  Result r;
  r.redraw = true;
  input::Events press;
  press.shortPress = true;
  input::Events hold;
  hold.longPress = true;
  const bool title = y < 27;

  switch (g_screen) {
    case Screen::Tabs:
      if (title) {
        for (int i = 0; i < kTabCount; ++i) {
          const int tx = 8 + i * 70;  // as drawn by header()
          if (x >= tx - 4 && x < tx + 66) {
            g_tab = (uint8_t)i;
            g_logScroll = false;
            return r;
          }
        }
        if (x >= 220) return handle(hold, m, now);  // clock corner: settings
        return r;
      }
      // The mascot (left side of STAT and the full-screen states): owner card.
      if ((g_tab == kStat || !m.data || !m.state.clockValid) && x < 88 && y >= 30 && y < 160) {
        g_screen = Screen::OwnerCard;
        g_cardReturn = Screen::Tabs;
        return r;
      }
      // LOG: tap the top or bottom half of the list to page through it.
      if (g_tab == kLog && m.data && m.state.clockValid) {
        const int top = 52, bottom = 52 + kLogRows * kLogRowH;
        if (y >= top - 4 && y < bottom) {
          g_logTop += y < (top + bottom) / 2 ? -(kLogRows - 1) : (kLogRows - 1);
          if (g_logTop < 0) g_logTop = 0;  // upper clamp in drawLog
        }
      }
      return r;

    case Screen::Menu:
    case Screen::Debug: {
      if (title) return handle(hold, m, now);
      const bool menu = g_screen == Screen::Menu;
      const int count = menu ? (int)kMenuCount : scenarios::count() + 1;
      const int rowH = menu ? 20 : 18;  // as passed to drawList()
      int& sel = menu ? g_menuSel : g_debugSel;
      const int visible = (cfg::kScreenH - 34) / rowH;
      int top = sel - visible + 1;
      if (top < 0) top = 0;
      if (y < 31) return r;
      const int row = (y - 31) / rowH;
      if (row >= visible || top + row >= count) return r;
      const int i = top + row;
      if (row == visible - 1 && i < count - 1) {  // bottom row of a long list: scroll
        sel = i + 1;
        return r;
      }
      sel = i;
      return handle(press, m, now);
    }

    case Screen::OwnerCard:
      return handle(press, m, now);

    case Screen::Help: {
      if (title) return handle(hold, m, now);
      input::Events step;
      step.turn = y >= cfg::kScreenH / 2 ? 1 : -1;
      return handle(step, m, now);
    }

    case Screen::About:
      return handle(title ? hold : press, m, now);  // body taps count for the hidden menu

    case Screen::ConfirmPortal:
      if (title) return handle(hold, m, now);
      if (y >= 146 && y < 186) {  // the OPEN / CANCEL buttons
        if (x >= 36 && x < 144) {
          g_confirmYes = true;
          return handle(press, m, now);
        }
        if (x >= 176 && x < 284) {
          g_confirmYes = false;
          return handle(press, m, now);
        }
      }
      return r;
  }
  return r;
}

void render(const Model& m, uint32_t t) {
  auto& c = canvas();
  c.fillScreen(kBg);

  if (m.updatePct >= 0) {
    drawUpdating("INSTALLING UPDATE", m.updatePct);
    return;
  }
  if (m.portal) {
    drawPortal(m);
  } else if (g_screen == Screen::Tabs) {
    header(m);
    if (!m.data) {
      drawCentredScreen("NO DATA YET",
                        m.wifiUp ? "Fetching the club file..." : "Waiting for Wi-Fi...",
                        m.syncError[0] ? m.syncError : nullptr,
                        "Hold 2 s, or tap the clock, for settings.", t);
    } else if (!m.state.clockValid) {
      drawCentredScreen("SYNCING CLOCK",
                        m.wifiUp ? "Asking the time servers..." : "Waiting for Wi-Fi to set the time.",
                        "Day counts appear once the clock is set.", nullptr, t);
    } else {
      switch (g_tab) {
        case kStat: drawStat(m, t); break;
        case kData: drawData(m); break;
        case kLog: drawLog(m); break;
      }
    }
  } else {
    switch (g_screen) {
      case Screen::Menu: {
        drawList("SETTINGS", kMenuCount, g_menuSel, menuLabel, m);
        break;
      }
      case Screen::OwnerCard: drawOwnerCard(m); break;
      case Screen::Help: drawHelp(); break;
      case Screen::About: drawAbout(m); break;
      case Screen::Debug: drawList("PREVIEW STATES", scenarios::count() + 1, g_debugSel, debugLabel, m, 18); break;
      case Screen::ConfirmPortal: drawConfirmPortal(); break;
      default: break;
    }
  }
  previewBanner(m);
  present(m.effects, t);
}

// ---- Start-up screen --------------------------------------------------------
// Pure theatre: a made-up boot log scrolls past while the real start-up is a
// few lines in setup(). Messages alternate with hex dumps of nothing at all.

namespace {
const char* const kBootLines[] = {
    "BC-BIOS 3.0  (C) BOOK-CLUB INDUSTRIES",
    "CPU XTENSA LX6 @ 240 MHZ ....... OK",
    "MEMORY TEST 327680 BYTES ....... OK",
    "MOUNTING /DEV/BOOKSHELF ........ OK",
    "LOADING EXPOSURE TABLES ........ OK",
    "CALIBRATING GEIGER COUNTER ..... OK",
    "CHECKING SNACK RESERVES ........ LOW",
    "PARSING QUEST LOG .............. OK",
    "NEGOTIATING FUN CAPS ........... PENDING",
    "WARMING UP VACUUM TUBES ........ OK",
    "HANDSHAKE WITH CLUB MAINFRAME .. OK",
    "ALL SYSTEMS NOMINAL",
};
constexpr int kBootMsgs = sizeof kBootLines / sizeof kBootLines[0];
constexpr int kBootSteps = kBootMsgs * 2 - 1;  // a hex dump between messages
constexpr int kBootLogRows = 7;

// The Wide Glide logo: coverage 1..15 onto the theme's ramp, solid = hot.
void drawLogo(int x0, int y0) {
  auto& c = canvas();
  const int stride = (logo::kW + 1) / 2;
  for (int y = 0; y < logo::kH; ++y) {
    for (int x = 0; x < logo::kW; ++x) {
      const uint8_t b = logo::kPx[y * stride + x / 2];
      const int cov = x % 2 ? (b & 15) : (b >> 4);
      if (!cov) continue;
      c.drawPixel(x0 + x, y0 + y, cov >= 14 ? kHot : (uint8_t)((cov * 12 + 7) / 15));
    }
  }
}

void bootLine(int step, char* buf, size_t len) {
  if (step % 2 == 0) {
    snprintf(buf, len, "%s", kBootLines[step / 2]);
    return;
  }
  uint32_t v = 0x9E3779B9u * (uint32_t)(step + 7);  // any old noise
  int n = snprintf(buf, len, "%08lX:", (unsigned long)(0x3FFB0000u + (v & 0xFFF0u)));
  for (int i = 0; i < 8 && n < (int)len - 4; ++i) {
    v = v * 1103515245u + 12345u;
    n += snprintf(buf + n, len - n, " %02X", (unsigned)((v >> 16) & 0xFF));
  }
}
}  // namespace

void drawBoot(uint32_t ms) {
  auto& c = canvas();
  c.fillScreen(kBg);
  drawLogo((cfg::kScreenW - logo::kW) / 2, 4);
  char loading[16];
  snprintf(loading, sizeof loading, "LOADING%.*s", (int)(ms / 300 % 4), "...");
  text(100, 54, loading, kBright, 2, 2);  // left-aligned so the dots don't shift it

  // The log: lines arrive evenly over the first 85% of the splash.
  const uint32_t logMs = cfg::kBootSplashMs * 85 / 100;
  int shown = (int)(ms * kBootSteps / logMs) + 1;
  if (shown > kBootSteps) shown = kBootSteps;
  const int top = 86;
  c.drawRect(8, top - 4, cfg::kScreenW - 16, kBootLogRows * 16 + 8, kFaint);
  const int first = shown > kBootLogRows ? shown - kBootLogRows : 0;
  for (int i = first; i < shown; ++i) {
    char line[56];
    bootLine(i, line, sizeof line);
    const bool newest = i == shown - 1;
    text(14, top + (i - first) * 16, line, newest ? kHot : (i % 2 ? kDim : kText));
  }
  if ((ms / 250) % 2) {  // blinking cursor after the newest line
    char line[56];
    bootLine(shown - 1, line, sizeof line);
    const int row = shown - 1 - first;
    c.fillRect(14 + textWidth(line, 1) + 2, top + row * 16 + 2, 6, 12, kBright);
  }

  int permille = (int)(ms * 1000 / cfg::kBootSplashMs);
  if (permille > 1000) permille = 1000;
  bar(20, 212, 280, 14, permille, kBright);
  present(true, ms);
}

void drawUpdating(const char* title, int pct) {
  auto& c = canvas();
  c.fillScreen(kBg);
  textCentered(160, 60, title, kHot, 2, 2);
  bar(30, 110, 260, 22, pct * 10, kBright);
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", pct);
  textCentered(160, 142, buf, kText, 2, 2);
  textCentered(160, 190, "DO NOT UNPLUG", kWarn, 1, 2);
  present(false, 0);
}

}  // namespace ui
