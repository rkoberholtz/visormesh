#!/usr/bin/env python3
"""Pixel art for the VisorMesh launcher icons.

Drawn at the device size -- 22x22 for the launcher's large icon, 15x9 for the
list view -- on the Visor Neo's 16 gray levels, one grid cell per screen pixel.
Nothing here is scaled down from the 512px artwork: at 22px a downscale turns
into gray mush, which is what tools/make_icon.sh --art exists to avoid.

    python3 tools/gen_icon_art.py             # write resources/icon/src/*.png
    python3 tools/gen_icon_art.py --show      # print the pixel grids
    tools/make_icon.sh --art resources/icon/src/large22.png \
                           resources/icon/src/small15x9.png

Levels are 0..15 (n*17 = the 8-bit gray). Keep anything reversed out at 14 (238)
or below: white 255 is the app's transparency key (ICONFAMILY ... TRANSPARENT
255 255 255), so a 255 pixel punches a hole through the icon instead of drawing.
"""
import subprocess
import sys
from typing import Optional

OUT = "resources/icon/src"

TRANSPARENT: Optional[int] = None
Pixel = Optional[int]
Grid = list[list[Pixel]]


def level(n: int) -> int:
    return n * 17                            # 0..15 -> 8-bit gray

# ---- the art -------------------------------------------------------------

# Crown outline: per-column top row of the hair. Four chunky spikes ~5px apart
# with 4px notches -- any thinner and the teeth stop reading at 22px.
TOP22 = [6, 4, 2, 3, 4, 6, 4, 1, 3, 5, 6, 4, 2, 3, 5, 6, 4, 2, 4, 5, 6, 6]
TOP15 = [2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1]

M = ["ff...ff", "fff.fff", "ff.f.ff", "ff.f.ff",
     "ff...ff", "ff...ff", "ff...ff"]
C = [".ffff.", "ff..ff", "ff....", "ff....",
     "ff....", "ff..ff", ".ffff."]


def grid(w, h) -> Grid:
    return [[TRANSPARENT] * w for _ in range(h)]


def put(g: Grid, x: int, y: int, v: Pixel) -> None:
    if 0 <= y < len(g) and 0 <= x < len(g[0]):
        g[y][x] = v


def large() -> Grid:
    """22x22: hair crown, visor band, dark square, reversed MC."""
    g = grid(22, 22)

    # dark square (behind everything), corners knocked off to round them
    for y in range(9, 22):
        for x in range(2, 20):
            put(g, x, y, 1)
    for corner in ((2, 9), (19, 9), (2, 21), (19, 21)):
        put(g, *corner, TRANSPARENT)

    # hair: light fill, level 6 only where the crown meets the background
    for x in range(22):
        for y in range(TOP22[x], 8):
            put(g, x, y, 12)
    for x in range(22):
        for y in range(TOP22[x], 8):
            if g[y][x] != 12:
                continue
            above = g[y - 1][x] if y > 0 else TRANSPARENT
            left = g[y][x - 1] if x > 0 else TRANSPARENT
            right = g[y][x + 1] if x < 21 else TRANSPARENT
            if TRANSPARENT in (above, left, right):
                g[y][x] = 6

    # band: highlight / face / shadow, rounded ends; sits in front of both
    for x in range(22):
        if x in (0, 21):
            put(g, x, 9, 3)
        else:
            put(g, x, 8, 7)
            put(g, x, 9, 5)
            put(g, x, 10, 3)

    # MC on 2px stems: the thinnest weight that survives 1:1
    for i, row in enumerate(M):
        for j, ch in enumerate(row):
            if ch == "f":
                put(g, 4 + j, 13 + i, 14)
    for i, row in enumerate(C):
        for j, ch in enumerate(row):
            if ch == "f":
                put(g, 12 + j, 13 + i, 14)
    return g


def small() -> Grid:
    """15x9: silhouette only -- no lettering survives at this size."""
    g = grid(15, 9)
    for y in range(4, 9):
        for x in range(1, 14):
            put(g, x, y, 1)
    for corner in ((1, 4), (13, 4), (1, 8), (13, 8)):
        put(g, *corner, TRANSPARENT)
    for x in range(15):
        for y in range(TOP15[x], 3):
            put(g, x, y, 12 if y > TOP15[x] else 6)
    for x in range(15):
        put(g, x, 3, 5)
    return g


# ---- plumbing ------------------------------------------------------------

def write_pgm(g, path):
    rows = "\n".join(" ".join("255" if v is TRANSPARENT else str(level(v))
                              for v in row) for row in g)
    open(path, "w").write(f"P2\n{len(g[0])} {len(g)}\n255\n{rows}\n")


def write_png(g, path):
    """White pixels become transparent, so the padding is see-through, not a box."""
    pgm = path + ".pgm"
    write_pgm(g, pgm)
    subprocess.run(["magick", pgm, "-transparent", "white", "-depth", "4", path],
                   check=True)
    subprocess.run(["rm", "-f", pgm], check=True)


def show(g):
    sym = "0123456789abcdef"
    for row in g:
        print("".join("." if v is TRANSPARENT else sym[v] for v in row))


if __name__ == "__main__":
    if "--show" in sys.argv:
        show(large())
        print()
        show(small())
    else:
        write_png(large(), f"{OUT}/large22.png")
        write_png(small(), f"{OUT}/small15x9.png")
        print(f"wrote {OUT}/large22.png ({len(large()[0])}x{len(large())}), "
              f"{OUT}/small15x9.png ({len(small()[0])}x{len(small())})")
