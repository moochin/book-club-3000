// The mascot. Poses drawn as PNGs in art/mascot/ (see art/README.md) are
// baked into art_data.h at build time; any pose without art falls back to a
// labelled placeholder figure, so the state logic can always be checked.
#pragma once

#include <stdint.h>

#include "bc_state.h"

namespace mascot {

constexpr int kW = 64;
constexpr int kH = 96;

// Draws the pose with its top-left at (x, y). `frameMs` drives animation.
// When `label` is set, placeholders print the pose name under the box.
void draw(int x, int y, bc::Pose pose, uint32_t frameMs, bool label = true);

// True when real art exists for the pose.
bool hasArt(bc::Pose pose);
// The built-in placeholder figure (used to export starting templates).
void drawPlaceholder(int x, int y, bc::Pose pose, uint32_t frameMs, bool label);

}  // namespace mascot
