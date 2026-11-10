// Preview scenarios for the hidden debug menu (About > press 5 times).
// Each one feeds a hard-coded sample file and a fake "now" through the real
// parser and state engine, so every exposure level and quest state can be
// checked on the device without editing the shared file.
#pragma once

#include "bc_data.h"
#include "bc_date.h"

namespace scenarios {

struct Scenario {
  const char* name;
  const char* last;   // last.date
  const char* next;   // next.date or nullptr
  const char* time;   // next.time or nullptr
  const char* today;  // fake local date
  int hh, mm;         // fake local time
};

int count();
const Scenario& get(int i);

// Builds the preview inputs for scenario i. Returns false on a bad scenario.
bool build(int i, bc::BookClubData& data, bc::LocalNow& now);

// The sample file from the handover, used when a unit has no data yet in the
// static-UI milestone and as the base for every scenario.
extern const char* const kSampleJson;

}  // namespace scenarios
