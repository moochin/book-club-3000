// Display: the LovyanGFX panel plus one full-screen 4-bit palette canvas.
//
// Everything is drawn into the canvas using palette indices (see Ink), then
// pushed in one go, so the screen never flickers mid-draw. The palette is a
// single accent colour ramp, so changing theme is just a palette swap.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

namespace display {

// Palette indices. 0..12 are a brightness ramp of the accent colour; the
// scanline/sweep effects step indices up or down that ramp.
enum Ink : uint8_t {
  kBg = 0,      // near-black, faintly tinted
  kFaint = 2,   // grid lines, scanline-dark
  kDim = 5,     // secondary text
  kMid = 8,     // borders
  kText = 10,   // body text
  kBright = 12, // headings, highlights
  kWarnDim = 13,
  kWarn = 14,   // warning colour (amber on green, red-orange on amber)
  kHot = 15,    // near-white accent, for the selected item
};

#ifndef BC_SIM
class Panel : public lgfx::LGFX_Device {
 public:
  Panel();

 private:
#if defined(BC_PANEL_ST7789)
  lgfx::Panel_ST7789 panel_;
#else
  lgfx::Panel_ILI9341 panel_;
#endif
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;
};
#else
using Panel = lgfx::LGFX_Device;  // host simulator: no real panel
#endif

void begin(uint8_t rotation, uint8_t theme);
void setRotation(uint8_t rotation);
void setTheme(uint8_t theme);
void setBrightness(uint8_t level);  // 0..255

Panel& panel();
LGFX_Sprite& canvas();

// Pushes the canvas, applying scanlines and a slow sweep band when enabled.
void present(bool effects, uint32_t nowMs);

// Text helpers for the terminal font (Font0 stretched to a tall 6x16 cell).
void text(int x, int y, const char* s, uint8_t ink = kText, int sx = 1, int sy = 2);
void textCentered(int cx, int y, const char* s, uint8_t ink = kText, int sx = 1, int sy = 2);
void textRight(int rx, int y, const char* s, uint8_t ink = kText, int sx = 1, int sy = 2);
int textWidth(const char* s, int sx = 1);
// Writes `s` wrapped to `maxChars` per line; returns the number of lines.
int textWrapped(int x, int y, const char* s, int maxChars, int maxLines, uint8_t ink = kText,
                int lineH = 18);

}  // namespace display
