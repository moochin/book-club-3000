#!/usr/bin/env python3
"""Builds the mascot sprites in art/mascot/ from the drawings in art/src/drawings/.

    python3 art/src/make_sprites.py        (needs Pillow)

Each drawing is full-colour line art on a white background. This script:

1. converts it to a 64 x 96 sprite in the screen's 16-shade palette:
   removes the background, keeps the black ink as outlines, maps colours to
   brightness, and adds the floor shadow;
2. adds the small extras that don't survive the shrink: the watch face, the
   question mark, confetti, the jump's bounce;
3. applies hand touch-ups: clean faces for the two sickest poses and a
   deeper slump for "very sick".

It overwrites art/mascot/*.png. Tweak the settings in JOBS and re-run; or
hand-edit the PNGs in art/mascot/ and stop running this script.
"""
import colorsys
import os
import random
import sys
from collections import deque

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
from build_art import palette  # noqa: E402

DRAWINGS = os.path.join(HERE, "drawings")
OUT = os.path.join(HERE, "..", "mascot")

# Palette indices (see art/README.md).
CLEAR, INK, SKIN_SH, SKIN, WDIM, WARN, HOT = 0, 2, 10, 11, 13, 14, 15


# ---- Conversion --------------------------------------------------------------

def convert(src, k=5, ink_thresh=120, height=90, lift=0, boost=0, edge=False,
            sick=False, sick_sat=0.18, sick_col=WDIM, beer=False, confetti=0):
    """Returns a 96 x 64 grid of palette indices.

    k: how much the ink lines are thickened before shrinking (odd, 1 = none).
    ink_thresh: how much ink a pixel needs to become outline (0-255).
    height: figure height in pixels; wide poses shrink to fit 62 px across.
    lift: raise the figure off the floor (jumps).
    boost: brighten the fills (for dark, grimy drawings).
    edge: redraw a clean outline round the silhouette (for textured drawings).
    sick: turn green-tinged skin into a warning-colour speckle.
    beer: turn amber liquid into the warning colour.
    confetti: random seed for confetti dots (0 = none).
    """
    im = Image.open(src).convert("RGB")
    alpha = Image.new("L", im.size, 255)
    dark = Image.new("L", im.size, 0)
    px, ap, dp = im.load(), alpha.load(), dark.load()
    w0, h0 = im.size

    # Background = white connected to the edge, so whites inside the figure
    # (eyes, teeth, beer foam) stay part of him.
    q = deque((x, y) for x in range(w0) for y in (0, h0 - 1))
    q.extend((x, y) for y in range(h0) for x in (0, w0 - 1))
    while q:
        x, y = q.popleft()
        if ap[x, y] == 0 or min(px[x, y]) <= 232:
            continue
        ap[x, y] = 0
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w0 and 0 <= ny < h0 and ap[nx, ny]:
                q.append((nx, ny))
    for y in range(h0):
        for x in range(w0):
            if ap[x, y] and max(px[x, y]) < 77:
                dp[x, y] = 255

    box = alpha.getbbox()
    im, alpha, dark = im.crop(box), alpha.crop(box), dark.crop(box)
    th = height
    tw = round(im.width * th / im.height)
    if tw > 62:
        tw = 62
        th = round(im.height * tw / im.width)
    ink = (dark.filter(ImageFilter.MaxFilter(k)) if k > 1 else dark).resize((tw, th), Image.BOX)
    col = im.resize((tw, th), Image.BOX)
    a_s = alpha.resize((tw, th), Image.BOX)

    idx = [[CLEAR] * 64 for _ in range(96)]
    ox, oy = (64 - tw) // 2, 96 - th - 4 - lift
    for y in range(th):
        for x in range(tw):
            if a_s.getpixel((x, y)) < 100:
                continue
            if ink.getpixel((x, y)) > ink_thresh:
                c = INK
            else:
                r, g, b = col.getpixel((x, y))
                h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                # Brightness from value, lifted for saturated colours so the
                # red hoodie and blue jeans glow instead of going black.
                c = max(6, min(12, round(5 + boost + (0.25 + 0.55 * v + 0.25 * s * v) * 7)))
                if sick and 0.17 < h < 0.40 and s > sick_sat and v > 0.45 and (x + y) % 2 == 0:
                    c = sick_col
                if beer and 0.04 < h < 0.15 and s > 0.45 and v > 0.45:
                    c = WARN
            idx[oy + y][ox + x] = c

    if edge:
        src_idx = [r[:] for r in idx]
        for y in range(96):
            for x in range(64):
                if src_idx[y][x] and any(not (0 <= x + dx < 64 and 0 <= y + dy < 96) or src_idx[y + dy][x + dx] == 0
                                         for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    idx[y][x] = INK
        for y in range(96):
            for x in range(64):
                if idx[y][x] and all(not (0 <= x + dx < 64 and 0 <= y + dy < 96) or idx[y + dy][x + dx] == 0
                                     for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    idx[y][x] = CLEAR

    # Floor shadow.
    for y in range(88, 95):
        for x in range(64):
            if ((x - 32) / 21) ** 2 + ((y - 91.5) / 3.5) ** 2 <= 1 and idx[y][x] == CLEAR:
                idx[y][x] = INK

    if confetti:
        rnd = random.Random(confetti)
        for _ in range(18):
            x, y = rnd.randint(1, 61), rnd.randint(1, 80)
            if idx[y][x] == CLEAR and idx[y][x + 1] == CLEAR:
                c = rnd.choice((HOT, WARN, 12))
                idx[y][x] = idx[y][x + 1] = c
    return idx


# ---- Extras and touch-ups ----------------------------------------------------

def put(f, pts, c):
    for x, y in pts:
        if 0 <= x < 64 and 0 <= y < 96:
            f[y][x] = c


def watch(f, sx, sy, hand):
    """A bright watch face on the wrist; `hand` picks the minute hand's pixel."""
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            if abs(dx) + abs(dy) <= 3:
                f[sy + dy][sx + dx] = INK if (abs(dx) == 2 or abs(dy) == 2) else HOT
    f[sy][sx] = INK
    f[sy + hand[1]][sx + hand[0]] = INK


QUESTION = ["..####..",
            ".######.",
            "##....##",
            "......##",
            ".....##.",
            "....##..",
            "...##...",
            "...##...",
            "........",
            "...##...",
            "...##..."]


def question_mark(f, x0, y0):
    pts = {(x0 + i, y0 + j) for j, row in enumerate(QUESTION) for i, ch in enumerate(row) if ch == "#"}
    for x, y in pts:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < 64 and 0 <= ny < 96 and (nx, ny) not in pts and f[ny][nx] == CLEAR:
                    f[ny][nx] = INK
    put(f, pts, HOT)


def hat_stripes(f, rows=(8, 11, 14), x0=31, x1=40):
    """Bright bands across the party hat, lost when the drawing is shrunk."""
    for y in rows:
        for x in range(x0, x1 + 1):
            if f[y][x] in (10, 11, 12):
                f[y][x] = HOT


def repaint_face(f, cx, cy, rx, ry):
    """Smooth skin inside the face oval, with a one-pixel ink rim."""
    for y in range(96):
        for x in range(64):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d <= 1.0:
                f[y][x] = SKIN_SH if x > cx + rx * 0.45 else SKIN
            elif d <= 1.35 and f[y][x] != CLEAR:
                f[y][x] = INK


def sick_face(f, cx, cy):
    put(f, [(cx - 5, cy - 4), (cx - 4, cy - 4), (cx - 3, cy - 5)], INK)              # worried brows
    put(f, [(cx + 2, cy - 5), (cx + 3, cy - 4), (cx + 4, cy - 4)], INK)
    put(f, [(cx - 4, cy - 2), (cx - 3, cy - 2), (cx + 2, cy - 2), (cx + 3, cy - 2)], INK)  # heavy lids
    put(f, [(cx - 4, cy - 1), (cx + 3, cy - 1)], INK)                                 # pupils
    put(f, [(cx - 3, cy + 4), (cx - 2, cy + 3), (cx - 1, cy + 4), (cx, cy + 3),
            (cx + 1, cy + 4), (cx + 2, cy + 3)], INK)                                 # wobbly mouth
    put(f, [(cx - 5, cy + 1), (cx - 4, cy + 2), (cx + 4, cy + 1), (cx + 5, cy + 2),
            (cx - 1, cy - 6), (cx + 1, cy - 6)], WDIM)                                # sickly tinge
    put(f, [(cx + 6, cy - 3), (cx + 6, cy - 2), (cx + 7, cy - 2)], HOT)               # sweat drop


def ghoul_face(f, cx, cy, sore):
    for ex, ey in ((cx - 3, cy - 2), (cx + 2, cy - 1)):                               # sunken eyes
        put(f, [(ex - 1, ey), (ex, ey), (ex + 1, ey), (ex - 1, ey + 1), (ex + 1, ey + 1), (ex, ey - 1)], INK)
        put(f, [(ex, ey + 1)], HOT)
    put(f, [(x, y) for x in range(cx - 2, cx + 2) for y in range(cy + 3, cy + 6)], INK)  # gaping mouth
    put(f, [(cx - 1, cy + 3)], HOT)                                                   # one tooth
    put(f, [(cx + 1, cy + 6), (cx + 1, cy + 7)], SKIN)                                # drool
    put(f, [(cx - 5, cy), (cx - 4, cy + 1), (cx - 5, cy + 2), (cx + 4, cy - 4), (cx + 5, cy - 3),
            (cx + 3, cy + 3), (cx + 4, cy + 4)], sore)                                # sores


def slump(f, dy=2, neck=28):
    """Sinks the head into the shoulders."""
    head = [row[:] for row in f[:neck + 1]]
    for y in range(dy):
        f[y] = [CLEAR] * 64
    for i, row in enumerate(head):
        y = dy + i
        for x in range(64):
            if row[x] != CLEAR:
                f[y][x] = row[x]
            elif y < len(head):
                f[y][x] = CLEAR


# ---- The poses -----------------------------------------------------------------

def d(name):
    return os.path.join(DRAWINGS, name)


def build():
    out = {}
    out["level0.png"] = convert(d("well_rested.png"))
    out["level1.png"] = convert(d("healthy.png"))

    f = convert(d("mildly_sick.webp"), sick=True)
    repaint_face(f, 31, 21, 6.5, 7.5)
    sick_face(f, 31, 21)
    out["level2.png"] = f

    f = convert(d("very_sick.webp"), sick=True)
    slump(f)
    out["level3.png"] = f

    crit = dict(k=5, ink_thresh=170, boost=1, edge=True, sick=True, sick_sat=0.06)
    for name, sore, ms in (("level4_1", WDIM, 700), ("level4_2", WARN, 300)):
        f = convert(d("critical.jpg"), sick_col=sore, **crit)
        repaint_face(f, 27, 20, 6, 7)
        ghoul_face(f, 27, 20, sore)
        out[f"{name}@{ms}.png"] = f

    out["final_week_1@250.png"] = convert(d("excited.webp"), height=84, lift=5)
    out["final_week_2@250.png"] = convert(d("excited.webp"), height=84, lift=1)

    out["meeting_day_1@300.png"] = convert(d("party.png"), beer=True, confetti=1)
    out["meeting_day_2@300.png"] = convert(d("party.png"), beer=True, confetti=2)

    for i, dy in ((1, 0), (2, 2)):
        f = convert(d("no_plans.jpg"))
        question_mark(f, 52, 1 + dy)
        out[f"no_date_{i}@1000.png"] = f

    for i, seed in ((1, 3), (2, 4)):
        f = convert(d("birthday.png"), beer=True, confetti=seed)
        hat_stripes(f)
        out[f"birthday_{i}@400.png"] = f

    for i, hand in ((1, (0, -1)), (2, (1, 0))):
        f = convert(d("waiting.jpg"))
        watch(f, 31, 37, hand)
        out[f"syncing_{i}@600.png"] = f
    return out


def save(f, path):
    pal = palette("green")
    img = Image.new("P", (64, 96))
    img.putpalette([v for c in pal for v in c] + [0] * (768 - 48))
    img.putdata([c for row in f for c in row])
    img.save(path, transparency=0)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    sprites = build()
    for n in os.listdir(OUT):
        if n.endswith(".png"):
            os.remove(os.path.join(OUT, n))
    for name, f in sprites.items():
        save(f, os.path.join(OUT, name))
        print("wrote art/mascot/" + name)
