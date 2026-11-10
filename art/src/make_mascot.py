#!/usr/bin/env python3
"""Generates the mascot, "Patch", as 64 x 96 PNGs in art/mascot/.

Patch is an original character: a gamer in a pom-pom beanie and a hoodie,
with headphones round the neck. Drawn in a bold, filled cartoon style:
bright shapes, dark outlines, a soft rim glow and a floor shadow.

    python3 art/src/make_mascot.py        (needs Pillow)

This overwrites art/mascot/*.png. If you start hand-editing those PNGs,
stop running this script (or delete the poses you edit from POSES below).
Colours are palette indices (see art/README.md): 0 transparent, 1-12 the
theme ramp, 13/14 warning dim/bright, 15 near-white.
"""
import math
import os
import random

from PIL import Image, ImageDraw

W, H = 64, 96
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mascot")

# Palette indices.
CLEAR, SHADOW, LINE, GLOW = 0, 2, 2, 4
FILL, FILL_DARK, FILL_DEEP, BRIGHT = 10, 8, 6, 12
WARN_DIM, WARN, HOT = 13, 14, 15


class Canvas:
    def __init__(self):
        self.px = [[CLEAR] * W for _ in range(H)]

    def mask(self, draw_fn):
        m = Image.new("L", (W, H), 0)
        draw_fn(ImageDraw.Draw(m))
        return [[m.getpixel((x, y)) > 127 for x in range(W)] for y in range(H)]

    def part(self, draw_fn, fill=FILL, shade=True, outline=LINE):
        """Fills a shape, shades its right edge, and outlines it."""
        m = self.mask(draw_fn)
        xs = [x for y in range(H) for x in range(W) if m[y][x]]
        if not xs:
            return m
        x0, x1 = min(xs), max(xs)
        cut = x1 - (x1 - x0) * 0.28
        for y in range(H):
            for x in range(W):
                if not m[y][x]:
                    continue
                edge = any(not (0 <= x + dx < W and 0 <= y + dy < H and m[y + dy][x + dx])
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if edge and outline is not None:
                    self.px[y][x] = outline
                elif shade and x > cut and fill not in (HOT, WARN, WARN_DIM):
                    self.px[y][x] = max(fill - 2, 1)
                else:
                    self.px[y][x] = fill
        return m

    def dot(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c

    def line(self, pts, c, width=1):
        m = self.mask(lambda d: d.line(pts, fill=255, width=width))
        for y in range(H):
            for x in range(W):
                if m[y][x]:
                    self.px[y][x] = c

    def glow(self):
        """A one-pixel dim rim outside the silhouette, like phosphor bleed."""
        src = [row[:] for row in self.px]
        for y in range(H):
            for x in range(W):
                if src[y][x] != CLEAR:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and src[ny][nx] not in (CLEAR, SHADOW):
                        self.px[y][x] = GLOW if src[ny][nx] == LINE else self.px[y][x]
                        break

    def save(self, name):
        img = Image.new("P", (W, H))
        # Write with the real palette so the PNG looks right in an editor.
        import sys
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
        from build_art import palette
        pal = palette("green")
        img.putpalette([v for rgb in pal for v in rgb] + [0] * (768 - 48))
        img.putdata([c for row in self.px for c in row])
        img.save(os.path.join(OUT, name), transparency=0)


# ---- The figure -------------------------------------------------------------


def arm(c, shoulder, hand, fist=True, thumb=False, behind=False):
    sx, sy = shoulder
    hx, hy = hand
    c.part(lambda d: d.line([sx, sy, hx, hy], fill=255, width=7), fill=FILL_DARK if behind else FILL)
    c.part(lambda d: d.ellipse([hx - 4, hy - 4, hx + 4, hy + 4], fill=255), shade=False)
    if thumb:
        c.part(lambda d: d.rounded_rectangle([hx - 1, hy - 10, hx + 3, hy - 2], 2, fill=255), shade=False)


def draw_patch(pose, frame=0):
    c = Canvas()
    rnd = random.Random(pose + str(frame))

    # Posture.
    jump = 0        # vertical offset for jumps (the shadow stays put)
    head_dx = head_dy = 0
    slump = pose in ("level3", "level4")
    if slump:
        head_dx, head_dy = 3, 5
    if pose == "final_week" and frame == 1:
        jump = -2
    if pose == "meeting_day" and frame == 1:
        jump = -1
    dy = 7 + jump   # upper body sits lower so the beanie fits in the box

    # Floor shadow (stays put when jumping, so the jump reads).
    c.part(lambda d: d.ellipse([12, 86, 52, 94], fill=255), fill=SHADOW, shade=False, outline=None)

    # Legs and trainers.
    knee = 2 if slump else 0
    c.part(lambda d: d.rounded_rectangle([21, 64 + jump, 31, 85 + jump], 3, fill=255), fill=FILL_DARK)
    c.part(lambda d: d.rounded_rectangle([33 + knee, 64 + jump, 43 + knee, 85 + jump], 3, fill=255), fill=FILL_DARK)
    for x0 in (17, 33 + knee):
        c.part(lambda d, x0=x0: d.rounded_rectangle([x0, 81 + jump, x0 + 14, 88 + jump], 3, fill=255), fill=FILL)
        c.line([(x0 + 2, 86 + jump), (x0 + 12, 86 + jump)], BRIGHT)  # sole stripe

    # Arms behind the body come first.
    sl, sr = (19, 43 + dy), (44, 43 + dy)
    arms = {
        "level0": dict(l=((15, 58 + dy), False), r=((55, 30 + dy), True)),         # thumbs up
        "level1": dict(l=((14, 62 + dy), False), r=((50, 62 + dy), False)),
        "level2": dict(l=((26, 55 + dy), False), r=((49, 62 + dy), False)),         # hand on tummy
        "level3": dict(l=((15, 64 + dy), False), r=((50, 66 + dy), False)),         # hanging
        "level4": dict(l=((8, 50 + dy), False), r=((52, 66 + dy), False)),          # reaching
        "final_week": dict(l=((8, 18 + dy), False), r=((56, 18 + dy), False)),      # arms up
        "meeting_day": dict(l=((8, 22 + dy), False), r=((54, 16 + dy), False)),
        "no_date": dict(l=((7, 46 + dy), False), r=((57, 46 + dy), False)),         # shrug
        "syncing": dict(l=((30, 50 + dy), False), r=((47, 62 + dy), False)),        # checking watch
    }[pose]

    # Hoodie body.
    c.part(lambda d: d.rounded_rectangle([17, 38 + dy, 46, 63 + dy], 7, fill=255))
    c.line([(24, 54 + dy), (39, 54 + dy)], LINE)          # kangaroo pocket
    c.line([(24, 54 + dy), (22, 61 + dy)], LINE)
    c.line([(39, 54 + dy), (41, 61 + dy)], LINE)
    c.line([(28, 42 + dy), (28, 49 + dy)], BRIGHT)         # drawstrings
    c.line([(35, 42 + dy), (35, 49 + dy)], BRIGHT)
    if pose == "level4":  # torn hem and a sickly patch
        for x in range(18, 46, 4):
            c.dot(x, 63 + dy, CLEAR)
            c.dot(x + 1, 62 + dy, LINE)
        c.part(lambda d: d.ellipse([36, 50 + dy, 42, 55 + dy], fill=255), fill=WARN_DIM, shade=False)

    (lh, lt), (rh, rt) = arms["l"], arms["r"]
    arm(c, sl, lh, thumb=lt)
    arm(c, sr, rh, thumb=rt)

    # Props.
    if pose == "meeting_day":  # a pint held high
        gx, gy = 50, 2 + dy
        c.part(lambda d: d.rectangle([gx, gy, gx + 8, gy + 12], fill=255), fill=BRIGHT, shade=False)
        c.part(lambda d: d.ellipse([gx - 1, gy - 3, gx + 9, gy + 2], fill=255), fill=HOT, shade=False)
        c.line([(gx + 6, gy + 4), (gx + 6, gy + 9)], HOT)
    if pose == "syncing":  # wristwatch on the raised left wrist
        c.part(lambda d: d.ellipse([27, 46 + dy, 33, 52 + dy], fill=255), fill=HOT, shade=False)
        c.dot(30, 49 + dy, LINE)
        c.dot(30 + (1 if frame else 0), 48 + dy - (0 if frame else 1), LINE)
    if pose == "no_date":  # a floating question mark
        qy = 2 + (1 if frame else 0)
        c.line([(52, qy + 2), (54, qy), (57, qy), (59, qy + 2), (59, qy + 4), (56, qy + 7), (56, qy + 9)], BRIGHT, 2)
        c.part(lambda d: d.rectangle([55, qy + 11, 57, qy + 13], fill=255), fill=BRIGHT, shade=False, outline=None)

    # Headphones round the neck.
    hx0, hy0 = 31 + head_dx // 2, 37 + dy + head_dy // 2
    c.part(lambda d: d.rounded_rectangle([hx0 - 10, hy0 - 1, hx0 + 10, hy0 + 3], 2, fill=255), fill=FILL_DEEP)
    for cx in (hx0 - 9, hx0 + 9):
        c.part(lambda d, cx=cx: d.rounded_rectangle([cx - 3, hy0 - 2, cx + 3, hy0 + 4], 2, fill=255),
               fill=FILL_DEEP, shade=False)

    # Head.
    hx, hy = 32 + head_dx, 22 + dy + head_dy
    c.part(lambda d: d.ellipse([hx - 15, hy - 14, hx + 15, hy + 15], fill=255))
    c.part(lambda d: d.ellipse([hx - 18, hy - 2, hx - 12, hy + 6], fill=255), shade=False)  # ears
    c.part(lambda d: d.ellipse([hx + 12, hy - 2, hx + 18, hy + 6], fill=255))

    # Beanie with a fold and a pom-pom.
    c.part(lambda d: d.chord([hx - 15, hy - 24, hx + 15, hy + 2], 180, 360, fill=255), fill=FILL_DARK)
    c.part(lambda d: d.rounded_rectangle([hx - 16, hy - 12, hx + 16, hy - 6], 2, fill=255), fill=FILL)
    for x in range(hx - 13, hx + 14, 3):  # knit ribs
        c.dot(x, hy - 10, FILL_DEEP)
        c.dot(x, hy - 8, FILL_DEEP)
    c.part(lambda d: d.ellipse([hx - 3, hy - 27, hx + 3, hy - 21], fill=255), fill=BRIGHT, shade=False)

    # Face.
    ex, ey = hx, hy + 1
    blink = pose == "level1" and frame == 1
    sick = pose in ("level2", "level4")
    if pose in ("final_week", "meeting_day"):  # star eyes / happy arcs
        for sx in (ex - 6, ex + 6):
            if pose == "final_week":
                c.line([(sx - 2, ey), (sx + 2, ey)], HOT)
                c.line([(sx, ey - 2), (sx, ey + 2)], HOT)
                c.dot(sx, ey, LINE)
            else:
                c.line([(sx - 3, ey + 1), (sx - 1, ey - 1), (sx + 1, ey - 1), (sx + 3, ey + 1)], LINE)
    elif blink or pose == "level3":
        for sx in (ex - 6, ex + 6):
            c.line([(sx - 3, ey), (sx + 3, ey)], LINE)
            if pose == "level3":  # heavy lids
                c.line([(sx - 3, ey + 1), (sx + 2, ey + 1)], FILL_DEEP)
    else:
        for i, sx in enumerate((ex - 6, ex + 6)):
            if pose == "level4" and i == 1:  # crossed-out eye
                c.line([(sx - 2, ey - 2), (sx + 2, ey + 2)], LINE)
                c.line([(sx - 2, ey + 2), (sx + 2, ey - 2)], LINE)
                continue
            look = (1 if frame else -1) if pose == "no_date" else 0
            c.part(lambda d, sx=sx: d.ellipse([sx - 2, ey - 3, sx + 2, ey + 3], fill=255), fill=LINE, shade=False, outline=None)
            c.dot(sx + look, ey - 1, HOT)
    # Eyebrows.
    if pose == "no_date":
        c.line([(ex - 9, ey - 6), (ex - 4, ey - 7)], LINE)
        c.line([(ex + 4, ey - 7), (ex + 9, ey - 5)], LINE)
    if sick:
        c.line([(ex - 9, ey - 5), (ex - 4, ey - 6)], LINE)
        c.line([(ex + 4, ey - 6), (ex + 9, ey - 5)], LINE)

    # Mouth.
    my = hy + 8
    if pose in ("level0", "final_week", "meeting_day"):  # big open grin
        c.part(lambda d: d.chord([hx - 7, my - 4, hx + 7, my + 6], 0, 180, fill=255), fill=LINE, shade=False, outline=None)
        c.line([(hx - 5, my + 1), (hx + 5, my + 1)], HOT)  # teeth
    elif pose == "level1":
        c.line([(hx - 5, my), (hx - 2, my + 2), (hx + 2, my + 2), (hx + 5, my)], LINE)
    elif sick:
        for x in range(hx - 6, hx + 7):
            c.dot(x, my + (1 if (x // 2) % 2 else 0), LINE)
    elif pose == "level3":
        c.line([(hx - 5, my + 2), (hx - 2, my), (hx + 2, my), (hx + 5, my + 2)], LINE)
    elif pose == "no_date":
        c.line([(hx - 4, my + 1), (hx + 4, my)], LINE)
    else:  # syncing
        c.line([(hx - 3, my + 1), (hx + 3, my + 1)], LINE)

    # Sickness details.
    if pose == "level2":
        for _ in range(9):  # queasy blotches
            x, y = hx + rnd.randint(-11, 11), hy + rnd.randint(-3, 11)
            if c.px[y][x] in (FILL, FILL_DARK):
                c.dot(x, y, WARN_DIM)
        c.part(lambda d: d.ellipse([hx + 11, hy - 6, hx + 14, hy - 1], fill=255), fill=HOT, shade=False)  # sweat
    if pose == "level4":
        for (px, py, r) in ((hx - 8, hy + 5, 3), (hx + 7, hy - 3, 2), (hx - 2, hy + 12, 2)):
            c.part(lambda d, px=px, py=py, r=r: d.ellipse([px - r, py - r, px + r, py + r], fill=255),
                   fill=WARN if frame else WARN_DIM, shade=False, outline=None)
    if pose == "level3":  # sweat drop
        c.part(lambda d: d.ellipse([hx - 15, hy - 3, hx - 12, hy + 2], fill=255), fill=HOT, shade=False)

    c.glow()

    # Confetti on meeting day, different each frame.
    if pose == "meeting_day":
        for _ in range(16):
            x, y = rnd.randint(1, W - 3), rnd.randint(1, 80)
            if c.px[y][x] == CLEAR:
                col = rnd.choice((HOT, WARN, BRIGHT))
                c.dot(x, y, col)
                c.dot(x + 1, y, col)
    return c


# pose: list of (frame suffix, ms)
POSES = {
    "level0": [("", None)],
    "level1": [("_1", 3000), ("_2", 150)],
    "level2": [("", None)],
    "level3": [("", None)],
    "level4": [("_1", 700), ("_2", 300)],
    "final_week": [("_1", 250), ("_2", 250)],
    "meeting_day": [("_1", 300), ("_2", 300)],
    "no_date": [("_1", 1500), ("_2", 1500)],
    "syncing": [("_1", 600), ("_2", 600)],
}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".png"):
            os.remove(os.path.join(OUT, f))
    for pose, frames in POSES.items():
        for i, (suffix, ms) in enumerate(frames):
            name = f"{pose}{suffix}" + (f"@{ms}" if ms else "") + ".png"
            draw_patch(pose, i).save(name)
            print("wrote art/mascot/" + name)
