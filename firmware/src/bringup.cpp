#include "bringup.h"

#include <Arduino.h>

#include "audio.h"
#include "config.h"
#include "display.h"
#include "input.h"

namespace bringup {

static void drawPattern(lgfx::LGFX_Device& p, int rotation, bool inverted, int brightness) {
  p.setRotation(rotation);
  const int w = p.width(), h = p.height();
  p.fillScreen(TFT_BLACK);
  // Colour bars: if red shows as cyan (etc.), flip the inversion; if red and
  // blue are swapped, the panel needs rgb_order flipped in display.cpp.
  static const uint32_t kBars[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x3CFF6E, 0xFFB028};
  static const char* const kNames[] = {"RED", "GREEN", "BLUE", "WHITE", "THEME G", "THEME A"};
  const int bw = w / 6;
  for (int i = 0; i < 6; ++i) {
    p.fillRect(i * bw, 0, bw, h / 3, p.color888(kBars[i] >> 16, (kBars[i] >> 8) & 255, kBars[i] & 255));
    p.setTextColor(i == 3 ? TFT_BLACK : TFT_BLACK);
    p.setTextSize(1);
    p.setCursor(i * bw + 2, h / 3 - 10);
    p.print(kNames[i]);
  }
  // Grey ramp.
  for (int x = 0; x < w; ++x) {
    const uint8_t v = (uint8_t)(x * 255 / (w - 1));
    p.drawFastVLine(x, h / 3, 12, p.color888(v, v, v));
  }
  p.setTextColor(TFT_WHITE, TFT_BLACK);
  p.setTextSize(1, 2);
  p.setCursor(4, h / 3 + 20);
#if defined(BC_PANEL_ST7789)
  p.printf("DRIVER ST7789  INVERT %d", inverted ? 1 : 0);
#else
  p.printf("DRIVER ILI9341  INVERT %d", inverted ? 1 : 0);
#endif
  p.setCursor(4, h / 3 + 40);
  p.printf("ROTATION %d   BACKLIGHT %d%%", rotation, brightness * 100 / 255);
  p.setCursor(4, h / 3 + 60);
  p.print("TURN: ROTATE  PRESS: BEEP+LIGHT");
  p.setCursor(4, h / 3 + 80);
  p.print("HOLD 2S: TOGGLE INVERSION");
  // Corners: the case wants USB-C on the left and the JST plugs at the top.
  p.setTextColor(TFT_YELLOW, TFT_BLACK);
  p.setCursor(2, h - 18);
  p.print("BOTTOM-LEFT");
  p.setCursor(w - 6 * 12 - 2, h - 18);
  p.print("BOTTOM-RIGHT");
  p.drawRect(0, 0, w, h, TFT_YELLOW);
}

void run() {
  auto& p = display::panel();
  int rotation = cfg::kDefaultRotation;
  bool inverted = BC_PANEL_INVERT;
  int brightness = 255;
  int step = 0;
  p.setBrightness(brightness);
  drawPattern(p, rotation, inverted, brightness);
  Serial.println("[bringup] ready: turn = rotation, press = beep + backlight, hold = invert");
  audio::setEnabled(true);
  uint32_t lastLog = 0;
  int64_t turns = 0;
  for (;;) {
    const uint32_t now = millis();
    const input::Events e = input::poll(now);
    if (e.turn) {
      turns += e.turn;
      audio::knobClick();
      rotation = (rotation + (e.turn > 0 ? 1 : 3)) & 3;
      Serial.printf("[bringup] turn %+d (total %lld) -> rotation %d\n", e.turn, (long long)turns, rotation);
      drawPattern(p, rotation, inverted, brightness);
    }
    if (e.shortPress) {
      static const int kLevels[] = {64, 128, 255};
      step = (step + 1) % 3;
      brightness = kLevels[step];
      p.setBrightness(brightness);
      audio::confirmBeep();
      Serial.printf("[bringup] short press -> backlight %d\n", brightness);
      drawPattern(p, rotation, inverted, brightness);
    }
    if (e.longPress) {
      inverted = !inverted;
      p.invertDisplay(inverted);
      Serial.printf("[bringup] long press -> invert %d\n", inverted);
      drawPattern(p, rotation, inverted, brightness);
    }
    if (now - lastLog >= 500) {
      lastLog = now;
      const int ldr = analogRead(cfg::kPinLdr);
      Serial.printf("[bringup] ldr=%d sw=%d boot=%d\n", ldr, digitalRead(cfg::kPinEncSw),
                    digitalRead(cfg::kPinBoot));
      p.setTextColor(TFT_CYAN, TFT_BLACK);
      p.setTextSize(1, 2);
      p.setCursor(4, p.height() / 3 + 100);
      p.printf("LDR %4d  SW %d  TURNS %4lld ", ldr, digitalRead(cfg::kPinEncSw), (long long)turns);
    }
    audio::tick(now);
    delay(5);  // bring-up only; the real firmware loop never blocks
  }
}

}  // namespace bringup
