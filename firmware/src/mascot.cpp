#include "mascot.h"

#include "display.h"

using display::canvas;

namespace mascot {

using bc::Pose;

// A deliberately simple, original figure: a round-headed reader with big
// round glasses, a dosimeter antenna and a cardigan, clutching a book.
// Placeholder only.
namespace {

struct Look {
  int headDx = 0, headDy = 0;   // slump offsets
  int armL = 0, armR = 0;       // arm angles, 0 = down, 2 = out, 4 = up
  bool grin = false, frown = false, wavy = false;
  bool blink = false, starEyes = false, xEye = false;
  bool tinge = false;           // queasy dither on the face
  bool ragged = false;          // ghoul: chipped outline, patches
  bool thumbsUp = false, sign = false, hourglass = false, confetti = false;
};

void arm(int sx, int sy, int dir, int pose) {
  // pose 0: down, 1: slight out, 2: out, 3: up-out, 4: up
  static const int8_t kDx[5] = {2, 8, 14, 12, 6};
  static const int8_t kDy[5] = {22, 18, 6, -10, -16};
  const int ex = sx + dir * kDx[pose], ey = sy + kDy[pose];
  canvas().drawLine(sx, sy, ex, ey, display::kText);
  canvas().drawLine(sx, sy + 1, ex, ey + 1, display::kText);
  canvas().fillCircle(ex, ey, 2, display::kText);
}

}  // namespace

void draw(int x, int y, Pose pose, uint32_t t, bool label) {
  auto& c = canvas();
  Look k;
  const bool blinkNow = (t % 4200) < 140;
  switch (pose) {
    case Pose::Level0: k.grin = true; k.thumbsUp = true; k.armR = 3; break;
    case Pose::Level1: k.blink = blinkNow; k.armL = 1; k.armR = 1; break;
    case Pose::Level2: k.wavy = true; k.tinge = true; k.armL = 1; k.armR = 0; break;
    case Pose::Level3: k.frown = true; k.headDx = 4; k.headDy = 6; k.blink = blinkNow || (t % 1500) < 400; break;
    case Pose::Level4: k.ragged = true; k.xEye = true; k.wavy = true; k.headDx = 3; k.headDy = 4; k.armL = 2; break;
    case Pose::FinalWeek: k.starEyes = true; k.grin = true; k.armL = 4; k.armR = 4; k.headDy = (t / 250) % 2 ? -2 : 0; break;
    case Pose::MeetingDay: k.grin = true; k.armL = 4; k.armR = 4; k.confetti = true; k.headDy = (t / 180) % 2 ? -3 : 0; break;
    case Pose::NoDate: k.sign = true; k.blink = blinkNow; k.armR = 3; break;
    case Pose::Syncing: k.hourglass = true; break;
  }

  // Frame (placeholder box).
  c.drawRect(x, y, kW, kH, display::kFaint);

  if (k.hourglass) {
    const int cx = x + kW / 2, cy = y + kH / 2;
    c.drawTriangle(cx - 14, cy - 24, cx + 14, cy - 24, cx, cy, display::kText);
    c.drawTriangle(cx - 14, cy + 24, cx + 14, cy + 24, cx, cy, display::kText);
    const int sand = (int)((t / 200) % 12);
    c.fillTriangle(cx - 8 + sand / 2, cy + 24 - sand, cx + 8 - sand / 2, cy + 24 - sand, cx, cy + 24, display::kBright);
    c.drawFastVLine(cx, cy, 24, display::kDim);
  } else {
    const int hx = x + 32 + k.headDx, hy = y + 22 + k.headDy;
    // Body: an open cardigan over a jumper, with a book held to the chest.
    const int bx = x + 18, by = y + 42 + k.headDy / 2;
    c.fillRoundRect(bx, by, 28, 32, 5, display::kFaint);
    c.drawRoundRect(bx, by, 28, 32, 5, display::kText);
    c.drawLine(bx + 9, by + 1, bx + 11, by + 31, display::kMid);  // cardigan edges
    c.drawLine(bx + 18, by + 1, bx + 16, by + 31, display::kMid);
    c.fillRect(bx + 7, by + 6, 14, 11, display::kMid);  // the book
    c.drawFastVLine(bx + 14, by + 6, 11, display::kBg);
    // Legs.
    c.fillRect(bx + 4, by + 32, 7, 14, display::kText);
    c.fillRect(bx + 17, by + 32, 7, 14, display::kText);
    c.fillRect(bx + 2, by + 45, 10, 3, display::kBright);
    c.fillRect(bx + 16, by + 45, 10, 3, display::kBright);
    // Arms.
    arm(bx, by + 4, -1, k.armL);
    arm(bx + 27, by + 4, 1, k.armR);
    if (k.thumbsUp) {
      c.fillRect(bx + 27 + 12 - 1, by + 4 - 10 - 7, 3, 6, display::kBright);
    }
    // Head.
    c.fillCircle(hx, hy, 16, display::kBg);
    c.drawCircle(hx, hy, 16, display::kBright);
    c.drawCircle(hx, hy, 15, display::kText);
    // Dosimeter antenna: its bulb glows brighter as exposure rises.
    c.drawLine(hx + 6, hy - 15, hx + 10, hy - 22, display::kText);
    c.fillCircle(hx + 10, hy - 22, 2, k.ragged || k.tinge ? display::kWarn : display::kBright);
    if (k.tinge) {
      for (int py = -10; py <= 12; py += 2)
        for (int px = -12 + (py & 2); px <= 12; px += 4)
          if (px * px + py * py < 150) c.drawPixel(hx + px, hy + py, display::kWarnDim);
    }
    if (k.ragged) {
      c.fillCircle(hx + 11, hy - 8, 4, display::kBg);  // chipped outline
      c.fillCircle(hx - 13, hy + 6, 3, display::kBg);
      c.fillRect(hx - 9, hy + 3, 5, 4, display::kWarnDim);  // patches
      c.fillRect(bx + 18, by + 25, 6, 5, display::kWarnDim);
    }
    // Eyes.
    const int ey = hy - 3;
    if (k.starEyes) {
      for (int s = -1; s <= 1; s += 2) {
        const int ex = hx + s * 6;
        c.drawFastHLine(ex - 3, ey, 7, display::kHot);
        c.drawFastVLine(ex, ey - 3, 7, display::kHot);
      }
    } else if (k.blink) {
      c.drawFastHLine(hx - 8, ey, 5, display::kBright);
      c.drawFastHLine(hx + 4, ey, 5, display::kBright);
    } else {
      c.fillRect(hx - 7, ey - 2, 3, 4, display::kBright);
      if (k.xEye) {
        c.drawLine(hx + 4, ey - 2, hx + 8, ey + 2, display::kBright);
        c.drawLine(hx + 4, ey + 2, hx + 8, ey - 2, display::kBright);
      } else {
        c.fillRect(hx + 5, ey - 2, 3, 4, display::kBright);
      }
    }
    // Big round glasses.
    c.drawCircle(hx - 6, ey, 5, display::kMid);
    c.drawCircle(hx + 6, ey, 5, display::kMid);
    c.drawFastHLine(hx - 1, ey - 1, 3, display::kMid);
    // Mouth.
    const int my = hy + 7;
    if (k.grin) {
      c.drawLine(hx - 7, my - 1, hx - 3, my + 3, display::kBright);
      c.drawFastHLine(hx - 3, my + 3, 7, display::kBright);
      c.drawLine(hx + 3, my + 3, hx + 7, my - 1, display::kBright);
    } else if (k.wavy) {
      for (int i = -7; i <= 7; ++i) c.drawPixel(hx + i, my + ((i + 8) / 3 % 2), display::kBright);
    } else if (k.frown) {
      c.drawLine(hx - 6, my + 3, hx - 2, my, display::kBright);
      c.drawFastHLine(hx - 2, my, 5, display::kBright);
      c.drawLine(hx + 2, my, hx + 6, my + 3, display::kBright);
    } else {
      c.drawFastHLine(hx - 4, my + 1, 9, display::kBright);
    }
    if (k.sign) {
      // Holding up a "?" placard.
      c.fillRect(x + 44, y + 2, 18, 20, display::kFaint);
      c.drawRect(x + 44, y + 2, 18, 20, display::kBright);
      display::text(x + 50, y + 4, "?", display::kHot, 1, 2);
    }
    if (k.confetti) {
      for (int i = 0; i < 14; ++i) {
        const uint32_t h = (uint32_t)i * 2654435761u;
        const int px = x + 2 + (int)(h % (kW - 4));
        const int py = y + 2 + (int)((h / 64 + t / 40) % (kH - 4));
        c.fillRect(px, py, 2, 2, i % 3 == 0 ? display::kWarn : display::kHot);
      }
    }
  }

  if (label) display::textCentered(x + kW / 2, y + kH + 2, bc::poseName(pose), display::kDim, 1, 1);
}

}  // namespace mascot
