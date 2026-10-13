"""Draw the A2M2053 board outline on Edge.Cuts in the open PCB editor.

Component-side view (photo was solder side, so mirrored left-right).
Replaces every existing Edge.Cuts shape. Needs the KiCad API server on
(Preferences > Plugins) and kicad-python (`pip install kicad-python`).
"""
import math
from kipy import KiCad
from kipy.board_types import BoardSegment, BoardArc, BoardBezier, BoardCircle, BoardLayer
from kipy.geometry import Vector2

X0, Y0 = 100.0, 50.0      # top-left of the full 100 mm body, at the 94 mm section top
W, H = 100.0, 105.0       # body width; height from bottom to top of 94 mm section
INSET = 3.0               # 94 mm section (calipers), centered
STEP_L, STEP_R = 8.0, 13.0  # depth of the 94 mm section on each side (test print); the steps differ
TAB_X, TAB_W = INSET + 22.0, 15.5  # tab left edge 22 mm from the 94 mm section edge, width (calipers)
TAB_H = 5.0               # tab height above the 94 mm top (Rico)
COVE_H = 1.8              # step: quarter-ellipse cove, this tall, as wide as the step
RS = 0.5                  # outside step corner radius
RC = 1.0                  # bottom corner radius (the edge away from the tab)
LW = 0.1
HOLE_D = 3.2              # M3 mounting holes; original measures about 3.35 (calipers)

K = 0.5523                # cubic Bezier quarter-ellipse constant
S45 = math.sqrt(0.5)
L, Rt, B = X0, X0 + W, Y0 + H
YL, YR = Y0 + STEP_L, Y0 + STEP_R  # step lines
A = INSET - RS            # cove width

holes = [  # centers
    (Rt - 1.5 - HOLE_D / 2, Y0 + 17.0),  # right side: edge 1.5 mm in, center 17 mm below the 94 mm top (test print)
    (L + INSET + 40.0, Y0 + 3.0 + HOLE_D / 2),  # right of the tab: center 40 mm from the 94 mm section's left edge, edge 3 mm below its top
]

start = (L + INSET, Y0)
path = [  # ("L", end) | ("A", mid, end) | ("B", c1, c2, end), clockwise from start
    ("L", (L + TAB_X, Y0)), ("L", (L + TAB_X, Y0 - TAB_H)),
    ("L", (L + TAB_X + TAB_W, Y0 - TAB_H)), ("L", (L + TAB_X + TAB_W, Y0)),
    ("L", (Rt - INSET, Y0)),
    ("L", (Rt - INSET, YR - COVE_H)),
    ("B", (Rt - INSET, YR - COVE_H + K * COVE_H), (Rt - RS - K * A, YR), (Rt - RS, YR)),
    ("A", (Rt - RS + RS * S45, YR + RS - RS * S45), (Rt, YR + RS)),
    ("L", (Rt, B - RC)),
    ("A", (Rt - RC + RC * S45, B - RC + RC * S45), (Rt - RC, B)),
    ("L", (L + RC, B)),
    ("A", (L + RC - RC * S45, B - RC + RC * S45), (L, B - RC)),
    ("L", (L, YL + RS)),
    ("A", (L + RS - RS * S45, YL + RS - RS * S45), (L + RS, YL)),
    ("B", (L + RS + K * A, YL), (L + INSET, YL - COVE_H + K * COVE_H), (L + INSET, YL - COVE_H)),
    ("L", start),
]


def v(p):
    return Vector2.from_xy_mm(p[0], p[1])


items = []
cur = start
for kind, *p in path:
    if kind == "L":
        it = BoardSegment()
        it.start, it.end = v(cur), v(p[0])
    elif kind == "A":
        it = BoardArc()
        it.start, it.mid, it.end = v(cur), v(p[0]), v(p[1])
    else:
        it = BoardBezier()
        it.start, it.control1, it.control2, it.end = v(cur), v(p[0]), v(p[1]), v(p[2])
    items.append(it)
    cur = p[-1]

for h in holes:
    c = BoardCircle()
    c.center = v(h)
    c.radius_point = v((h[0] + HOLE_D / 2, h[1]))
    items.append(c)

for it in items:
    it.layer = BoardLayer.BL_Edge_Cuts
    it.attributes.stroke.width = int(LW * 1e6)

board = KiCad().get_board()
old = [s for s in board.get_shapes() if s.layer == BoardLayer.BL_Edge_Cuts]
commit = board.begin_commit()
if old:
    board.remove_items(old)
board.create_items(items)
board.push_commit(commit, "Board outline")
print(f"removed {len(old)}, drew {len(items)} Edge.Cuts items on {board.name}")
