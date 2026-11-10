// The mascot: PLACEHOLDER art until the final sprites exist (build plan
// step 6). Each pose is a simple labelled figure so the state logic can be
// checked on the device. Replace drawMascot()'s body with sprite blits later;
// the 64 x 96 box and the Pose enum are the interface.
#pragma once

#include <stdint.h>

#include "bc_state.h"

namespace mascot {

constexpr int kW = 64;
constexpr int kH = 96;

// Draws the pose with its top-left at (x, y). `frameMs` drives blinks and
// small idle animations. When `label` is set, the pose name is printed under
// the box (placeholder builds only).
void draw(int x, int y, bc::Pose pose, uint32_t frameMs, bool label = true);

}  // namespace mascot
