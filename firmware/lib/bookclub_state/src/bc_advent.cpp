#include "bc_advent.h"

namespace bc {

// Built in: one line a day, in the voice of the club mainframe. Door 8
// answers door 7's question. ASCII, at most kAdventTextLen - 1 characters
// (the native tests check both).
static const char* const kDefaults[kAdventDoors] = {
    "Festive radiation detected. Exposure is unavoidable. Recommended treatment: mince pies.",
    "Today's challenge: name five Christmas films without saying Die Hard. Then argue about Die Hard.",
    "Fun Caps tip: offering to wrap the presents earns goodwill. Wrapping them badly earns more.",
    "Wasteland survival rule 4: never trust a selection box with the good ones already gone.",
    "Q: What do you call a book club that never reads any books? A: Accurate.",
    "Snack reserves check. If they read LOW, this is your sign to stock up on crisps.",
    "Pop quiz: how many ghosts visit Scrooge? The answer is behind tomorrow's door. No peeking.",
    "Yesterday's answer: four, if you count Marley. Most people forget Marley. Marley is still upset.",
    "Co-op tip: a Christmas jumper gives +2 to Banter and -3 to Dignity. Worth it.",
    "The mainframe has calculated the perfect amount of cheese for the season. It is: more.",
    "Signal intercepted: SOMEONE IN THE CLUB HAS NOT BOUGHT A SINGLE PRESENT. You know who you are.",
    "The Twelve Days of Christmas add up to 364 gifts. Your loft will never recover.",
    "Film night rule: whoever falls asleep first has to pick the next film. Stay awake.",
    "Stamina check: the festive season is a marathon, not a sprint. Pace the sherry.",
    "Today's quest: send a club member something nice. Then a meme. Balance must be kept.",
    "Fact: a cracker joke has never once been funny. That is the point. Respect the tradition.",
    "Board game warning: Monopoly has ended more friendships than the bombs. Play with care.",
    "One week to go. If the tree is still in the loft, the mainframe is not judging. Much.",
    "Leftovers forecast: turkey sandwiches until mid-January, with a strong chance of curry.",
    "Pro tip: the TV remote is the most valuable item in the house this week. Guard it well.",
    "The shortest day of the year. More dark means more time for games. The mainframe approves.",
    "Wrapping tip: if it's a bottle, everyone knows it's a bottle. Wrap it anyway.",
    "Unit check: Wi-Fi strong, snacks stocked, sofa claimed. Ready for the big one.",
    "Christmas Eve. All units on standby for festive cheer. Merry Christmas from the club mainframe.",
};

int adventDay(const LocalNow& now) {
  if (!now.valid || now.date.month != 12) return 0;
  return now.date.day <= kAdventDoors ? now.date.day : kAdventDoors + 1;
}

bool adventCanOpen(int door, int today) {
  return door >= 1 && door <= kAdventDoors && today >= 1 && door <= today;
}

uint32_t adventOpenedThisYear(int16_t recordYear, uint32_t mask, int16_t year) {
  if (recordYear != year) return 0;
  return mask & ((1UL << kAdventDoors) - 1);
}

bool adventWaiting(uint32_t mask, int today) {
  for (int d = 1; d <= kAdventDoors; ++d)
    if (adventCanOpen(d, today) && !adventIsOpen(mask, d)) return true;
  return false;
}

int adventOpenCount(uint32_t mask) {
  int n = 0;
  for (int d = 1; d <= kAdventDoors; ++d) n += adventIsOpen(mask, d);
  return n;
}

const char* adventDefaultText(int door) {
  return door >= 1 && door <= kAdventDoors ? kDefaults[door - 1] : "";
}

const char* adventText(const BookClubData* data, int door) {
  if (door < 1 || door > kAdventDoors) return "";
  if (data) {
    const uint16_t off = data->advent.offset[door - 1];
    if (off != AdventConfig::kNone) return data->advent.pool + off;
  }
  return kDefaults[door - 1];
}

AdventIcon adventIcon(int door) {
  if (door < 1) door = 1;
  return (AdventIcon)((door - 1) % (int)AdventIcon::Count);
}

}  // namespace bc
