"""Replay mock_rift.lua's recorded screens with cairo and norns' own font (as matron draws them),
check them, and write previews.
    python3 ignore/dev/test/render_screens.py /tmp/screens.tsv OUTDIR [frame names for the sheet...]

Checks, on every frame:
  - no text leaves the 128x64 screen;
  - no two texts overlap;
  - a text inside a drawn box has at least 1 px clear on every side; a text never crosses a
    box's edge.
"""
import math
import os
import sys

import cairo

LEVELS = [i / 15 for i in range(16)]


def frames(path):
    cur, name = [], None
    for line in open(path):
        p = line.rstrip("\n").split("\t")
        if p[0] == "frame":
            if name:
                yield name, cur
            name, cur = p[1], []
        else:
            cur.append(p)
    if name:
        yield name, cur


def num(x):
    return float(x)


def replay(ops, scale=1):
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 128 * scale, 64 * scale)
    cr = cairo.Context(s)
    cr.scale(scale, scale)
    cr.set_source_rgb(0, 0, 0)
    cr.paint()
    cr.select_font_face("norns")
    cr.set_font_size(8)
    fo = cairo.FontOptions()
    fo.set_antialias(cairo.ANTIALIAS_GRAY)
    cr.set_font_options(fo)
    texts, boxes = [], []
    path_rects = []
    for op in ops:
        k, a = op[0], op[1:]
        if k == "level":
            v = LEVELS[max(0, min(15, int(float(a[0]))))]
            cr.set_source_rgb(v, v, v)
        elif k == "move":
            cr.move_to(num(a[0]), num(a[1]))
        elif k == "line":
            cr.line_to(num(a[0]), num(a[1]))
        elif k == "rect":
            x, y, w, h = map(num, a)
            cr.rectangle(x, y, w, h)
            path_rects.append((x, y, w, h))
        elif k == "arc":
            cr.arc(*map(num, a))
        elif k == "curve":
            cr.curve_to(*map(num, a))
        elif k == "close":
            cr.close_path()
        elif k == "stroke":
            for r in path_rects:
                boxes.append(r)
            path_rects = []
            cr.stroke()
        elif k == "fill":
            path_rects = []
            cr.fill()
        elif k == "line_width":
            cr.set_line_width(num(a[0]))
        elif k in ("text", "text_center", "text_right"):
            t = a[0] if a else ""
            e = cr.text_extents(t)
            x, y = cr.get_current_point()
            if k == "text_center":
                cr.rel_move_to(-e.width * 0.5, 0)
            elif k == "text_right":
                cr.rel_move_to(-e.width, 0)
            x, y = cr.get_current_point()
            if t.strip():
                texts.append((t, x + e.x_bearing, y + e.y_bearing, e.width, e.height))
            cr.show_text(t)
    return s, texts, boxes


def check(name, texts, boxes):
    bad = []
    for t, x, y, w, h in texts:
        if x < -0.01 or y < -0.01 or x + w > 128.01 or y + h > 64.01:
            bad.append(f"{name}: '{t}' leaves the screen ({x:.1f},{y:.1f},{w:.1f}x{h:.1f})")
    for i in range(len(texts)):
        for j in range(i + 1, len(texts)):
            a, b = texts[i], texts[j]
            if a[1] < b[1] + b[3] and b[1] < a[1] + a[3] and a[2] < b[2] + b[4] and b[2] < a[2] + a[4]:
                bad.append(f"{name}: '{a[0]}' overlaps '{b[0]}'")
    for t, x, y, w, h in texts:
        for bx, by, bw, bh in boxes:
            # the box's stroked edge: its rect, the stroke centred on it
            inside = x >= bx and y >= by and x + w <= bx + bw and y + h <= by + bh
            touches = x < bx + bw + 0.5 and bx - 0.5 < x + w and y < by + bh + 0.5 and by - 0.5 < y + h
            if inside:
                if x - bx < 1.5 or y - by < 1.5 or bx + bw - (x + w) < 1.5 or by + bh - (y + h) < 1.5:
                    bad.append(f"{name}: '{t}' touches its box's edge")
            elif touches:
                bad.append(f"{name}: '{t}' crosses a box at ({bx},{by},{bw}x{bh})")
    return bad


def main(src, outdir, sheet_names):
    os.makedirs(outdir, exist_ok=True)
    allbad, n = [], 0
    keep = {}
    for name, ops in frames(src):
        n += 1
        s, texts, boxes = replay(ops)
        allbad += check(name, texts, boxes)
        if name in sheet_names:
            keep[name] = ops
    for b in allbad[:40]:
        print("FAIL", b)
    print(f"{n} screens checked, {len(allbad)} problems")
    # the sheet: each named frame at 4x, labelled
    if keep:
        sc, pad, lab = 4, 8, 14
        cols = 3
        rows = math.ceil(len(sheet_names) / cols)
        W = cols * (128 * sc + pad) + pad
        H = rows * (64 * sc + pad + lab) + pad
        sheet = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
        cr = cairo.Context(sheet)
        cr.set_source_rgb(0.12, 0.12, 0.12)
        cr.paint()
        for i, nm in enumerate(sheet_names):
            if nm not in keep:
                continue
            img, _, _ = replay(keep[nm], sc)
            x = pad + (i % cols) * (128 * sc + pad)
            y = pad + (i // cols) * (64 * sc + pad + lab)
            cr.set_source_rgb(0.75, 0.75, 0.75)
            cr.select_font_face("sans")
            cr.set_font_size(11)
            cr.move_to(x, y + 10)
            cr.show_text(nm)
            cr.set_source_surface(img, x, y + lab)
            cr.paint()
        sheet.write_to_png(os.path.join(outdir, "screens.png"))
        print("wrote", os.path.join(outdir, "screens.png"))
    return len(allbad)


if __name__ == "__main__":
    sys.exit(1 if main(sys.argv[1], sys.argv[2], sys.argv[3:]) else 0)
