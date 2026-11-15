#include "stat_text.h"

#include "settings.h"

namespace stattext {

// ---- The wording: edit freely --------------------------------------------
//
// One block per stat, in the owner card's order. Each has four bands, by
// score: 1-3, 4-6, 7-9, then a perfect 10. Each band is a rank and a blurb.
//
// Ranks: up to 19 characters (they're shown large).
// Blurbs: up to about 200 characters; longer ones are cut off with a "~".
// Plain ASCII only: the screen's font has no curly quotes, dashes or accents.

struct Band {
  const char* rank;
  const char* blurb;
};

static const Band kText[settings::kStatCount][kBands] = {
    // GAMING SKILL
    {{"BUTTON MASHER",
      "Holds the controller like it owes them money. Has paused the game to ask which one "
      "they are."},
     {"WEEKEND WARRIOR",
      "Knows the controls, mostly. Wins the odd round and talks about it for a month."},
     {"SWEATY TRYHARD",
      "Plays on hard by default. Has opinions about frame rates and will share them."},
     {"FINAL BOSS",
      "Undefeated. Nobody asks for a rematch any more. The controller is part of their "
      "hand now."}},

    // MOVIE TRIVIA
    {{"WHAT'S THIS ONE?",
      "Asleep by the end of the first act. Thinks every actor is \"the guy from that "
      "thing\"."},
     {"PUB QUIZ RELIABLE",
      "Gets the big ones. Can name the director given a minute and a strong hint."},
     {"WALKING IMDB",
      "Spots the cameo before it happens. Knows which scenes were cut, and why."},
     {"THE ORACLE",
      "Mouths every line along with the film. Never, ever ask them for \"just one fun "
      "fact\"."}},

    // BEER CAPACITY
    {{"ONE AND DONE",
      "Half a shandy and a sit down. First onto the squash, first to bed."},
     {"STEADY PINTS",
      "Keeps pace, knows their limit and mostly respects it."},
     {"BOTTOMLESS",
      "Still standing at last orders, and has opinions about hops."},
     {"THE SPONGE",
      "Scientists want to study them. Bar staff know them by name. Has never once "
      "admitted to a hangover."}},

    // SNACK SUPPLY
    {{"EMPTY HANDED",
      "Turns up with nothing, then eats your crisps. Once brought a single satsuma."},
     {"BAG OF CRISPS",
      "Brings something. Usually ready salted. Nobody complains."},
     {"SNACK DEALER",
      "Arrives with a carrier bag of goods and knows everyone's favourite dip."},
     {"THE BUFFET",
      "Pockets full, boot full, cupboards full. Nobody has ever gone hungry on their "
      "watch."}},

    // BANTER
    {{"THE QUIET ONE",
      "Laughs along. Thinks of the perfect comeback on the drive home."},
     {"CHIPS IN",
      "Lands the odd zinger, then explains it, which kills it."},
     {"SHARP TONGUE",
      "Quick, cutting and usually right. Keeps the group chat alive."},
     {"ROAST MASTER",
      "Nobody is safe. Has reduced grown adults to tears, of both kinds."}},

    // PUNCTUALITY
    {{"ON THEIR WAY",
      "\"Leaving now\" means \"just got in the shower\". Arrives in time for the washing "
      "up."},
     {"FASHIONABLY LATE",
      "Ten minutes behind, with a reasonable excuse and snacks by way of apology."},
     {"BANG ON TIME",
      "Turns up exactly when they said. Slightly smug about it."},
     {"ALREADY THERE",
      "Was early to their own birth. Has been waiting outside since lunch."}},

    // STAYING POWER
    {{"EARLY NIGHT",
      "Yawning by nine, gone by ten. Found asleep on the sofa with their shoes on."},
     {"ONE MORE ROUND",
      "Makes it to midnight. Starts saying \"right then\" at about half eleven."},
     {"NIGHT OWL",
      "Still going when the snacks run out. Sees more sunrises than is healthy."},
     {"UNKILLABLE",
      "Has never been seen asleep. Will arrive at the next meeting still going from the "
      "last one."}},
};

// ---- End of the wording ---------------------------------------------------

int band(int score) {
  if (score >= 10) return 3;
  if (score >= 7) return 2;
  if (score >= 4) return 1;
  return 0;
}

static const Band& at(int stat, int score) {
  if (stat < 0 || stat >= settings::kStatCount) stat = 0;
  return kText[stat][band(score)];
}

const char* rank(int stat, int score) { return at(stat, score).rank; }
const char* blurb(int stat, int score) { return at(stat, score).blurb; }

}  // namespace stattext
