
#!/usr/bin/env python3
"""Generates a Vault Boy mascot as 64 x 96 PNGs in art/mascot/.

This script has been completely rewritten from the ground up to accurately 
generate the proportions, hair, and suit of the classic Fallout Vault Boy 
in a flat, retro terminal style. 

    python3 art/src/make_mascot.py
"""
import math
import os
import random

from PIL import Image, ImageDraw

W, H = 64, 96
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mascot")

# Palette indices mapping to the green terminal theme.
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
        m = self.mask(lambda d: d.line(pts, fill=255, width=width, joint="curve"))
        for y in range(H):
            for x in range(W):
                if m[y][x]:
                    self.px[y][x] = c

    def glow(self):
        """A one-pixel dim rim outside the silhouette."""
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
        try:
            import sys
            sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
            from build_art import palette
            pal = palette("green")
        except ImportError:
            # Standalone fallback green palette if build_art is missing
            pal = [(0,0,0), (0,20,0), (0,40,0), (0,60,0), (0,80,0), (0,100,0), 
                   (0,120,0), (0,140,0), (0,160,0), (0,180,0), (0,200,0), (0,220,0), 
                   (30,255,30), (100,255,100), (200,255,200), (255,255,255)]
            
        img.putpalette([v for rgb in pal for v in rgb] + [0] * (768 - 48))
        img.putdata([c for row in self.px for c in row])
        img.save(os.path.join(OUT, name), transparency=0)


# ---- Character Construction -------------------------------------------------

def draw_limb(c, start, end, thickness=6, fill=FILL, is_skin=False):
    """Draws a tapered limb, handling skin vs suit shading."""
    sx, sy = start
    ex, ey = end
    actual_fill = BRIGHT if is_skin else fill
    shade_flag = not is_skin
    c.part(lambda d: d.line([sx, sy, ex, ey], fill=255, width=thickness), fill=actual_fill, shade=shade_flag)
    c.part(lambda d: d.ellipse([ex - thickness//2 + 1, ey - thickness//2 + 1, 
                                ex + thickness//2 - 1, ey + thickness//2 - 1], fill=255), 
           fill=actual_fill, shade=shade_flag)

def draw_vault_boy(pose, frame=0):
    c = Canvas()
    rnd = random.Random(pose + str(frame))

    # Core offsets for animation
    jump = 0        
    head_dx = head_dy = 0
    slump = pose in ("level3", "level4")
    if slump:
        head_dx, head_dy = 2, 4
    if pose == "final_week" and frame == 1:
        jump = -2
    if pose == "meeting_day" and frame == 1:
        jump = -1
    dy = 8 + jump   

    # Floor shadow (static during jumps)
    c.part(lambda d: d.ellipse([14, 88, 50, 94], fill=255), fill=SHADOW, shade=False, outline=None)

    # --- Legs & Boots ---
    knee = 2 if slump else 0
    # Left leg
    c.part(lambda d: d.polygon([(23, 62 + jump), (29, 62 + jump), (30, 84 + jump), (25, 84 + jump)], fill=255), fill=FILL)
    # Right leg
    c.part(lambda d: d.polygon([(35, 62 + jump), (41, 62 + jump), (39, 84 + jump), (34, 84 + jump)], fill=255), fill=FILL)
    
    # Chunky Vault Boots
    c.part(lambda d: d.rounded_rectangle([20, 83 + jump, 31, 90 + jump], 2, fill=255), fill=FILL_DARK)
    c.part(lambda d: d.rounded_rectangle([33 + knee, 83 + jump, 44 + knee, 90 + jump], 2, fill=255), fill=FILL_DARK)

    # --- Arms Logic ---
    sl, sr = (20, 42 + dy), (44, 42 + dy)
    arms = {
        "level0": dict(l=((13, 58 + dy), False), r=((50, 32 + dy), True)),  # Thumbs up        
        "level1": dict(l=((15, 62 + dy), False), r=((49, 62 + dy), False)),
        "level2": dict(l=((26, 55 + dy), False), r=((49, 62 + dy), False)),         
        "level3": dict(l=((15, 64 + dy), False), r=((50, 66 + dy), False)),         
        "level4": dict(l=((10, 52 + dy), False), r=((52, 66 + dy), False)),          
        "final_week": dict(l=((10, 20 + dy), False), r=((54, 20 + dy), False)),      
        "meeting_day": dict(l=((10, 24 + dy), False), r=((52, 18 + dy), False)),
        "no_date": dict(l=((9, 48 + dy), False), r=((55, 48 + dy), False)),         
        "syncing": dict(l=((30, 50 + dy), False), r=((47, 62 + dy), False)),        
    }[pose]

    (lh, lt), (rh, rt) = arms["l"], arms["r"]

    # Draw left arm (behind torso)
    draw_limb(c, sl, lh, fill=FILL_DARK)

    # --- Torso (Vault Suit) ---
    # Distinct V-taper to the waist
    c.part(lambda d: d.polygon([(18, 38 + dy), (46, 38 + dy), (40, 62 + dy), (24, 62 + dy)], fill=255), fill=FILL)
    
    # Utility Belt
    c.part(lambda d: d.rectangle([(23, 60 + dy), (41, 64 + dy)], fill=255), fill=FILL_DARK)
    c.part(lambda d: d.rectangle([(30, 59 + dy), (34, 65 + dy)], fill=255), fill=BRIGHT) # Buckle
    
    # Vault Suit Details (Zipper and Collar)
    c.line([(32, 46 + dy), (32, 60 + dy)], BRIGHT, 2)  # Zipper
    c.line([(25, 38 + dy), (32, 48 + dy), (39, 38 + dy)], BRIGHT, 2) # Yellow/Bright V-neck collar

    # Draw right arm (in front of torso)
    draw_limb(c, sr, rh, fill=FILL)

    # Hands / Gloves (Rendered as bright skin tones on the terminal)
    c.part(lambda d: d.ellipse([lh[0] - 5, lh[1] - 5, lh[0] + 5, lh[1] + 5], fill=255), fill=BRIGHT, shade=False)
    c.part(lambda d: d.ellipse([rh[0] - 5, rh[1] - 5, rh[0] + 5, rh[1] + 5], fill=255), fill=BRIGHT, shade=False)
    
    # Thumbs up detail for level0
    if rt: 
        c.part(lambda d: d.rounded_rectangle([rh[0] - 2, rh[1] - 11, rh[0] + 3, rh[1] - 1], 2, fill=255), fill=BRIGHT, shade=False)

    # --- Head & Face ---
    hx, hy = 32 + head_dx, 22 + dy + head_dy
    
    # Ears
    c.part(lambda d: d.ellipse([hx - 18, hy - 3, hx - 12, hy + 5], fill=255), fill=BRIGHT, shade=False)
    c.part(lambda d: d.ellipse([hx + 12, hy - 3, hx + 18, hy + 5], fill=255), fill=BRIGHT, shade=False)
    
    # Main Face (Slightly wider than tall for that specific look)
    c.part(lambda d: d.ellipse([hx - 15, hy - 14, hx + 15, hy + 13], fill=255), fill=BRIGHT, shade=False)

    # Hair - The Pompadour
    # Back/side volume
    c.part(lambda d: d.ellipse([hx - 15, hy - 19, hx + 13, hy - 7], fill=255), fill=BRIGHT, shade=False)
    # The sweeping front curl (pointing right)
    c.part(lambda d: d.ellipse([hx - 18, hy - 27, hx - 2, hy - 12], fill=255), fill=BRIGHT, shade=False)
    # Top connecting volume
    c.part(lambda d: d.ellipse([hx - 8, hy - 29, hx + 12, hy - 16], fill=255), fill=BRIGHT, shade=False)
    # Hairline detail
    c.line([(hx - 12, hy - 10), (hx - 9, hy - 13), (hx - 3, hy - 13)], LINE, 1)

    # --- Facial Features ---
    ex, ey = hx, hy + 1
    blink = pose == "level1" and frame == 1
    sick = pose in ("level2", "level4")

    if blink or pose == "level3":
        for sx in (ex - 6, ex + 6):
            c.line([(sx - 4, ey), (sx + 4, ey)], LINE, 2)
    else:
        # Classic Oval Dot Eyes
        for i, sx in enumerate((ex - 6, ex + 6)):
            if pose == "level4" and i == 1:  # X for sick eye
                c.line([(sx - 3, ey - 3), (sx + 3, ey + 3)], LINE, 2)
                c.line([(sx - 3, ey + 3), (sx + 3, ey - 3)], LINE, 2)
                continue
            
            look_offset = (1 if frame else -1) if pose == "no_date" else 0
            c.part(lambda d, sx=sx: d.ellipse([sx - 2 + look_offset, ey - 4, 
                                               sx + 2 + look_offset, ey + 2], fill=255), 
                   fill=LINE, shade=False, outline=None)
            
    # Expressive Eyebrows
    if pose == "no_date" or sick:
        # Worried/Confused brows
        c.line([(ex - 9, ey - 6), (ex - 4, ey - 7)], LINE, 2)
        c.line([(ex + 4, ey - 7), (ex + 9, ey - 5)], LINE, 2)
    else:
        # Confident Arched Brows
        c.part(lambda d: d.arc([hx - 9, hy - 9, hx - 2, hy - 4], 180, 0, fill=255, width=2), fill=LINE, shade=False, outline=None)
        c.part(lambda d: d.arc([hx + 2, hy - 9, hx + 9, hy - 4], 180, 0, fill=255, width=2), fill=LINE, shade=False, outline=None)

    # Nose dot
    c.dot(hx, hy + 3, LINE)

    # Mouth
    my = hy + 6
    if pose in ("level0", "final_week", "meeting_day", "no_date", "syncing"):  
        # Wide, open grin
        c.part(lambda d: d.chord([hx - 8, my - 2, hx + 8, my + 6], 0, 180, fill=255), fill=LINE, shade=False, outline=None)
        # Dimples
        c.line([(hx - 9, my), (hx - 9, my + 2)], LINE)
        c.line([(hx + 9, my), (hx + 9, my + 2)], LINE)
    elif pose == "level1":
        c.line([(hx - 5, my + 1), (hx + 5, my + 1)], LINE, 2)
    elif sick:
        # Squiggly mouth
        for x in range(hx - 6, hx + 7):
            c.dot(x, my + (1 if (x // 2) % 2 else 0), LINE)
    elif pose == "level3":
        # Frown
        c.part(lambda d: d.arc([hx - 5, my, hx + 5, my + 4], 180, 0, fill=255, width=2), fill=LINE, shade=False, outline=None)

    # --- Props & Overlays ---
    # Syncing watch
    if pose == "syncing":  
        c.part(lambda d: d.ellipse([27, 46 + dy, 33, 52 + dy], fill=255), fill=FILL_DARK, shade=False)
        c.part(lambda d: d.ellipse([28, 47 + dy, 32, 51 + dy], fill=255), fill=HOT, shade=False)
        c.dot(30, 49 + dy, LINE)

    # Meeting day beer/pint
    if pose == "meeting_day":  
        gx, gy = 50, 2 + dy
        c.part(lambda d: d.rectangle([gx, gy, gx + 8, gy + 12], fill=255), fill=BRIGHT, shade=False)
        c.part(lambda d: d.ellipse([gx - 1, gy - 3, gx + 9, gy + 2], fill=255), fill=HOT, shade=False)

    # Confetti
    if pose == "meeting_day":
        for _ in range(16):
            x, y = rnd.randint(1, W - 3), rnd.randint(1, 80)
            if c.px[y][x] == CLEAR:
                c.dot(x, y, HOT)

    c.glow()
    return c

# Pose Map: list of (frame suffix, milliseconds)
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
            draw_vault_boy(pose, i).save(name)
            print("wrote art/mascot/" + name)

