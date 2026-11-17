#!/usr/bin/env python3
"""A barn stand for the round Waveshare ESP32-S3-Touch-AMOLED-1.75C (the aluminium puck with a lanyard).

The puck sits in the barn's round hayloft window, leaning back a little so it's easy to see on a desk, and
lifts straight out to go on its lanyard. Notches keep clear of the two buttons on its right edge and give a
fingertip on the left; a tunnel brings a USB-C cable from the back of the barn up to the port at the bottom
so it charges while it sits there. Multi-colour on a Bambu A1 with the AMS lite: red walls, dark roof,
white trim and sign, green base.

    pip install manifold3d trimesh matplotlib
    python3 barn_stand.py                                  # writes the STLs to out-round/
    python3 barn_stand.py --diameter 54.2 --thickness 13.8 --sign "POCKET PONY"

MEASURE THE PUCK FIRST (calipers): its diameter and thickness are guesses until then.
"""
import argparse
import math
import os

import numpy as np
from manifold3d import CrossSection, FillRule, JoinType, Manifold
import trimesh

ap = argparse.ArgumentParser()
ap.add_argument("--diameter", type=float, default=54.0, help="the puck's diameter (mm): MEASURE THIS")
ap.add_argument("--thickness", type=float, default=14.0, help="the puck's thickness (mm): MEASURE THIS")
ap.add_argument("--tilt", type=float, default=10.0, help="how far it leans back (degrees)")
ap.add_argument("--sign", default="POCKET PONY", help="text on the base (keep it short)")
ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "out-round"))
a = ap.parse_args()

SEG = 96
R = a.diameter / 2 + 0.4        # the window's radius: the puck plus play
T = a.thickness
DEPTH = 0.62 * T                # how far the puck sits into the wall (the rest stands proud)
TILT = math.radians(a.tilt)

BASE_T = 4.0
DOORS = 16.0                    # the barn doors under the window
BW = 2 * R + 22                 # barn width (x)
CZ = BASE_T + DOORS + R + 3     # the window's centre height (z)
EAVES = CZ + R * 0.45           # the walls end part way up the window: the window is in the gable
PEAK = CZ + R + 14              # the gable's top
WALL = DEPTH + R * math.sin(TILT) + 4.0   # the front wall, deep enough for the leaning recess
BD = max(WALL + 10, 34.0)       # barn depth (y; front at 0)
cx = BW / 2


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def slab_xz(cs, y0, y1):
    """A shape drawn on the front face (x, z), through y0..y1."""
    if cs.is_empty():
        return Manifold()
    return cs.extrude(y1 - y0).rotate([90, 0, 0]).translate([0, y1, 0])


def poly(pts):
    return CrossSection([pts], FillRule.EvenOdd)


def circle(x, z, r):
    return CrossSection.circle(r, SEG).translate([x, z])


def bar(x0, z0, x1, z1, wdt):
    v = np.array([x1 - x0, z1 - z0]); n = np.array([-v[1], v[0]]) / np.linalg.norm(v) * wdt / 2
    return poly([(x0 + n[0], z0 + n[1]), (x1 + n[0], z1 + n[1]), (x1 - n[0], z1 - n[1]), (x0 - n[0], z0 - n[1])])


def tilted(m):
    """Lean a shape built around the window's centre back by TILT (top away from you)."""
    return m.translate([-cx, 0, -CZ]).rotate([-a.tilt, 0, 0]).translate([cx, 0, CZ])


# ---------------------------------------------------------------- the red body
outline = [(0, BASE_T), (BW, BASE_T), (BW, EAVES), (cx, PEAK), (0, EAVES)]
body = slab_xz(poly(outline), 0, BD)
# hollow the back a little to save plastic (a deep, closed-off barn interior)
body -= box(5, WALL + 2, BASE_T + 2, BW - 5, BD - 3, EAVES - 4)

# the recess the puck sits in, leaning back: a cylinder along y, from in front of the face to DEPTH in
cyl = Manifold.cylinder(DEPTH + 30, R, R, SEG).rotate([-90, 0, 0]).translate([cx, -30, CZ])
body -= tilted(cyl)
# the right side: clear of the PWR and BOOT buttons (about 1:30 and 4:30 o'clock), all the way round
relief = (Manifold.cylinder(DEPTH + 30, R + 2.5, R + 2.5, SEG).rotate([-90, 0, 0]).translate([cx, -30, CZ])
          ^ box(cx + R * 0.30, -40, CZ - R * 0.95, cx + R + 4, 60, CZ + R * 0.95))
body -= tilted(relief)
# the left side: a fingertip scoop to lift it out by (the top notch is cut below, with the trim)
scoop = Manifold.cylinder(40, 9, 9, SEG).rotate([-90, 0, 0]).translate([cx - R - 2, -20, CZ + 2])
body -= tilted(scoop)
# a fingertip notch at the top too: tip it forward from there and lift it out
top_notch = Manifold.cylinder(40, 8, 8, SEG).rotate([-90, 0, 0]).translate([cx, -20, CZ + R + 1])
body -= tilted(top_notch)
# the USB-C tunnel: a slot under the puck's bottom edge, down, then straight out of the back of the barn
plug_w, plug_d = 13.5, 8.0
slot = box(cx - plug_w / 2, -1, CZ - R - 16, cx + plug_w / 2, plug_d + 2, CZ - R + 2)
body -= tilted(slot)
tunnel = box(cx - plug_w / 2 - 1, 2, BASE_T + 1.5, cx + plug_w / 2 + 1, BD + 1, BASE_T + 1.5 + 11)
body -= tunnel
body -= tilted(box(cx - plug_w / 2, 2, BASE_T + 1.5, cx + plug_w / 2, plug_d + 2, CZ - R + 2))

# ---------------------------------------------------------------- white trim (set into the red, 0.8 proud)
T_IN, T_OUT = 0.6, 0.8
trim = Manifold()
ring = circle(cx, CZ, R + 4.5) - circle(cx, CZ, R + 0.6)
trim += slab_xz(ring, -T_OUT, T_IN)
for x in (0.8, BW - 0.8):  # corner boards
    trim += box(x - 2.2, -T_OUT, BASE_T, x + 2.2, T_IN, EAVES)
# the barn doors under the window, with their white X braces
dw, dh = BW * 0.46, DOORS - 3
dz = BASE_T + 1.5
doors_out = CrossSection.square([dw + 3, dh + 1.5]).translate([cx - dw / 2 - 1.5, dz - 0.01])
doors_in = CrossSection.square([dw, dh]).translate([cx - dw / 2, dz])
trim += slab_xz(doors_out - doors_in, -T_OUT, T_IN)
for x0 in (cx - dw / 2, cx):
    panel = CrossSection.square([dw / 2, dh]).translate([x0, dz])
    xb = (bar(x0, dz, x0 + dw / 2, dz + dh, 1.3) + bar(x0, dz + dh, x0 + dw / 2, dz, 1.3)) ^ panel
    trim += slab_xz(xb, -T_OUT, T_IN)
trim += slab_xz(CrossSection.square([1.4, dh]).translate([cx - 0.7, dz]), -T_OUT, T_IN)
trim = trim - tilted(cyl) - tilted(relief) - tilted(slot) - tilted(scoop) - tilted(top_notch)  # nothing in the puck's way
body -= trim

# ---------------------------------------------------------------- the roof: two dark slabs with an overhang
OVER, RT = 3.0, 2.6
roof = Manifold()
for side in (-1, 1):
    ex, ez = (0 if side < 0 else BW), EAVES
    vx, vz = cx - ex, PEAK - ez
    L = float(np.hypot(vx, vz)); ux, uz = vx / L, vz / L
    nx, nz = (-uz, ux) if side < 0 else (uz, -ux)
    sx, sz = ex - ux * OVER, ez - uz * OVER
    roof += slab_xz(poly([(sx, sz), (cx, PEAK), (cx + nx * RT, PEAK + nz * RT), (sx + nx * RT, sz + nz * RT)]),
                    -OVER * 0.6, BD + OVER * 0.6)
roof += slab_xz(poly([(cx - 2.2, PEAK - 1), (cx + 2.2, PEAK - 1), (cx + 2.2, PEAK + RT + 0.8), (cx - 2.2, PEAK + RT + 0.8)]),
                -OVER * 0.6, BD + OVER * 0.6)
roof = roof - body

# ---------------------------------------------------------------- the green base, with the sign on its front
BX, BYF, BYB = 6.0, 8.0, 4.0
base = box(-BX, -BYF, 0, BW + BX, BD + BYB, BASE_T)
base -= box(cx - plug_w / 2 - 1, 2, BASE_T - 2.5, cx + plug_w / 2 + 1, BD + BYB + 1, BASE_T + 1)  # the cable's way out
sign = Manifold()
if a.sign:
    from matplotlib.textpath import TextPath
    from matplotlib.font_manager import FontProperties
    tp = TextPath((0, 0), a.sign.upper(), size=4.2, prop=FontProperties(family="DejaVu Sans", weight="bold"))
    cs = CrossSection([[(float(x), float(y)) for x, y in p] for p in tp.to_polygons() if len(p) >= 3], FillRule.EvenOdd)
    bb = cs.bounds(); tw = bb[2] - bb[0]
    room = BW + 2 * BX - 6
    if tw > room:
        cs = cs.scale([room / tw, room / tw]); bb = cs.bounds(); tw = bb[2] - bb[0]
    th = bb[3] - bb[1]
    cs = cs.translate([cx - tw / 2 - bb[0], (BASE_T - th) / 2 - bb[1]])
    sign = slab_xz(cs, -BYF - 0.6, -BYF + 0.6)
    base -= sign

# ---------------------------------------------------------------- a stand-in puck, for the preview only
puck = tilted(Manifold.cylinder(T, a.diameter / 2, a.diameter / 2, SEG).rotate([-90, 0, 0])
              .translate([cx, DEPTH - T, CZ - 0.4]))

# ---------------------------------------------------------------- write it out
os.makedirs(a.out, exist_ok=True)


def save(m, name):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts, process=False)
    t.export(os.path.join(a.out, name))
    return t


parts = {
    "stand-1-walls-red.stl": body,
    "stand-2-roof-dark.stl": roof,
    "stand-3-trim-white.stl": trim,
    "stand-4-base-green.stl": base,
    "stand-5-sign-white.stl": sign,
}
for name, m in parts.items():
    if m.is_empty():
        print("  (empty)", name, m.status())
        continue
    t = save(m, name)
    print(f"  {name:26s} watertight={t.is_watertight}  {t.volume / 1000:.1f} cm3")
save(puck, "preview-puck.stl")
print(f"stand {BW + 2 * BX:.1f} x {BD + BYF + BYB:.1f} x {PEAK + RT + 0.8:.1f} mm (w x d x h); "
      f"window {2 * R:.1f} mm, {DEPTH:.1f} mm deep, leaning back {a.tilt:.0f} degrees")
