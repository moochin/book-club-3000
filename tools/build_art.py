#!/usr/bin/env python3
"""Converts the mascot PNGs in art/mascot/ into firmware/src/art_data.h.

Runs automatically before every firmware build (firmware/scripts/art.py) and
from sim/build.sh. Standard library only.

    python3 tools/build_art.py            # regenerate the header
    python3 tools/build_art.py --palette  # (re)write art/palette.gpl + .png

File names:  <pose>.png, or <pose>_<frame>.png for animation, with an
optional @<ms> to set how long that frame shows (default 500 ms):

    level1_1@3000.png   level1_2@150.png      -> idle, then a quick blink

Poses: level0 level1 level2 level3 level4 final_week meeting_day no_date syncing
birthday
Each frame is exactly 64 x 96 pixels. Transparent pixels (or the palette's
background colour, index 0) let the screen show through.
"""
import base64
import os
import tempfile
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngio import read_png, write_png  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ART_DIR = os.path.join(ROOT, "art", "mascot")
OUT = os.path.join(ROOT, "firmware", "src", "art_data.h")
W, H = 64, 96
DEFAULT_MS = 500

# Same order as bc::Pose in firmware/lib/bookclub_state/src/bc_state.h.
POSES = ["level0", "level1", "level2", "level3", "level4",
         "final_week", "meeting_day", "no_date", "syncing", "birthday"]

# Keep in step with buildPalette() in firmware/src/display.cpp.
CURVE = [0, 8, 18, 30, 44, 60, 78, 98, 120, 150, 185, 220, 255]
THEMES = {
    "green": dict(accent=(0x3C, 0xFF, 0x6E), warn=(0xFF, 0xB0, 0x28), bg=(0x03, 0x0C, 0x05)),
    "amber": dict(accent=(0xFF, 0xB0, 0x28), warn=(0xFF, 0x50, 0x30), bg=(0x0C, 0x08, 0x02)),
}
NAMES = ["background (transparent)"] + [f"ramp {i}" for i in range(1, 13)] + \
        ["warning dim", "warning", "hot (near white)"]


def _lerp8(a, b, num, den):
    d = (b - a) * num
    q = abs(d) // den
    return a + (q if d >= 0 else -q)  # C integer division truncates toward zero


def palette(theme):
    t = THEMES[theme]
    acc, warn, bg = t["accent"], t["warn"], t["bg"]
    pal = [tuple(_lerp8(bg[c], acc[c], k, 255) for c in range(3)) for k in CURVE]
    pal.append(tuple(v // 2 for v in warn))
    pal.append(warn)
    pal.append(tuple(_lerp8(acc[c], 255, 1, 2) for c in range(3)))
    return pal


def write_palette_files():
    os.makedirs(os.path.dirname(ART_DIR), exist_ok=True)
    pal = palette("green")
    with open(os.path.join(ROOT, "art", "palette.gpl"), "w") as f:
        f.write("GIMP Palette\nName: Book-Club 3000\nColumns: 16\n#\n")
        for (r, g, b), name in zip(pal, NAMES):
            f.write(f"{r:3d} {g:3d} {b:3d}\t{name}\n")
    # A swatch strip: 16 squares of 16 px, green theme on top, amber below.
    rows = []
    for theme in ("green", "amber"):
        p = palette(theme)
        for _ in range(16):
            rows.append([p[i // 16] for i in range(16 * 16)])
    write_png(os.path.join(ROOT, "art", "palette.png"), 256, 32, rows)
    print("wrote art/palette.gpl and art/palette.png")


def parse_name(fname):
    m = re.fullmatch(r"([a-z0-9_]+?)(?:_(\d+))?(?:@(\d+))?\.png", fname)
    if not m or m.group(1) not in POSES:
        # Allow names like "level1_2" where the regex split oddly.
        raise ValueError(
            f"art/mascot/{fname}: name it <pose>.png or <pose>_<n>[@ms].png, "
            f"where <pose> is one of: {' '.join(POSES)}")
    return m.group(1), int(m.group(2) or 1), int(m.group(3) or DEFAULT_MS)


def quantise(path, rows):
    """Maps every pixel to a palette index (0 = transparent)."""
    candidates = []
    for theme in THEMES:
        for i, rgb in enumerate(palette(theme)):
            candidates.append((rgb, i))
    off, out = 0, []
    for row in rows:
        line = []
        for r, g, b, a in row:
            if a < 128:
                line.append(0)
                continue
            best = min(candidates, key=lambda c: (c[0][0] - r) ** 2 + (c[0][1] - g) ** 2 + (c[0][2] - b) ** 2)
            dist = (best[0][0] - r) ** 2 + (best[0][1] - g) ** 2 + (best[0][2] - b) ** 2
            if dist > 3 * 12 ** 2:
                off += 1
            line.append(best[1])
        out.append(line)
    if off:
        print(f"warning: {path}: {off} pixels are not palette colours; mapped to the nearest "
              f"(load art/palette.gpl in your editor to avoid surprises)")
    return out


def pack(indices):
    data = bytearray()
    for row in indices:
        for x in range(0, W, 2):
            data.append((row[x] << 4) | row[x + 1])  # high nibble = left pixel
    return data


def portal_mascot_lines():
    """The Wi-Fi setup page's mascot: level0 (thumbs up) as a small PNG in
    each theme's colours, base64-encoded for an <img src="data:..."> tag."""
    src = os.path.join(ART_DIR, "level0.png")
    out = ["", "// The setup page's mascot (art/mascot/level0.png), as PNG data URIs.",
           "// Empty if that pose hasn't been drawn."]
    if not os.path.exists(src):
        for theme in THEMES:
            out.append(f'static const char kPortalMascot_{theme}[] = "";')
        return out
    w, h, rows = read_png(src)
    idx = quantise("art/mascot/level0.png", rows)
    for theme in THEMES:
        pal = palette(theme)
        rgba = [[(0, 0, 0, 0) if i == 0 else (*pal[i], 255) for i in row] for row in idx]
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            tmp = f.name
        try:
            write_png(tmp, w, h, rgba)
            b64 = base64.b64encode(open(tmp, "rb").read()).decode()
        finally:
            os.remove(tmp)
        out.append(f"static const char kPortalMascot_{theme}[] =")
        for i in range(0, len(b64), 100):
            out.append(f'    "{b64[i:i + 100]}"')
        out[-1] += ";"
    return out


def build():
    frames = {p: [] for p in POSES}
    errors = []
    if os.path.isdir(ART_DIR):
        for fname in sorted(os.listdir(ART_DIR)):
            if not fname.lower().endswith(".png"):
                continue
            try:
                pose, n, ms = parse_name(fname)
                w, h, rows = read_png(os.path.join(ART_DIR, fname))
                if (w, h) != (W, H):
                    raise ValueError(f"art/mascot/{fname}: is {w}x{h}, must be {W}x{H}")
                frames[pose].append((n, ms, fname, pack(quantise(f"art/mascot/{fname}", rows))))
            except Exception as e:  # report every bad file, then fail
                errors.append(str(e))
    if errors:
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    lines = [
        "// GENERATED by tools/build_art.py from art/mascot/*.png. Do not edit;",
        "// change the PNGs instead. Poses without art fall back to the",
        "// placeholder figure in mascot.cpp.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace art {",
        "",
        f"constexpr int kW = {W};",
        f"constexpr int kH = {H};",
        "",
        "struct Frame {",
        "  const uint8_t* px;  // 4 bpp palette indices, high nibble = left pixel, 0 = transparent",
        "  uint16_t ms;",
        "};",
        "struct Anim {",
        "  const Frame* frames;",
        "  uint8_t count;",
        "};",
        "",
    ]
    anims = []
    for pose in POSES:
        fs = sorted(frames[pose])
        if not fs:
            anims.append("{nullptr, 0}")
            continue
        for n, ms, fname, data in fs:
            lines.append(f"// {fname}")
            lines.append(f"static const uint8_t k_{pose}_{n}[{len(data)}] = {{")
            for i in range(0, len(data), 24):
                lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
            lines.append("};")
        lines.append(f"static const Frame k_{pose}[] = {{" +
                     ", ".join(f"{{k_{pose}_{n}, {ms}}}" for n, ms, _, _ in fs) + "};")
        lines.append("")
        anims.append(f"{{k_{pose}, {len(fs)}}}")
    lines.append("// Indexed by bc::Pose.")
    lines.append(f"static const Anim kAnims[{len(POSES)}] = {{")
    for pose, a in zip(POSES, anims):
        lines.append(f"    {a},  // {pose}")
    lines.append("};")
    lines.append("")
    lines += portal_mascot_lines()
    lines.append("}  // namespace art")
    text = "\n".join(lines) + "\n"
    old = open(OUT).read() if os.path.exists(OUT) else None
    if text != old:
        with open(OUT, "w") as f:
            f.write(text)
    have = [p for p in POSES if frames[p]]
    missing = [p for p in POSES if not frames[p]]
    print(f"art: {len(have)}/{len(POSES)} poses drawn" +
          (f"; placeholders for: {' '.join(missing)}" if missing else ""))


if __name__ == "__main__":
    if "--palette" in sys.argv:
        write_palette_files()
    else:
        build()
