#!/usr/bin/env python3
"""Turns the Visor's VisorMeshShots database into PNGs.

    tools/shots.py build/VisorMeshShots.pdb [out_dir]   (default: build/shots)

`make shots` fetches the database over HotSync and runs this. Each record is a
ShotHeader (src/shots.h, big-endian) followed by the screen bits, rows of
rowBytes, pixels packed most significant bits first, 0 = white. Files are
named by the capture time (the Visor's local clock); existing ones are kept.
"""
import datetime
import os
import struct
import sys
import zlib

HEADER = struct.Struct(">HHHBBI")
PALM_EPOCH = datetime.datetime(1904, 1, 1)


def records(pdb):
    count = struct.unpack_from(">H", pdb, 76)[0]
    offsets = [struct.unpack_from(">I", pdb, 78 + 8 * i)[0] for i in range(count)]
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < count else len(pdb)
        yield pdb[start:end]


def gray_rows(rec):
    width, height, row_bytes, depth, _, when = HEADER.unpack_from(rec)
    if depth not in (1, 2, 4, 8):
        raise ValueError(f"unsupported depth {depth}")
    bits = rec[HEADER.size:]
    top = (1 << depth) - 1
    rows = []
    for y in range(height):
        row = bits[y * row_bytes:(y + 1) * row_bytes]
        out = bytearray()
        for x in range(width):
            bit = x * depth
            v = (row[bit // 8] >> (8 - depth - bit % 8)) & top
            out.append(255 - v * 255 // top)   # 8-bit is palette-indexed: this is only an approximation
        rows.append(bytes(out))
    return width, height, when, rows


def png(width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\0" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    out_dir = sys.argv[2] if len(sys.argv) == 3 else "build/shots"
    os.makedirs(out_dir, exist_ok=True)
    with open(sys.argv[1], "rb") as f:
        pdb = f.read()
    written = 0
    seen = {}
    for rec in records(pdb):
        width, height, when, rows = gray_rows(rec)
        stamp = (PALM_EPOCH + datetime.timedelta(seconds=when)).strftime("%Y%m%d-%H%M%S")
        n = seen.get(stamp, 0)            # two in one second: -1, -2, ... in record order
        seen[stamp] = n + 1
        name = os.path.join(out_dir, f"visormesh-{stamp}{f'-{n}' if n else ''}.png")
        if os.path.exists(name):
            continue                      # converted on an earlier run
        with open(name, "wb") as f:
            f.write(png(width, height, rows))
        written += 1
        print(name)
    print(f"{written} screenshot(s) in {out_dir}")


if __name__ == "__main__":
    main()
