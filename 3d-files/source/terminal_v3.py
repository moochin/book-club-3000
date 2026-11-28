"""Book Club Terminal v3 (deep body): original retro desk-terminal case for the ESP32-2432S028R (Cheap Yellow Display).
Parts: front shell, screen bezel, dial drum, logo badge (all glue-on), rear housing, stand. No supports needed.
Units mm. Case coords: x right, y up (seen from the front), z = 0 at the front face, increasing to the back.
Needs: pip install manifold3d trimesh numpy matplotlib      Run: python3 terminal_v3.py
"""
import math, numpy as np, trimesh
from manifold3d import Manifold, CrossSection, JoinType, FillRule

# ---- board ----
PCB_W, PCB_H, PCB_T = 86.0, 50.0, 1.6
LCD_H_ABOVE_PCB = 4.0
WIN_W, WIN_H = 60.0, 45.5
WIN_OFF_X, WIN_OFF_Y = 2.6, 0.0   # measured: the CYD picture sits 2.6 mm off the PCB centre, away from the USB end
PEG_INSET = 4.0
PEG_D_BASE, PEG_D_TOP = 10.0, 6.0
# ---- case ----
WALL, FACE = 2.4, 2.0
CLR = 0.3
MARGIN_L = 8.0            # left of PCB: USB-C plug + screw columns
PCB_Y0 = 6.0              # chin below the PCB
TOP_GAP = 14.0            # above PCB: JST plugs, wires, badge
RIB = 1.6
POD_FLAT = 18.0
DEPTH = 19.0              # front shell internal depth
REAR_D, REAR_TAPER, REAR_INSET = 20.0, 12.0, 9.0   # rear housing: depth, tapered length, how far the rear face steps in
CORNER_R = 4.0
KNOB_D = 7.2
PILOT_D, SCREW_D, HEAD_D = 2.6, 3.4, 6.4
ENGRAVE = 0.6
# ---- glue-on parts ----
BEZ_GAP, BEZ_T, BEZ_T_BOTTOM, BEZ_TOP, BEZ_BOTTOM = 1.5, 6.0, 4.0, 13.0, 7.0
DRUM_D, DRUM_H = 34.0, 6.0
BADGE_W, BADGE_H, BADGE_T, BADGE_RAISE = 86.0, 8.4, 1.0, 0.8
BADGE_TEXT = "Book-Club 3000"
BADGE_FONT = __import__("os").path.join(__import__("os").path.dirname(__import__("os").path.abspath(__file__)), "fonts", "wideglide.ttf")   # Wide Glide; swap for any .ttf
UNIT_PLATE = (0.0, 6.0, 13.0, 12.0)
DOWEL_D, DOWEL_FACE_DEPTH, DOWEL_PART_DEPTH = 2.0, 1.5, 1.5   # holes for offcuts of 1.75 mm filament
DECO_PILOT, DECO_DEPTH = 2.5, 4.5   # pilot for M3 x 4 button-head screws on the face; depth from the front surface
# ---- speaker ----
SPK_D, SPK_T, SPK_CLR, SPK_WALL = 40.0, 5.0, 0.3, 1.6      # 40 mm round, 5 mm thick (32 ohm 0.5 W)
# ---- stand ----
TILT, STAND_W, LIP = 15.0, 100.0, 4.0
TRAY_D, TRAY_H, TRAY_W, TRAY_DEPTH = 16.0, 8.0, 9.0, 5.0   # stylus tray across the front: depth, height, channel width and depth

PCB_YH = PCB_H + 2*CLR
PCB_Y1 = PCB_Y0 + PCB_YH
IN_H = PCB_Y1 + TOP_GAP
OUT_H = IN_H + 2*WALL
PCB_X0, PCB_X1 = MARGIN_L, MARGIN_L + PCB_W + 2*CLR
BAY_X0 = PCB_X1 + RIB
POD_CX, POD_CY = BAY_X0 + POD_FLAT, IN_H/2
POD_RI, POD_RO = IN_H/2, OUT_H/2
Z_IN = FACE + DEPTH
Z_PCB0 = FACE + LCD_H_ABOVE_PCB
Z_PCB1 = Z_PCB0 + PCB_T
TOTAL_T = Z_IN + REAR_D
WCX, WCY = PCB_X0 + CLR + PCB_W/2 + WIN_OFF_X, PCB_Y0 + PCB_YH/2 + WIN_OFF_Y
BADGE_Y0 = WCY + WIN_H/2 + BEZ_GAP + BEZ_T + 1.9
BADGE_DOWEL_DX = BADGE_W/2 - 8.0
DRUM_DOWEL_R = 12.0
BEZ_DOWEL_X = WIN_W/2 + BEZ_GAP + BEZ_T/2
DOWELS_BEZEL = [(WCX - BEZ_DOWEL_X, WCY), (WCX + BEZ_DOWEL_X, WCY)]
DOWELS_BADGE = [(WCX - BADGE_DOWEL_DX, BADGE_Y0 + BADGE_H/2), (WCX + BADGE_DOWEL_DX, BADGE_Y0 + BADGE_H/2)]
DOWELS_DRUM = [(POD_CX - DRUM_DOWEL_R, POD_CY), (POD_CX + DRUM_DOWEL_R, POD_CY)]
DOWELS = DOWELS_BEZEL + DOWELS_BADGE + DOWELS_DRUM
USB_Y0, USB_Y1, USB_Z0, USB_Z1 = PCB_Y0 + 16.8, PCB_Y0 + 32.8, 4.5, 14.0   # 16 x 9.5 mm, centred on the USB-C socket (mid short edge)

def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1-x0, y1-y0, z1-z0]).translate([x0, y0, z0])
def cyl(x, y, z0, z1, d, d2=None):
    return Manifold.cylinder(z1-z0, d/2, (d if d2 is None else d2)/2, 64).translate([x, y, z0])
def circ(x, y, r):
    return CrossSection.circle(r, 128).translate([x, y])
def outer_cs():
    r = CORNER_R
    return CrossSection.batch_hull([circ(-WALL+r, -WALL+r, r), circ(-WALL+r, IN_H+WALL-r, r), circ(POD_CX, POD_CY, POD_RO)])
def inner_cs():
    return outer_cs().offset(-WALL, JoinType.Round, 2.0, 128)

IN_W_MAX = 400
DECO_SCREWS = [(4.0, IN_H-4.0), (3.0, 16.5), (BAY_X0-1.6, 4.5)]   # top-left sits coaxial over its corner column
POD_COLS = [(POD_CX + (POD_RI-3.7)*math.cos(a), POD_CY + (POD_RI-3.7)*math.sin(a)) for a in (math.radians(60), math.radians(-60))]
LEFT_COLS = [(0, 0, 8, 8), (0, IN_H-8, 8, IN_H)]
SCREWS = [((a+c)/2, (b+d)/2) for a, b, c, d in LEFT_COLS] + POD_COLS
PEGS = [(x, y) for x in (PCB_X0+CLR+PEG_INSET, PCB_X1-CLR-PEG_INSET) for y in (PCB_Y0+CLR+PEG_INSET, PCB_Y1-CLR-PEG_INSET)]

BXI, BXO = WIN_W/2 + BEZ_GAP, WIN_W/2 + BEZ_GAP + BEZ_T
BYI = WIN_H/2 + BEZ_GAP
BY_TOP, BY_BOT = WCY + BYI + BEZ_T, WCY - BYI - BEZ_T_BOTTOM

def shell():
    s = Manifold.extrude(outer_cs(), Z_IN) - Manifold.extrude(inner_cs(), Z_IN).translate([0, 0, FACE])
    for fx in np.arange(18, 92, 6.0):                                # fins along the top
        s += box(fx, IN_H+WALL-0.5, 0, fx+3, IN_H+WALL+1.5, Z_IN)
    for ang in range(-66, 67, 12):                                   # ribs around the pod rim
        s += box(POD_RO-0.4, -1.3, 0, POD_RO+1.3, 1.3, Z_IN).rotate([0, 0, ang]).translate([POD_CX, POD_CY, 0])
    for (a, b, c, d) in LEFT_COLS:
        s += box(a, b, FACE, c, d, Z_IN)
    for (x, y) in POD_COLS:
        s += cyl(x, y, FACE, Z_IN, 8.0)
    for x0, x1 in ((PCB_X0+CLR, PCB_X0+6), (PCB_X1-6, PCB_X1)):      # PCB corner pads and edge stops
        for y0, y1 in ((PCB_Y0, PCB_Y0+6), (PCB_Y1-6, PCB_Y1)):
            s += box(x0, y0, FACE, x1, y1, Z_PCB0)
        s += box(x0, 0, FACE, x1, PCB_Y0, Z_PCB1)
        s += box(x0, PCB_Y1, FACE, x1, IN_H, Z_PCB1)
    s += box(PCB_X1, 0, FACE, PCB_X1+RIB, IN_H, Z_PCB1-0.2)
    s -= box(WCX-WIN_W/2, WCY-WIN_H/2, -1, WCX+WIN_W/2, WCY+WIN_H/2, FACE+1)                    # screen window
    for (dx, dy) in DOWELS:                                          # locating holes for 1.75 mm filament dowels
        s -= cyl(dx, dy, -1, DOWEL_FACE_DEPTH, DOWEL_D)
    for ly in np.arange(24.0, 50.0, 3.6):                            # dummy louvres
        s -= box(0.5, ly, -1, 12.0, ly+1.6, 0.9)
    for (sx, sy) in DECO_SCREWS:                                     # real decorative M3 screws, self-tapping from the front
        if sy > 10 and sy < IN_H-10:                                 # over open cavity: add a short boss to bite into
            s += cyl(sx, sy, FACE-0.01, DECO_DEPTH, 7.0) ^ box(0, 0, 0, IN_W_MAX, IN_H, Z_IN)
        s -= cyl(sx, sy, -1, DECO_DEPTH, DECO_PILOT)
    s -= cyl(POD_CX, POD_CY, -1, FACE+1, KNOB_D)                     # encoder
    for ang in range(40, 141, 12):                                   # perforation arc
        a = math.radians(ang); r = DRUM_D/2 + 3.6
        s -= cyl(POD_CX+r*math.cos(a), POD_CY+r*math.sin(a), -1, 1.0, 2.4)
    for gy in (7.0, 10.5, 14.0):                                     # speaker grille
        s -= box(POD_CX-7, gy-0.8, -1, POD_CX+7, gy+0.8, FACE+1)
    s -= box(-WALL-1, USB_Y0, USB_Z0, 0.01, USB_Y1, USB_Z1)          # USB-C slot
    for (x, y) in SCREWS:
        s -= cyl(x, y, Z_PCB0, Z_IN+1, PILOT_D)
    return s

def prism_yz(profile, x0, x1):
    """Extrude a (y, z) profile along x."""
    m = Manifold.extrude(CrossSection([profile]), x1-x0).rotate([90, 0, 90])
    b = m.bounding_box(); ys = [p[0] for p in profile]; zs = [p[1] for p in profile]
    return m.translate([x0-b[0], min(ys)-b[1], min(zs)-b[2]])

def bezel():
    """Deep screen surround. Local z: 0 = glued face, rising toward the viewer. Print as-is."""
    m = prism_yz([(BY_BOT, 0), (BY_TOP, 0), (BY_TOP, BEZ_TOP), (BY_BOT, BEZ_BOTTOM)], WCX-BXO, WCX+BXO)
    lo = box(WCX-BXI, WCY-BYI, -1, WCX+BXI, WCY+BYI, 0)
    hi = box(WCX-BXI-3.5, WCY-BYI-2.0, BEZ_TOP, WCX+BXI+3.5, WCY+BYI+3.5, BEZ_TOP+1)
    m -= Manifold.batch_hull([lo, hi])
    for rx in np.arange(WCX-BXO+5, WCX+BXO-7, 7.0):                  # ribs along the top
        m += box(rx, BY_TOP-0.2, 0, rx+3, BY_TOP+1.2, BEZ_TOP-1.5)
    for k in range(3):                                               # grooves down each side
        z = 2.0 + 2.4*k
        m -= box(WCX-BXO-1, BY_BOT+6, z, WCX-BXO+0.7, BY_TOP-6, z+1.0)
        m -= box(WCX+BXO-0.7, BY_BOT+6, z, WCX+BXO+1, BY_TOP-6, z+1.0)
    for (dx, dy) in DOWELS_BEZEL:
        m -= cyl(dx, dy, -1, DOWEL_PART_DEPTH, DOWEL_D)
    return m

def drum():
    """Raised dial collar under the knob. Local z up from the glued face."""
    d = cyl(0, 0, 0, DRUM_H-2, DRUM_D) + cyl(0, 0, DRUM_H-2, DRUM_H, DRUM_D, DRUM_D-4)
    d -= cyl(0, 0, -1, DRUM_H+1, 13.0)
    d -= cyl(0, 0, DRUM_H-1.2, DRUM_H+1, 22.0)                       # seat for the knob skirt
    for k in range(24):
        d -= box(DRUM_D/2-2.6, -0.45, DRUM_H-2.2, DRUM_D/2+1, 0.45, DRUM_H+1).rotate([0, 0, 15*k])
    for sx in (-1, 1):
        d -= cyl(sx*DRUM_DOWEL_R, 0, -1, DOWEL_PART_DEPTH, DOWEL_D)
    return d.translate([POD_CX, POD_CY, 0])

def rear():
    """Rear housing. Local z: 0 = rear face (on the bed), rim at REAR_D mates with the front shell."""
    o, i = outer_cs(), inner_cs()
    def slab(cs, z): return Manifold.extrude(cs, 0.02).translate([0, 0, z])
    back_o = o.offset(-REAR_INSET, JoinType.Round, 2.0, 128)
    solid = Manifold.batch_hull([slab(back_o, 0), slab(o, REAR_TAPER)]) + Manifold.extrude(o, REAR_D-REAR_TAPER).translate([0, 0, REAR_TAPER])
    back_i = back_o.offset(-WALL, JoinType.Round, 2.0, 128)
    hollow = Manifold.batch_hull([slab(back_i, WALL), slab(i, REAR_TAPER)]) + Manifold.extrude(i, REAR_D-REAR_TAPER+1).translate([0, 0, REAR_TAPER])
    c = solid - hollow
    envelope = solid + Manifold.extrude(o, 60).translate([0, 0, REAR_TAPER])   # nothing may poke outside this
    for (x, y) in SCREWS:                                            # screw bosses, trimmed to the tapered outside
        c += cyl(x, y, 0, REAR_D, 8.4) ^ envelope
    D = REAR_D                                                       # alignment lips, ramped off the wall at 45 degrees
    lip = [(-0.2, D-5.0), (CLR+1.6, D-2.9), (CLR+1.6, D+2.5), (CLR, D+2.5), (CLR, D), (-0.2, D)]
    c += prism_yz(lip, 10, BAY_X0)
    c += prism_yz([(IN_H-y, z) for (y, z) in lip][::-1], 10, BAY_X0)
    c += prism_xz(lip, 12, IN_H - 12)                                # same lip along the flat short end
    peg_top = REAR_D + (Z_IN - Z_PCB1 - 0.2)
    for (x, y) in PEGS:                                              # clamp pegs onto the PCB corners
        c += peg(x, y, peg_top) ^ envelope
    for vy in np.arange(-15.0, 15.1, 5.0):                          # vents over the speaker cone, clipped to a circle
        half = (SPK_VENT_R**2 - vy**2) ** 0.5 - 1.0
        c -= box(SPK_X - half, SPK_Y + vy - 0.8, -1, SPK_X + half, SPK_Y + vy + 0.8, WALL + 1)
    for vy in np.arange(POD_CY-9, POD_CY+10, 4.5):
        c -= box(POD_CX-9, vy-0.8, -1, POD_CX+9, vy+0.8, WALL+1)
    c += speaker_holder()
    for (x, y) in SCREWS:
        c -= cyl(x, y, -1, REAR_D+1, SCREW_D)
        c -= cyl(x, y, -1, REAR_D-4.0, HEAD_D)
    # The housing is printed back-face down and then turned over onto the shell. Turning it over is a rotation,
    # which swaps top and bottom (y -> IN_H - y), so build it pre-swapped. The outline and screws are symmetric.
    return c.mirror([0, 1, 0]).translate([0, IN_H, 0])

def rear_closed():
    """The rear housing as it sits on the shell once turned over, in shell coordinates."""
    return rear().mirror([0, 1, 0]).translate([0, IN_H, 0]).mirror([0, 0, 1]).translate([0, 0, TOTAL_T])

def peg(x, y, top):
    """Board clamp peg: 10 mm at the base tapering to 6 mm, with a flared foot and four gussets so it can't snap off."""
    p = cyl(0, 0, 0, top, PEG_D_BASE, PEG_D_TOP)
    p += cyl(0, 0, 0, WALL + 4.0, PEG_D_BASE + 6.0, PEG_D_BASE)      # flared foot
    for a in (0, 90, 180, 270):                                      # gussets, 1.6 mm thick, 10 mm tall
        g = Manifold.extrude(CrossSection([[(0, 0), (PEG_D_BASE/2 + 5.0, 0), (0, WALL + 12.0)]]), 1.6)
        g = g.rotate([90, 0, 0]).translate([0, 0.8, 0]).rotate([0, 0, a])
        p += g
    return p.translate([x, y, 0])

def prism_xz(profile, y0, y1):
    """Extrude an (x, z) profile along y."""
    m = Manifold.extrude(CrossSection([profile]), y1 - y0).rotate([90, 0, 0])
    b = m.bounding_box(); xs = [p[0] for p in profile]; zs = [p[1] for p in profile]
    return m.translate([min(xs) - b[0], y0 - b[1], min(zs) - b[2]])

def rrect(w, h, r):
    return CrossSection.batch_hull([circ(sx*(w/2-r), sy*(h/2-r), r) for sx in (-1, 1) for sy in (-1, 1)])

SPK_VENT_R = 16.0
SPK_X, SPK_Y = 51.0, PCB_Y0 + 26.0   # centred on the main rear vents, behind the board

def speaker_holder():
    """Snap-in ring on the rear housing's inner back wall for a 23 mm round speaker, cone facing the back vents.
    Sits behind the board, close to the board's SPEAK connector, so the 100 mm lead reaches.
    Local rear coords: the back wall's inner surface is at z = WALL."""
    r_in = SPK_D/2 + SPK_CLR
    h = SPK_T + 0.6
    f = cyl(0, 0, WALL - 0.01, WALL + h, 2*(r_in + SPK_WALL)) - cyl(0, 0, WALL - 1, WALL + h + 1, 2*r_in)
    lip = cyl(0, 0, WALL + SPK_T + 0.1, WALL + h, 2*(r_in + 0.3), 2*(r_in - 0.7)) - cyl(0, 0, WALL, WALL + h + 1, 2*(r_in - 0.7))
    keep = Manifold()
    for ang in (90, 210, 330):                                      # three short snap lips, 45 degree undersides
        keep += box(-4, r_in - 1.5, WALL, 4, r_in + 1.0, WALL + h + 1).rotate([0, 0, ang - 90])
    f += lip ^ keep
    f -= box(-2.5, -r_in - SPK_WALL - 1, WALL + 1.5, 2.5, -r_in + 1, WALL + h + 1).rotate([0, 0, 30])   # wire notch
    return f.translate([SPK_X, SPK_Y, 0])

def stand():
    """Profile (u = front->back, v = up) extruded along x. Print on its side."""
    t = math.radians(TILT); s_, c_ = math.sin(t), math.cos(t)
    slot = TOTAL_T + 0.6
    P = (6.0, 4.0 + slot*s_)
    lipv = P[1] + LIP*c_
    prof = [(0, 0), (82, 0), (82, 6), (66, 40), (54, 40), (12, lipv), (0, lipv)]
    a, b = (s_, c_), (c_, -s_); L = 90
    case = [P, (P[0]+slot*b[0], P[1]+slot*b[1]), (P[0]+slot*b[0]+L*a[0], P[1]+slot*b[1]+L*a[1]), (P[0]+L*a[0], P[1]+L*a[1])]
    cs = CrossSection([prof]) - CrossSection([case])
    # stylus tray: a ledge across the front with an open-ended U channel, so any stylus just lies in it
    cs += CrossSection.square([TRAY_D + 0.5, TRAY_H]).translate([-TRAY_D, 0])
    groove = CrossSection.circle(TRAY_W/2, 64).translate([-TRAY_D/2, TRAY_H - TRAY_DEPTH + TRAY_W/2]) + \
             CrossSection.square([TRAY_W, TRAY_DEPTH]).translate([-TRAY_D/2 - TRAY_W/2, TRAY_H - TRAY_DEPTH + TRAY_W/2])
    cs -= groove
    cs -= CrossSection([[(56, 5), (76, 5), (64, 33)]]).offset(-0.01, JoinType.Round)      # cable tunnel / weight saving
    for v in (4.0, 8.0, 12.0):
        if v > TRAY_H + 1:                                           # front grooves only above the tray
            cs -= CrossSection.square([1.0, 1.6]).translate([-0.01, v])
    for k in (0.2, 0.4, 0.6, 0.8):
        cs -= CrossSection.square([2.4, 2.4], True).rotate(25).translate([82+(66-82)*k, 6+(40-6)*k])
    st = Manifold.extrude(cs, STAND_W)
    return st, P

def text_cs(text, height, font_path):
    from matplotlib.textpath import TextPath
    from matplotlib.font_manager import FontProperties
    tp = TextPath((0, 0), text, size=10, prop=FontProperties(fname=font_path))
    cs = CrossSection([np.asarray(p)[:-1] for p in tp.to_polygons() if len(p) > 3], FillRule.EvenOdd)
    x0, y0, x1, y1 = cs.bounds(); k = height/(y1-y0)
    return cs.translate([-(x0+x1)/2, -(y0+y1)/2]).scale([k, k])
def badge_plate():
    r = BADGE_H/2
    cs = CrossSection.batch_hull([circ(-BADGE_W/2+r, 0, r), circ(BADGE_W/2-r, 0, r)])
    return Manifold.extrude(cs, BADGE_T), Manifold.extrude(cs - cs.offset(-0.8, JoinType.Round, 2.0, 64), BADGE_RAISE).translate([0, 0, BADGE_T])
def badge_letters():
    t = text_cs(BADGE_TEXT, BADGE_H-3.4, BADGE_FONT); x0, y0, x1, y1 = t.bounds()
    if x1-x0 > BADGE_W-8: t = t.scale([(BADGE_W-8)/(x1-x0)]*2)
    return Manifold.extrude(t, BADGE_RAISE).translate([0, 0, BADGE_T])
def badge():
    """Print flat; swap filament at 1.0 mm for two-tone lettering."""
    p, bd = badge_plate(); m = p + bd + badge_letters()
    for sx in (-1, 1):
        m -= cyl(sx*BADGE_DOWEL_DX, 0, -1, 0.6, DOWEL_D)
    return m

BADGE2_BASE, BADGE2_YELLOW, BADGE2_RELIEF = 0.8, 0.6, 0.8      # layer stack for the two-colour badge (0.2 mm layers)

def badge_two_colour():
    """Black base, a thin yellow layer over the whole top, then black lettering and a black rim on the yellow.
    Returns (black, yellow). Each colour change happens at a single height, so one STL with two filament swaps works too."""
    r = BADGE_H/2
    cs = CrossSection.batch_hull([circ(-BADGE_W/2+r, 0, r), circ(BADGE_W/2-r, 0, r)])
    z1, z2 = BADGE2_BASE, BADGE2_BASE + BADGE2_YELLOW
    base = Manifold.extrude(cs, z1)
    yellow = Manifold.extrude(cs, BADGE2_YELLOW).translate([0, 0, z1])
    rim = Manifold.extrude(cs - cs.offset(-0.8, JoinType.Round, 2.0, 64), BADGE2_RELIEF).translate([0, 0, z2])
    t = text_cs(BADGE_TEXT, BADGE_H-3.4, BADGE_FONT); x0, y0, x1, y1 = t.bounds()
    if x1-x0 > BADGE_W-8: t = t.scale([(BADGE_W-8)/(x1-x0)]*2)
    letters = Manifold.extrude(t, BADGE2_RELIEF).translate([0, 0, z2])
    black = base + rim + letters
    for sx in (-1, 1):
        black -= cyl(sx*BADGE_DOWEL_DX, 0, -1, 0.6, DOWEL_D)
    return black, yellow

# ---- knob, for an EC11 encoder with a 20 mm D-shaft ----
SHAFT_D, SHAFT_FLAT, SHAFT_CLR = 6.0, 4.5, 0.15     # 6 mm shaft, 4.5 mm across the flat
KNOB_D_TOP, KNOB_D_BASE, KNOB_H = 17.0, 19.0, 15.0
SKIRT_D, SKIRT_H, BORE_DEPTH = 21.4, 2.5, 13.2

def knob():
    """Printed knob that sits in the dial drum's seat. Local z: 0 = top face (print it this way down), base at KNOB_H.
    Grip ribs round the sides, a skirt that drops into the drum seat, a pointer groove to paint in."""
    k = Manifold.cylinder(KNOB_H - SKIRT_H, KNOB_D_TOP/2, KNOB_D_BASE/2, 96)
    k += cyl(0, 0, KNOB_H - SKIRT_H - 0.01, KNOB_H, SKIRT_D)
    for i in range(20):                                              # grip ribs
        rib = Manifold.cylinder(KNOB_H - SKIRT_H - 1.0, 0.9, 0.9, 12).translate([KNOB_D_TOP/2 + 0.2, 0, 0.6])
        k += rib.rotate([0, 0, 18*i])
    k -= box(-0.6, 1.5, -1, 0.6, KNOB_D_TOP/2 + 2, 0.8)              # pointer groove on top
    k -= box(-0.6, SKIRT_D/2 - 0.8, KNOB_H - SKIRT_H - 0.01, 0.6, SKIRT_D/2 + 1, KNOB_H + 1)   # matching tick on the skirt
    r = SHAFT_D/2 + SHAFT_CLR                                        # D-shaft bore from the base
    bore = cyl(0, 0, KNOB_H - BORE_DEPTH, KNOB_H + 1, 2*r)
    bore -= box(-r - 1, -r - 1, KNOB_H - BORE_DEPTH - 1, r + 1, -r + (SHAFT_D - SHAFT_FLAT) - SHAFT_CLR, KNOB_H + 2)
    k -= bore
    k -= Manifold.cylinder(0.8, r, r + 0.6, 32).translate([0, 0, KNOB_H - 0.8 + 0.001])   # lead-in chamfer
    return k

DIAL_ON_RIGHT = True   # True: dial on the right seen from the front (as in the renders). The badge is never mirrored.
OUT_DIR = "."

def save(m, name, mirror=None):
    import os
    if (DIAL_ON_RIGHT if mirror is None else mirror):
        m = m.mirror([1, 0, 0])
    name = os.path.join(OUT_DIR, name)
    me = m.to_mesh(); t = trimesh.Trimesh(np.asarray(me.vert_properties)[:, :3], np.asarray(me.tri_verts)); t.export(name)
    print(f"{name}: manifold={m.status().name} parts={len(m.decompose())} size={np.round(t.extents,1)} vol={t.volume/1000:.1f}cm3", flush=True)

if __name__ == "__main__":
    print("case", round(POD_CX+POD_RO+WALL, 1), "x", OUT_H, "x", TOTAL_T, "(+", BEZ_TOP, "bezel)")
    save(shell(), "v3_front_shell.stl"); save(bezel(), "v3_screen_bezel.stl"); save(drum(), "v3_dial_drum.stl")
    save(badge(), "v3_logo_badge.stl", mirror=False); save(knob(), "v3_knob.stl", mirror=False)
    _bk, _yl = badge_two_colour(); save(_bk, "v3_badge_2c_black.stl", mirror=False); save(_yl, "v3_badge_2c_yellow.stl", mirror=False); save(_bk + _yl, "v3_badge_2c_single_file.stl", mirror=False); save(rear(), "v3_rear_housing.stl"); save(stand()[0], "v3_stand.stl")
