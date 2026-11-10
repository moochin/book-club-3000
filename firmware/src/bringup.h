// Bring-up mode (build with -e cyd_bringup): a hardware check for a new
// board. Never returns.
//
//   Screen  colour bars + corner labels; confirms driver, inversion, rotation
//   Turn    cycles rotation 0..3 live (find the one that reads upright)
//   Press   clicks the speaker and steps the backlight 25/50/100 %
//   Hold    toggles colour inversion live (fixes "photo negative" colours)
//   Serial  logs encoder counts, button state and raw light-sensor readings
#pragma once

namespace bringup {

[[noreturn]] void run();

}  // namespace bringup
