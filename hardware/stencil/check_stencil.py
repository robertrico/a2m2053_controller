#!/usr/bin/env python3
"""Predict where the paste apertures sit on the stencil sheet, so the real
stencil can be checked with calipers before the fixture is printed.

stencilframer only reads the board outline and the four margins you give
it. It never looks at the paste layer, so it cannot tell you whether the
fixture will land the apertures on the pads. This script does that half:
it reads the F_Paste and Edge_Cuts Gerbers, places the sheet around the
outline with the given margins, and prints, for the aperture nearest each
sheet edge, the expected distance from the aperture's outer edge to that
sheet edge. Measure the same four distances on the stencil. If they match,
print the fixture with those margins. If not, the differences are the
margin corrections.

It also writes an SVG of sheet, outline and apertures with the four
dimensions drawn, viewed from the top side as in KiCad (Y down).

Usage: check_stencil.py [-l L] [-r R] [-t T] [-b B] [--svg out.svg]
Margins are in mm, top/bottom as seen in KiCad's board view.
"""
import argparse
import re
import sys

PASTE = "../kicad/fabrication/a2m2053-F_Paste.gbr"
EDGE = "../kicad/fabrication/a2m2053-Edge_Cuts.gbr"


def read_apertures(lines):
    """Aperture code -> (w, h) bounding box in mm. R, C, O and KiCad's
    RoundRect macro are enough for this board."""
    ap = {}
    for ln in lines:
        m = re.match(r"%ADD(\d+)([A-Za-z]+),([0-9.X\-]+)\*%", ln)
        if not m:
            continue
        code, kind, params = int(m.group(1)), m.group(2), [float(x) for x in m.group(3).split("X")]
        if kind in ("R", "O"):
            ap[code] = (params[0], params[1])
        elif kind == "C":
            ap[code] = (params[0], params[0])
        elif kind == "RoundRect":
            r, pts = params[0], params[1:9]
            xs, ys = pts[0::2], pts[1::2]
            ap[code] = (max(xs) - min(xs) + 2 * r, max(ys) - min(ys) + 2 * r)
        else:
            sys.exit("unsupported aperture %s" % ln)
    return ap


def read_flashes(path):
    lines = [l.strip() for l in open(path)]
    fs = next(l for l in lines if l.startswith("%FSLA"))
    dec = int(re.search(r"X\d(\d)", fs).group(1))
    scale = 10 ** dec
    ap = read_apertures(lines)
    cur, ref, out = None, "", []
    for ln in lines:
        if ln.startswith("%TO.C,"):
            ref = ln[6:-2]
        elif re.match(r"D\d+\*$", ln):
            cur = int(ln[1:-1])
        elif ln.endswith("D03*"):
            x, y = (int(v) / scale for v in re.match(r"X(-?\d+)Y(-?\d+)", ln).groups())
            w, h = ap[cur]
            # Gerber Y is up; KiCad's board view is Y down. Flip here so
            # everything below is in KiCad board coordinates.
            out.append({"ref": ref, "x": x, "y": -y, "w": w, "h": h})
    return out


def read_outline(path):
    lines = [l.strip() for l in open(path)]
    fs = next(l for l in lines if l.startswith("%FSLA"))
    scale = 10 ** int(re.search(r"X\d(\d)", fs).group(1))
    # list of polylines; a D02 (move) starts a new one, D01 (draw) extends it
    polys, cur = [], []
    for ln in lines:
        m = re.match(r"X(-?\d+)Y(-?\d+)D0([12])\*", ln)
        if not m:
            continue
        pt = (int(m.group(1)) / scale, -int(m.group(2)) / scale)
        if m.group(3) == "2":
            if len(cur) > 1:
                polys.append(cur)
            cur = [pt]
        else:
            cur.append(pt)
    if len(cur) > 1:
        polys.append(cur)
    return polys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-l", type=float, default=30, help="left margin, mm")
    p.add_argument("-r", type=float, default=30, help="right margin, mm")
    p.add_argument("-t", type=float, default=30, help="top margin (KiCad view), mm")
    p.add_argument("-b", type=float, default=30, help="bottom margin (KiCad view), mm")
    p.add_argument("--svg", default="check_stencil.svg", help="output drawing")
    a = p.parse_args()

    flashes = read_flashes(PASTE)
    polys = read_outline(EDGE)
    outline = [pt for pl in polys for pt in pl]
    bx0, bx1 = min(x for x, _ in outline), max(x for x, _ in outline)
    by0, by1 = min(y for _, y in outline), max(y for _, y in outline)
    sx0, sx1 = bx0 - a.l, bx1 + a.r
    sy0, sy1 = by0 - a.t, by1 + a.b

    print("board   %.2f x %.2f mm" % (bx1 - bx0, by1 - by0))
    print("sheet   %.2f x %.2f mm (margins L %.2f R %.2f T %.2f B %.2f)" % (sx1 - sx0, sy1 - sy0, a.l, a.r, a.t, a.b))
    print("%d paste apertures\n" % len(flashes))

    # aperture nearest each sheet edge; distance from aperture OUTER edge to sheet edge
    refs = {
        "left":   min(flashes, key=lambda f: f["x"] - f["w"] / 2),
        "right":  max(flashes, key=lambda f: f["x"] + f["w"] / 2),
        "top":    min(flashes, key=lambda f: f["y"] - f["h"] / 2),
        "bottom": max(flashes, key=lambda f: f["y"] + f["h"] / 2),
    }
    dims = {}
    for side, f in refs.items():
        if side == "left":
            d = (f["x"] - f["w"] / 2) - sx0
        elif side == "right":
            d = sx1 - (f["x"] + f["w"] / 2)
        elif side == "top":
            d = (f["y"] - f["h"] / 2) - sy0
        else:
            d = sy1 - (f["y"] + f["h"] / 2)
        dims[side] = d
        print("%-7s %-4s aperture %.2f x %.2f mm at (%.2f, %.2f): outer edge to %s sheet edge = %.2f mm"
              % (side, f["ref"], f["w"], f["h"], f["x"], f["y"], side, d))
    print("\nMeasure these four on the stencil. Expected minus measured = margin correction for that side.")

    # SVG, KiCad view (Y down), 1 mm = 4 px
    S = 4
    def X(v): return (v - sx0 + 5) * S
    def Y(v): return (v - sy0 + 5) * S
    W, H = (sx1 - sx0 + 10) * S, (sy1 - sy0 + 10) * S
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" font-family="sans-serif" font-size="11">' % (W, H, W, H),
           '<rect width="100%" height="100%" fill="white"/>',
           '<rect x="%g" y="%g" width="%g" height="%g" fill="#eee" stroke="#444"/>' % (X(sx0), Y(sy0), (sx1 - sx0) * S, (sy1 - sy0) * S),
           ]
    for pl in polys:
        svg.append('<polyline points="%s" fill="none" stroke="#06c" stroke-width="1"/>' % " ".join("%g,%g" % (X(x), Y(y)) for x, y in pl))
    for f in flashes:
        svg.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#c33"/>' % (X(f["x"] - f["w"] / 2), Y(f["y"] - f["h"] / 2), f["w"] * S, f["h"] * S))
    for side, f in refs.items():
        d = dims[side]
        if side == "left":
            x1, y1, x2, y2 = X(sx0), Y(f["y"]), X(f["x"] - f["w"] / 2), Y(f["y"])
        elif side == "right":
            x1, y1, x2, y2 = X(f["x"] + f["w"] / 2), Y(f["y"]), X(sx1), Y(f["y"])
        elif side == "top":
            x1, y1, x2, y2 = X(f["x"]), Y(sy0), X(f["x"]), Y(f["y"] - f["h"] / 2)
        else:
            x1, y1, x2, y2 = X(f["x"]), Y(f["y"] + f["h"] / 2), X(f["x"]), Y(sy1)
        svg.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#080" stroke-width="1"/>' % (x1, y1, x2, y2))
        svg.append('<text x="%g" y="%g" fill="#080">%s %.2f</text>' % ((x1 + x2) / 2 + 3, (y1 + y2) / 2 - 3, f["ref"], d))
    svg.append('<text x="%g" y="%g" fill="#444">sheet %.1f x %.1f mm, top side, as in KiCad (Y down)</text>' % (X(sx0), Y(sy0) - 6, sx1 - sx0, sy1 - sy0))
    svg.append("</svg>")
    open(a.svg, "w").write("\n".join(svg))
    print("drawing: %s" % a.svg)


if __name__ == "__main__":
    main()
