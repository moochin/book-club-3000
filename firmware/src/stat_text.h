// What the owner-card stats mean, by score. The wording lives in
// stat_text.cpp: edit it there.
#pragma once

#include <stdint.h>

namespace stattext {

// Score bands: 1-3, 4-6, 7-9 and a perfect 10.
constexpr int kBands = 4;
int band(int score);

// A short rank for this stat at this score ("WALKING IMDB"), and a sentence
// or two about it. Stat order as settings::statName().
const char* rank(int stat, int score);
const char* blurb(int stat, int score);

}  // namespace stattext
