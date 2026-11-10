#!/usr/bin/env python3
"""Turns the simulator's screens and the mascot PNGs into preview sheets.

    python3 tools/art_preview.py         # after art/preview.sh has run the sim
    python3 tools/art_preview.py --templates DIR   # PPM templates -> art/templates/*.png

Writes art/preview/screens.png (every STAT state, as on the device) and
art/preview/poses.png (each pose's frames at 3x, green and amber themes).
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_art import POSES, W, H, palette, parse_name  # noqa: E402
from pngio import read_png, read_ppm, write_png  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SIM_OUT = os.path.join(ROOT, "firmware", "sim", "out")
PREVIEW = os.path.join(ROOT, "art", "preview")
GAP = (24, 24, 24)


def scale(rows, k):
    out = []
    for row in rows:
        line = [p for p in row for _ in range(k)]
        out.extend([line] * k)
    return out


def grid(images, cols, gap=8):
    """images: list of rows-lists, all the same size."""
    h, w = len(images[0]), len(images[0][0])
    n_rows = (len(images) + cols - 1) // cols
    W_, H_ = cols * (w + gap) + gap, n_rows * (h + gap) + gap
    canvas = [[GAP] * W_ for _ in range(H_)]
    for i, img in enumerate(images):
        ox, oy = gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)
        for y in range(h):
            canvas[oy + y][ox:ox + w] = [p[:3] for p in img[y]]
    return W_, H_, canvas


def screens():
    files = sorted(glob.glob(os.path.join(SIM_OUT, "stat_*.ppm")))
    files += [os.path.join(SIM_OUT, n + ".ppm") for n in ("amber_critical_fx", "amber_meeting_day")]
    imgs = [read_ppm(f)[2] for f in files if os.path.exists(f)]
    if not imgs:
        print("no simulator output; run art/preview.sh")
        return
    w, h, rows = grid([scale(i, 2) for i in imgs], 3)
    write_png(os.path.join(PREVIEW, "screens.png"), w, h, rows)
    print("wrote art/preview/screens.png")


def recolour(rows, src, dst):
    """Maps pixels from the green palette to the amber one (nearest index)."""
    out = []
    for row in rows:
        line = []
        for r, g, b, a in row:
            if a < 128:
                line.append(dst[0])
                continue
            i = min(range(16), key=lambda k: sum((src[k][c] - (r, g, b)[c]) ** 2 for c in range(3)))
            line.append(dst[i])
        out.append(line)
    return out


def poses():
    green, amber = palette("green"), palette("amber")
    tiles = []
    art_dir = os.path.join(ROOT, "art", "mascot")
    for f in sorted(glob.glob(os.path.join(art_dir, "*.png"))):
        try:
            parse_name(os.path.basename(f))
            w, h, rows = read_png(f)
        except Exception as e:
            print(f"skipping {f}: {e}")
            continue
        if (w, h) != (W, H):
            continue
        on_green = [[green[0] if p[3] < 128 else p[:3] for p in row] for row in rows]
        tiles.append(scale(on_green, 3))
        tiles.append(scale(recolour(rows, green, amber), 3))
    if not tiles:
        print("no art in art/mascot yet")
        return
    w, h, rows = grid(tiles, 6)
    write_png(os.path.join(PREVIEW, "poses.png"), w, h, rows)
    print("wrote art/preview/poses.png")


def templates(src_dir):
    out_dir = os.path.join(ROOT, "art", "templates")
    os.makedirs(out_dir, exist_ok=True)
    bg = palette("green")[0]
    for pose in POSES:
        p = os.path.join(src_dir, pose + ".ppm")
        if not os.path.exists(p):
            continue
        w, h, rows = read_ppm(p)
        rgba = [[(0, 0, 0, 0) if px == bg else (*px, 255) for px in row] for row in rows]
        write_png(os.path.join(out_dir, pose + ".png"), w, h, rgba)
    print("wrote art/templates/*.png")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--templates":
        templates(sys.argv[2])
    else:
        os.makedirs(PREVIEW, exist_ok=True)
        screens()
        poses()
