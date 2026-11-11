#include "display.h"

#include <string.h>

namespace display {

#ifndef BC_SIM
Panel::Panel() {
  {
    auto c = bus_.config();
    c.spi_host = HSPI_HOST;
    c.spi_mode = 0;
    c.freq_write = 40000000;
    c.freq_read = 16000000;
    c.spi_3wire = false;
    c.use_lock = true;
    c.dma_channel = SPI_DMA_CH_AUTO;
    c.pin_sclk = cfg::kPinTftSck;
    c.pin_mosi = cfg::kPinTftMosi;
    c.pin_miso = cfg::kPinTftMiso;
    c.pin_dc = cfg::kPinTftDc;
    bus_.config(c);
    panel_.setBus(&bus_);
  }
  {
    auto c = panel_.config();
    c.pin_cs = cfg::kPinTftCs;
    c.pin_rst = -1;
    c.pin_busy = -1;
    c.panel_width = 240;
    c.panel_height = 320;
    c.offset_x = 0;
    c.offset_y = 0;
    c.offset_rotation = 0;
    c.readable = true;
    c.invert = BC_PANEL_INVERT;
    c.rgb_order = false;
    c.dlen_16bit = false;
    c.bus_shared = false;
    panel_.config(c);
  }
  {
    auto c = light_.config();
    c.pin_bl = cfg::kPinBacklight;
    c.invert = false;
    c.freq = 12000;
    c.pwm_channel = cfg::kLedcBacklight;
    light_.config(c);
    panel_.setLight(&light_);
  }
  {
    auto c = touch_.config();
    c.x_min = cfg::kTouchXMin;
    c.x_max = cfg::kTouchXMax;
    c.y_min = cfg::kTouchYMin;
    c.y_max = cfg::kTouchYMax;
    c.pin_int = cfg::kPinTouchIrq;
    c.bus_shared = false;
    c.offset_rotation = 0;
    c.spi_host = VSPI_HOST;
    c.freq = 1000000;
    c.pin_sclk = cfg::kPinTouchSck;
    c.pin_mosi = cfg::kPinTouchMosi;
    c.pin_miso = cfg::kPinTouchMiso;
    c.pin_cs = cfg::kPinTouchCs;
    touch_.config(c);
    panel_.setTouch(&touch_);
  }
  setPanel(&panel_);
}

static Panel g_panel;
static LGFX_Sprite g_canvas(&g_panel);
#else
// Host simulator (sim/): the canvas has no panel; present() hands it over.
void simPresent(LGFX_Sprite& canvas);
static LGFX_Sprite g_canvas;
#endif
static uint8_t g_dimLut[256];
static uint8_t g_brightLut[256];

struct Rgb {
  uint8_t r, g, b;
};

static uint8_t lerp8(uint8_t a, uint8_t b, int num, int den) {
  return (uint8_t)(a + ((int)b - (int)a) * num / den);
}

static void buildPalette(uint8_t theme) {
  // Accent colours from the handover: green #3CFF6E, amber #FFB028.
  const Rgb accent = theme == 1 ? Rgb{0xFF, 0xB0, 0x28} : Rgb{0x3C, 0xFF, 0x6E};
  const Rgb warn = theme == 1 ? Rgb{0xFF, 0x50, 0x30} : Rgb{0xFF, 0xB0, 0x28};
  const Rgb bg = theme == 1 ? Rgb{0x0C, 0x08, 0x02} : Rgb{0x03, 0x0C, 0x05};
  // Ramp 0..12: background to full accent, gamma-ish so low shades stay visible.
  static const uint8_t kCurve[13] = {0, 8, 18, 30, 44, 60, 78, 98, 120, 150, 185, 220, 255};
  for (int i = 0; i <= 12; ++i) {
    g_canvas.setPaletteColor(i, lerp8(bg.r, accent.r, kCurve[i], 255),
                             lerp8(bg.g, accent.g, kCurve[i], 255),
                             lerp8(bg.b, accent.b, kCurve[i], 255));
  }
  g_canvas.setPaletteColor(kWarnDim, warn.r / 2, warn.g / 2, warn.b / 2);
  g_canvas.setPaletteColor(kWarn, warn.r, warn.g, warn.b);
  g_canvas.setPaletteColor(kHot, lerp8(accent.r, 255, 1, 2), lerp8(accent.g, 255, 1, 2),
                           lerp8(accent.b, 255, 1, 2));
}

static uint8_t dimIndex(uint8_t i) {
  if (i == kBg) return kBg;
  if (i <= 12) return i - 1;
  if (i == kWarn) return kWarnDim;
  if (i == kHot) return kBright;
  return i;
}

static uint8_t brightIndex(uint8_t i) {
  if (i == kBg) return 1;
  if (i < 12) return i + 1;
  return i;
}

static void buildLuts() {
  for (int b = 0; b < 256; ++b) {
    const uint8_t hi = (uint8_t)(b >> 4), lo = (uint8_t)(b & 15);
    g_dimLut[b] = (uint8_t)(dimIndex(hi) << 4 | dimIndex(lo));
    g_brightLut[b] = (uint8_t)(brightIndex(hi) << 4 | brightIndex(lo));
  }
}

void begin(uint8_t rotation, uint8_t theme) {
#ifndef BC_SIM
  g_panel.init();
  g_panel.setRotation(rotation & 3);
  g_panel.fillScreen(TFT_BLACK);
#else
  (void)rotation;
#endif
  g_canvas.setColorDepth(4);
  g_canvas.setPsram(false);
  if (!g_canvas.createSprite(cfg::kScreenW, cfg::kScreenH)) {
#ifndef BC_SIM
    Serial.println("[display] canvas allocation failed");
#endif
  }
  g_canvas.createPalette();
  buildPalette(theme);
  buildLuts();
  g_canvas.setFont(&fonts::Font0);
  g_canvas.setTextWrap(false);
}

void setTheme(uint8_t theme) { buildPalette(theme); }
#ifndef BC_SIM
void setRotation(uint8_t rotation) { g_panel.setRotation(rotation & 3); }
void setBrightness(uint8_t level) { g_panel.setBrightness(level); }
Panel& panel() { return g_panel; }
#endif
LGFX_Sprite& canvas() { return g_canvas; }

static int8_t g_shiftX = 0, g_shiftY = 0;
static bool g_shiftChanged = false;

void setShift(int8_t dx, int8_t dy) {
  if (dx == g_shiftX && dy == g_shiftY) return;
  g_shiftX = dx;
  g_shiftY = dy;
  g_shiftChanged = true;
}

void present(bool effects, uint32_t nowMs) {
  if (effects) {
    uint8_t* buf = static_cast<uint8_t*>(g_canvas.getBuffer());
    if (buf) {
      const int stride = cfg::kScreenW / 2;  // 4 bpp: two pixels per byte
      // A soft band, 14 rows tall, sweeping down the screen every ~9 s.
      const int band = (int)((nowMs / 30) % (cfg::kScreenH + 60)) - 30;
      for (int y = 0; y < cfg::kScreenH; ++y) {
        uint8_t* row = buf + y * stride;
        const bool inBand = y >= band && y < band + 14;
        if (inBand) {
          for (int x = 0; x < stride; ++x) row[x] = g_brightLut[row[x]];
        } else if (y % 3 == 2) {
          for (int x = 0; x < stride; ++x) row[x] = g_dimLut[row[x]];
        }
      }
    }
  }
#ifndef BC_SIM
  if (g_shiftChanged) {
    // Clear the strip the move uncovers, so no stale pixels stay behind.
    g_panel.fillScreen(TFT_BLACK);
    g_shiftChanged = false;
  }
  g_canvas.pushSprite(g_shiftX, g_shiftY);
#else
  simPresent(g_canvas);
#endif
}

void text(int x, int y, const char* s, uint8_t ink, int sx, int sy) {
  g_canvas.setTextSize(sx, sy);
  g_canvas.setTextColor(ink);
  g_canvas.setCursor(x, y);
  g_canvas.print(s);
}

int textWidth(const char* s, int sx) { return (int)strlen(s) * 6 * sx; }

void textCentered(int cx, int y, const char* s, uint8_t ink, int sx, int sy) {
  text(cx - textWidth(s, sx) / 2, y, s, ink, sx, sy);
}

void textRight(int rx, int y, const char* s, uint8_t ink, int sx, int sy) {
  text(rx - textWidth(s, sx), y, s, ink, sx, sy);
}

int textWrapped(int x, int y, const char* s, int maxChars, int maxLines, uint8_t ink, int lineH) {
  char line[64];
  if (maxChars > (int)sizeof line - 1) maxChars = sizeof line - 1;
  int lines = 0;
  while (*s && lines < maxLines) {
    while (*s == ' ') ++s;
    int n = (int)strlen(s);
    if (n > maxChars) {
      n = maxChars;
      int cut = n;
      while (cut > 0 && s[cut] != ' ') --cut;
      if (cut > maxChars / 3) n = cut;
      if (lines == maxLines - 1) {  // last line: ellipsis
        n = maxChars - 1;
        memcpy(line, s, (size_t)n);
        line[n] = '~';
        line[n + 1] = 0;
        text(x, y + lines * lineH, line, ink);
        return lines + 1;
      }
    }
    memcpy(line, s, (size_t)n);
    line[n] = 0;
    text(x, y + lines * lineH, line, ink);
    s += n;
    ++lines;
  }
  return lines;
}

}  // namespace display
