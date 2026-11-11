// Compile-time configuration: pins, timings and tunables.
// Per-unit values (owner, theme, rotation, URLs...) live in NVS, see settings.h.
#pragma once

#include <stdint.h>

#define BC_FW_VERSION "1.2.0"

#if !defined(BC_PANEL_ILI9341) && !defined(BC_PANEL_ST7789)
#define BC_PANEL_ILI9341
#endif
#ifndef BC_PANEL_INVERT
#define BC_PANEL_INVERT 0
#endif
#ifndef BC_BRINGUP
#define BC_BRINGUP 0
#endif

namespace cfg {

// ---- Pins (ESP32-2432S028R "CYD") -----------------------------------------
constexpr int kPinTftDc = 2;
constexpr int kPinTftMiso = 12;
constexpr int kPinTftMosi = 13;
constexpr int kPinTftSck = 14;
constexpr int kPinTftCs = 15;
constexpr int kPinBacklight = 21;
constexpr int kPinSpeaker = 26;
constexpr int kPinLedR = 4;  // RGB LED, active low
constexpr int kPinLedG = 16;
constexpr int kPinLedB = 17;
constexpr int kPinLdr = 34;
constexpr int kPinEncA = 22;
constexpr int kPinEncB = 27;
constexpr int kPinEncSw = 35;  // input only; external 10k pull-up to 3.3 V
constexpr int kPinBoot = 0;
// XPT2046 resistive touch, on its own SPI pins (VSPI).
constexpr int kPinTouchSck = 25;
constexpr int kPinTouchMosi = 32;
constexpr int kPinTouchMiso = 39;
constexpr int kPinTouchCs = 33;
constexpr int kPinTouchIrq = 36;

// ---- LEDC channels ----------------------------------------------------------
constexpr int kLedcSpeaker = 0;
constexpr int kLedcBacklight = 7;  // used by LovyanGFX's Light_PWM

// ---- Screen -----------------------------------------------------------------
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr uint8_t kDefaultRotation = 1;  // confirm at bring-up (README)
constexpr uint32_t kFrameMsEffects = 66;  // ~15 fps while scanline sweep runs
constexpr uint32_t kFrameMsIdle = 1000;   // otherwise redraw once a second
constexpr uint32_t kBootSplashMs = 3200;  // start-up screen with the scrolling boot log

// ---- Knob -------------------------------------------------------------------
constexpr int kEncCountsPerDetent = 4;  // EC11 full-quadrature: 4 counts per click
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kLongPressMs = 2000;
constexpr uint32_t kKnobArmMs = 500;  // switch must read released this long first
// Touch: tap tabs, menu items and buttons directly; hold anywhere 2 s for a
// long press. Raw XPT2046 range: calibrate if taps land in the wrong place
// (input.cpp logs each tap's screen position).
constexpr uint16_t kTouchXMin = 300, kTouchXMax = 3900;
constexpr uint16_t kTouchYMin = 200, kTouchYMax = 3700;
// The USB-C ST7789 board's touch layer runs the other way along the panel's
// long-edge axis (LovyanGFX ignores min > max, so input.cpp flips it).
constexpr bool kTouchFlip = true;
constexpr uint32_t kTouchReleaseMs = 60;  // gap that counts as lifting the finger
constexpr uint32_t kFactoryResetHoldMs = 10000;  // BOOT button at power-on

// ---- Sync -------------------------------------------------------------------
constexpr uint32_t kSyncIntervalMs = 24UL * 60 * 60 * 1000;
constexpr uint32_t kSyncJitterMs = 30UL * 60 * 1000;
// Retry delays after a failed fetch, then hourly.
constexpr uint32_t kSyncRetryMs[] = {2UL * 60 * 1000, 10UL * 60 * 1000, 30UL * 60 * 1000,
                                     60UL * 60 * 1000};
constexpr uint32_t kSyncStaleSecs = 7UL * 24 * 60 * 60;  // warning icon after 7 days
constexpr uint32_t kHttpTimeoutMs = 15000;
// No built-in data URL: the club file lives in a secret gist whose link must
// stay out of this (public) repo. Each unit is given it on the setup page.
constexpr const char* kDefaultDataUrl = "";

// ---- Wi-Fi ------------------------------------------------------------------
constexpr uint32_t kWifiRetryMs = 60UL * 1000;          // background reconnect cadence
constexpr uint32_t kWifiOfflineNoticeMs = 5UL * 60 * 1000;  // "offline" after 5 min
constexpr uint32_t kMenuPortalTimeoutS = 15 * 60;       // portal opened from the menu

// ---- Power and comfort --------------------------------------------------------
constexpr uint8_t kBacklightFloor = 24;   // 0..255; keeps the screen readable
constexpr uint8_t kBacklightMax = 255;
// LDR: on the CYD the reading falls as the room gets brighter. Calibrate at
// bring-up (the bring-up build logs raw readings).
constexpr bool kLdrBrightIsLow = true;
constexpr int kLdrBright = 0;    // raw reading in a bright room
constexpr int kLdrDark = 900;    // raw reading in a dark room
constexpr int kLdrDarkRoom = 700;  // at or beyond this counts as "dark" for blanking
constexpr uint32_t kBlankAfterDarkMs = 10UL * 60 * 1000;

// ---- Sound --------------------------------------------------------------------
constexpr int kQuietFromMinute = 22 * 60;  // no sound 22:00..08:00
constexpr int kQuietToMinute = 8 * 60;

}  // namespace cfg
