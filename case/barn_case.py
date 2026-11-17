#!/usr/bin/env python3
"""A little red barn for Pocket Pony: a case for the Waveshare ESP32-S3-Touch-AMOLED-1.8 in its own black case.

The board's black case (about 45.2 x 37.6 mm, depth measured: see CASE_D) slides into a pocket from the back,
its screen showing through a framed window in the barn front, and a back door holds it in. Prints as one
multi-colour object on a Bambu A1 with the AMS lite (red walls, dark roof, white trim, green base), plus the
back door separately.

    pip install manifold3d trimesh matplotlib
    python3 barn_case.py                     # writes the STLs to out/
    python3 barn_case.py --depth 13.6 --sign "POCKET PONY"

Measure your board's case with calipers first and pass --width/--height/--depth if they differ.
"""
import argparse
import os

import numpy as np
from manifold3d import CrossSection, FillRule, Manifold
import trimesh

ap = argparse.ArgumentParser()
ap.add_argument("--width", type=float, default=37.6, help="board case width (mm, across the screen)")
ap.add_argument("--height", type=float, default=45.2, help="board case height (mm)")
ap.add_argument("--depth", type=float, default=13.0, help="board case depth (mm): MEASURE THIS")
ap.add_argument("--screen-w", type=float, default=28.7)
ap.add_argument("--screen-h", type=float, default=34.94)
ap.add_argument("--screen-up", type=float, default=0.0, help="screen centre above the case centre (mm)")
ap.add_argument("--sign", default="POCKET PONY", help="text on the base (keep it short)")
ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "out"))
a = ap.parse_args()

FIT = 0.4          # clearance all round the board case
W, H, D = a.width + FIT, a.height + FIT, a.depth + 0.3
FRONT = 2.0        # the barn front in front of the screen
BACK = 2.4         # the back door
SIDE = 7.0         # walls either side of the pocket
CELLAR = 16.0      # room under the board for the USB-C plug and its cable
BASE_T = 4.0       # the grassy base slab

BW = W + 2 * SIDE                 # barn width (x)
BD = FRONT + D + BACK             # barn depth (y; front face at y = 0, back at y = BD)
Z0 = BASE_T                       # barn floor
POCKET_Z = Z0 + CELLAR            # bottom of the board
EAVES = POCKET_Z + H + 4.0        # top of the walls
PEAK = EAVES + BW * 0.42          # top of the gable

SEG = 48


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def front_profile(poly_xz, y0, y1):
    """Extrude a polygon drawn on the front face (x, z) through y0..y1."""
    cs = CrossSection([poly_xz], FillRule.EvenOdd)
    return cs.extrude(y1 - y0).rotate([90, 0, 0]).translate([0, y1, 0])


def rounded_rect_xz(cx, cz, w, h, r):
    cs = CrossSection.square([w - 2 * r, h - 2 * r]).translate([-(w - 2 * r) / 2, -(h - 2 * r) / 2])
    cs = cs.offset(r, join_type=__import__("manifold3d").JoinType.Round, circular_segments=SEG)
    return cs.translate([cx, cz])


def slab_xz(cs, y0, y1):
    if cs.is_empty():
        return Manifold()
    return cs.extrude(y1 - y0).rotate([90, 0, 0]).translate([0, y1, 0])


cx = BW / 2
screen_cz = POCKET_Z + H / 2 + a.screen_up

# ---------------------------------------------------------------- the red body: walls and gable
outline = [(0, Z0), (BW, Z0), (BW, EAVES), (cx, PEAK), (0, EAVES)]
body = front_profile(outline, 0, BD)

# the pocket for the board, open at the back (above the back-door recess)
body -= box(SIDE, FRONT, POCKET_Z, SIDE + W, BD + 1, POCKET_Z + H)
# the window onto the screen, with a 45-degree bevel so the edge doesn't shade it
win_w, win_h = a.screen_w + 1.6, a.screen_h + 1.6
for i, inset in enumerate(np.linspace(0, FRONT, 5)):
    grow = (FRONT - inset)  # wider at the face
    cs = rounded_rect_xz(cx, screen_cz, win_w + grow * 1.2, win_h + grow * 1.2, 3 + grow * 0.6)
    body -= slab_xz(cs, inset - 0.01, inset + FRONT / 4 + 0.02)
# the cellar under the board: the USB-C plug goes in from below, the cable out of the back
plug = box(cx - 7.5, FRONT + D / 2 - 4.5, POCKET_Z - 0.5, cx + 7.5, FRONT + D / 2 + 4.5, POCKET_Z + 0.5)
cellar = box(cx - 10, FRONT + 1.2, Z0 + 1.2, cx + 10, BD + 1, POCKET_Z - 1.6)
body -= cellar
body -= box(cx - 7.5, FRONT + D / 2 - 4.5, POCKET_Z - 2, cx + 7.5, FRONT + D / 2 + 4.5, POCKET_Z + 0.5)
# finger windows on both sides for the PWR and BOOT buttons
for x0, x1 in ((-1, SIDE + 0.5), (SIDE + W - 0.5, BW + 1)):
    body -= box(x0, FRONT + 1.0, POCKET_Z + H * 0.22, x1, BD - BACK - 0.6, POCKET_Z + H * 0.78)
# the back door's recess and its four peg holes
body -= box(SIDE - 3, BD - BACK, POCKET_Z - 3, SIDE + W + 3, BD + 1, POCKET_Z + H + 2.5)
PEGS = [(SIDE - 1.6, POCKET_Z + 4), (SIDE + W + 1.6, POCKET_Z + 4), (SIDE - 1.6, POCKET_Z + H - 4),
        (SIDE + W + 1.6, POCKET_Z + H - 4)]
for px, pz in PEGS:
    body -= Manifold.cylinder(5, 1.0, 1.0, SEG).rotate([-90, 0, 0]).translate([px, BD - BACK - 4.5, pz])

# ---------------------------------------------------------------- white trim (set into the red, 0.8 proud)
T_IN, T_OUT = 0.6, 0.8
trim = Manifold()
frame_out = rounded_rect_xz(cx, screen_cz, win_w + FRONT * 1.2 + 6, win_h + FRONT * 1.2 + 6, 4.5)
frame_in = rounded_rect_xz(cx, screen_cz, win_w + FRONT * 1.2, win_h + FRONT * 1.2, 3 + FRONT * 0.6)
trim += slab_xz(frame_out - frame_in, -T_OUT, T_IN)
for x in (0.8, BW - 0.8):  # corner boards
    trim += box(x - 2.2, -T_OUT, Z0, x + 2.2, T_IN, EAVES)
trim += box(0, -T_OUT, EAVES - 3.2, BW, T_IN, EAVES)  # the eave board
# the hayloft door with its X brace, up in the gable
hl_w, hl_h = BW * 0.26, (PEAK - EAVES) * 0.5
hl_z = EAVES + (PEAK - EAVES) * 0.16
hl_out = CrossSection.square([hl_w + 4, hl_h + 4]).translate([cx - hl_w / 2 - 2, hl_z - 2])
hl_in = CrossSection.square([hl_w, hl_h]).translate([cx - hl_w / 2, hl_z])
trim += slab_xz(hl_out - hl_in, -T_OUT, T_IN)


def bar(x0, z0, x1, z1, wdt):
    v = np.array([x1 - x0, z1 - z0]); n = np.array([-v[1], v[0]]) / np.linalg.norm(v) * wdt / 2
    return CrossSection([[(x0 + n[0], z0 + n[1]), (x1 + n[0], z1 + n[1]), (x1 - n[0], z1 - n[1]), (x0 - n[0], z0 - n[1])]],
                        FillRule.EvenOdd)


x_brace = (bar(cx - hl_w / 2, hl_z, cx + hl_w / 2, hl_z + hl_h, 1.8) + bar(cx - hl_w / 2, hl_z + hl_h, cx + hl_w / 2, hl_z, 1.8)) ^ CrossSection.square([hl_w, hl_h]).translate([cx - hl_w / 2, hl_z])
trim += slab_xz(x_brace, -T_OUT, T_IN)
# X braces on the walls beside the window, like the big barn doors
gap = (BW - (win_w + FRONT * 1.2 + 6)) / 2
if gap > 4:
    for x0 in (2.8, BW - 2.8 - (gap - 4.6)):
        wdt = gap - 4.6
        if wdt > 2.5:
            z0, z1 = POCKET_Z + 2, POCKET_Z + H - 2
            xb = (bar(x0, z0, x0 + wdt, z1, 1.3) + bar(x0, z1, x0 + wdt, z0, 1.3)) ^ CrossSection.square([wdt, z1 - z0]).translate([x0, z0])
            trim += slab_xz(xb, -T_OUT, T_IN)
# a cellar door below the window: a framed panel
cd_w, cd_h = BW * 0.5, CELLAR - 5
cd = CrossSection.square([cd_w + 3, cd_h + 3]).translate([cx - cd_w / 2 - 1.5, Z0 + 2]) - CrossSection.square([cd_w, cd_h]).translate([cx - cd_w / 2, Z0 + 3.5])
trim += slab_xz(cd, -T_OUT, T_IN)
trim += slab_xz(bar(cx - cd_w / 2, Z0 + 3.5, cx + cd_w / 2, Z0 + 3.5 + cd_h, 1.3) ^ CrossSection.square([cd_w, cd_h]).translate([cx - cd_w / 2, Z0 + 3.5]), -T_OUT, T_IN)
trim = trim - box(-50, FRONT, -50, BW + 50, BD + 50, PEAK + 50)  # front only
body -= trim

# ---------------------------------------------------------------- the roof: two dark slabs with an overhang
OVER, RT = 3.0, 2.6
roof = Manifold()
for side in (-1, 1):
    ex, ez = (0 if side < 0 else BW), EAVES
    vx, vz = cx - ex, PEAK - ez
    L = float(np.hypot(vx, vz)); ux, uz = vx / L, vz / L
    nx, nz = (-uz, ux) if side < 0 else (uz, -ux)  # outward normal
    sx, sz = ex - ux * OVER, ez - uz * OVER
    tx, tz = cx + ux * 0.0, PEAK
    poly = [(sx, sz), (tx, tz), (tx + nx * RT, tz + nz * RT), (sx + nx * RT, sz + nz * RT)]
    roof += front_profile(poly, -OVER * 0.6, BD + OVER * 0.6)
roof += front_profile([(cx - 2.2, PEAK - 1), (cx + 2.2, PEAK - 1), (cx + 2.2, PEAK + RT + 0.8), (cx - 2.2, PEAK + RT + 0.8)], -OVER * 0.6, BD + OVER * 0.6)  # ridge
roof = roof - body  # where they overlap, the wall stays red

# ---------------------------------------------------------------- the green base, with the sign on its front
BX, BYF, BYB = 7.0, 7.0, 9.0
base = box(-BX, -BYF, 0, BW + BX, BD + BYB, BASE_T)
base = base - box(cx - 10, FRONT + 1.2, BASE_T - 0.01, cx + 10, BD + BYB + 1, BASE_T + 1)  # (the cellar floor is the base)
base += box(cx - 10, FRONT + 1.2, BASE_T - 0.6, cx + 10, BD + BYB + 1, BASE_T)  # keep it closed: a floor 0.6 lower
sign = Manifold()
if a.sign:
    from matplotlib.textpath import TextPath
    from matplotlib.font_manager import FontProperties
    tp = TextPath((0, 0), a.sign.upper(), size=4.2, prop=FontProperties(family="DejaVu Sans", weight="bold"))
    polys = [p for p in tp.to_polygons() if len(p) >= 3]
    cs = CrossSection([[(float(x), float(y)) for x, y in p] for p in polys], FillRule.EvenOdd)
    bb = cs.bounds()
    tw = bb[2] - bb[0]
    if tw > BW + 2 * BX - 6:
        cs = cs.scale([(BW + 2 * BX - 6) / tw, (BW + 2 * BX - 6) / tw]); bb = cs.bounds(); tw = bb[2] - bb[0]
    th = bb[3] - bb[1]
    cs = cs.translate([cx - tw / 2 - bb[0], (BASE_T - th) / 2 - bb[1]])
    sign = slab_xz(cs, -BYF - 0.6, -BYF + 0.6)
    base -= sign

# ---------------------------------------------------------------- the back door (printed flat, separately)
DW, DH = W + 6 - 0.4, H + 5.5 - 0.4
door = box(0, 0, 0, DW, DH, BACK - 0.2)
for px, pz in PEGS:
    door += Manifold.cylinder(4.4, 0.85, 0.75, SEG).translate([px - (SIDE - 3 + 0.2), pz - (POCKET_Z - 3 + 0.2), BACK - 0.2])
# speaker grille, and a little horseshoe to open it by
for i in range(5):
    for j in range(4):
        door -= Manifold.cylinder(5, 0.9, 0.9, 16).translate([DW / 2 - 8 + i * 4, DH / 2 - 6 + j * 4, -1])
door -= Manifold.cylinder(1.2, 6, 6, SEG).translate([DW / 2, DH - 5, -0.6]) - Manifold.cylinder(2, 3.6, 3.6, SEG).translate([DW / 2, DH - 5, -1]) - box(DW / 2 - 7, DH - 5 - 7, -2, DW / 2 + 7, DH - 5 - 2.5, 3)

# ---------------------------------------------------------------- write it out
os.makedirs(a.out, exist_ok=True)


def save(m, name):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts, process=False)  # (merging breaks thin bits)
    t.export(os.path.join(a.out, name))
    return t


parts = {
    "barn-1-walls-red.stl": body,
    "barn-2-roof-dark.stl": roof,
    "barn-3-trim-white.stl": trim,
    "barn-4-base-green.stl": base,
    "barn-5-sign-white.stl": sign,
}
meshes = {}
for name, m in parts.items():
    if m.is_empty():
        continue
    meshes[name] = save(m, name)
door_mesh = save(door, "barn-back-door.stl")
size = (BW + 2 * BX, BD + BYF + BYB, PEAK + RT + 0.8)
print(f"barn {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} mm (w x d x h); door {DW:.1f} x {DH:.1f} x {BACK - 0.2:.1f} mm")
for name, t in list(meshes.items()) + [("barn-back-door.stl", door_mesh)]:
    print(f"  {name:26s} watertight={t.is_watertight}  {t.volume / 1000:.1f} cm3")
