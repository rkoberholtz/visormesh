#!/usr/bin/env python3
"""Builds VisorMesh's basemap: a Palm database of 160x160 grey tiles drawn from
OpenStreetMap data, for the Map screen to plot nodes on.

    tools/make_map.py --cache DIR [--fetch] [--bpp 2|4] [--min-zoom 10] [--max-zoom 13]
                      [--bbox S,W,N,E] [--detail-bbox S,W,N,E]
                      [--out build/VisorMeshMap.pdb] [--preview DIR]

  --fetch      download the OSM data for the bbox from the Overpass API into the
               cache (one query per layer; skip it to reuse the cache)
  --detail-bbox the area the max (street-level) zoom covers, with residential
               streets; the other zooms cover --bbox without them. Residential
               streets are most of the size, so this sets how big the file is.
  --preview    also write each zoom as a PNG, to look at the result

Data (c) OpenStreetMap contributors, ODbL. The tiles are a Produced Work: the
Map screen shows the attribution stored in the header.

Tiles use Web Mercator at the standard zooms (256-pixel world at zoom 0), cut
into 160-pixel squares. Each tile is PackBits-compressed rows of 1, 2 or 4 bit
pixels (MSB first; 0 = white); an all-white tile is a 1-byte record.

Database layout (big-endian, read by src/map_db.c):
  record 0: header
    'VMap', version u16, tile size u16, bpp u8, zoom count u8,
    bbox lat S, lon W, lat N, lon E (i32, degrees * 1e6),
    table step (u32, degrees * 1e6), lat count u16, lon count u16,
    attribution (32 bytes, NUL-padded),
    per zoom: zoom u8, pad u8, cols u16, rows u16, first record u16,
              origin x i32, origin y i32 (world pixels at that zoom of tile 0,0)
    lat table: world y at the max zoom for lat S + i * step (i32 each)
    lon table: world x at the max zoom for lon W + i * step (i32 each)
  records 1..: tiles, row by row, per zoom
"""
import argparse
import json
import math
import os
import struct
import sys
import time
import urllib.parse
import urllib.request
import zlib

TILE = 160
STEP_E6 = 10000                       # projection table step: 0.01 degree
DEFAULT_BBOX = (39.70, -75.49, 40.20, -74.83)   # Philadelphia metro
DEFAULT_DETAIL = (39.88, -75.26, 40.02, -75.06)  # ~16 km around Center City
OVERPASS = 'https://overpass-api.de/api/interpreter'
USER_AGENT = 'VisorMesh-maptool/0.1 (personal use)'
ATTRIBUTION = '\u00a9 OpenStreetMap'     # the short form OSM accepts on small screens; Latin-1 on the Visor

# Overpass queries per cache file; {b} is the bbox. Relations need "out geom"
# ("out tags" drops their members); ways only need tags and geometry.
LAYERS = {
    'major.json': 'way["highway"~"^(motorway|trunk|primary|secondary|tertiary)(_link)?$"]{b};out tags geom qt;',
    'minor.json': 'way["highway"~"^(residential|unclassified|living_street)$"]{b};out skel geom qt;',
    'water_ways.json': '(way["natural"="water"]{b};way["waterway"="riverbank"]{b};'
                       'way["waterway"~"^(river|canal)$"]{b};);out tags geom qt;',
    'water_rels.json': '(relation["natural"="water"]{b};relation["waterway"="riverbank"]{b};);out geom qt;',
    'parks.json': '(way["leisure"="park"]{b};relation["leisure"="park"]{b};'
                  'way["landuse"~"^(forest|cemetery)$"]{b};way["railway"="rail"][!"service"]{b};);out geom qt;',
}

# Greys on a 0 (white) .. 15 (black) scale, mapped to the output depth (at 2 bpp:
# 0-4 white, 5-9 light, 10-14 dark, 15 black). Parks are a light dot texture so
# they stay distinct from water when there are only four greys.
PARK, WATER, RAIL, MINOR, MID, MAJOR = 6, 6, 11, 11, 14, 15


def fetch(cache, bbox):
    b = '(%s)' % ','.join(str(v) for v in bbox)
    for name, q in LAYERS.items():
        path = os.path.join(cache, name)
        if os.path.exists(path) and os.path.getsize(path) > 1000:
            continue
        query = '[out:json][timeout:300];' + q.format(b=b)
        data = urllib.parse.urlencode({'data': query}).encode()
        req = urllib.request.Request(OVERPASS, data=data, headers={'User-Agent': USER_AGENT})
        print('fetching', name, '...', flush=True)
        try:
            with urllib.request.urlopen(req, timeout=400) as r, open(path, 'wb') as f:
                f.write(r.read())
        except Exception as e:                  # a busy server: carry on without the layer
            print('  failed (%s); run --fetch again later' % e)
        time.sleep(10)                          # be gentle with the public server


def load(cache, name):
    path = os.path.join(cache, name)
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        print('  (no %s: layer left out)' % name)
        return []
    with open(path) as f:
        return json.load(f).get('elements', [])


def world_xy(lat, lon, z):
    """Web Mercator world pixels at zoom z (256-pixel world at zoom 0)."""
    n = 256 * (1 << z)
    x = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n
    return x, y


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h)

    def put(self, x, y, v, width):
        r0 = -(width // 2)
        for dy in range(r0, r0 + width):
            yy = y + dy
            if 0 <= yy < self.h:
                row = yy * self.w
                for dx in range(r0, r0 + width):
                    xx = x + dx
                    if 0 <= xx < self.w:
                        self.px[row + xx] = v

    def line(self, pts, v, width=1, dash=0):
        """Polyline through integer points; dash > 0 draws dash pixels on, dash off."""
        n = 0
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            dx, dy = abs(x1 - x0), -abs(y1 - y0)
            sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
            err = dx + dy
            while True:
                if not dash or (n // dash) % 2 == 0:
                    self.put(x0, y0, v, width)
                n += 1
                if x0 == x1 and y0 == y1:
                    break
                e2 = 2 * err
                if e2 >= dy:
                    err += dy
                    x0 += sx
                if e2 <= dx:
                    err += dx
                    y0 += sy

    def fill(self, rings, v, texture=False):
        """Even-odd fill of one or more closed rings (holes come out right);
        texture: only every other pixel, in a checkerboard."""
        edges = []
        ymin, ymax = self.h, -1
        for ring in rings:
            for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
                if y0 == y1:
                    continue
                if y0 > y1:
                    x0, y0, x1, y1 = x1, y1, x0, y0
                edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0)))
                ymin, ymax = min(ymin, y0), max(ymax, y1)
        for y in range(max(0, int(ymin)), min(self.h, int(ymax) + 1)):
            yc = y + 0.5
            xs = sorted(x0 + (yc - e0) * k for e0, e1, x0, k in edges if e0 <= yc < e1)
            row = y * self.w
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(max(0, int(a + 0.5)), min(self.w, int(b + 0.5))):
                    if not texture or (x + y) % 2 == 0:
                        self.px[row + x] = v


def geom(el, project):
    return [project(p['lat'], p['lon']) for p in el.get('geometry', []) if p]


def join_rings(lines):
    """Joins open way segments end to end into closed rings (OSM multipolygons
    split a big outline, like a river, over many ways). Leftovers that never
    close are dropped."""
    rings, open_ = [], []
    for pts in lines:
        if len(pts) > 3 and pts[0] == pts[-1]:
            rings.append(pts)
        elif len(pts) > 1:
            open_.append(list(pts))
    while open_:
        cur = open_.pop()
        grown = True
        while grown and cur[0] != cur[-1]:
            grown = False
            for i, seg in enumerate(open_):
                if seg[0] == cur[-1]:
                    cur += seg[1:]
                elif seg[-1] == cur[-1]:
                    cur += seg[-2::-1]
                elif seg[-1] == cur[0]:
                    cur = seg[:-1] + cur
                elif seg[0] == cur[0]:
                    cur = seg[:0:-1] + cur
                else:
                    continue
                open_.pop(i)
                grown = True
                break
        if len(cur) > 3 and cur[0] == cur[-1]:
            rings.append(cur)
    return rings


def closed_rings(el, project):
    """A closed way's ring, or a relation's member ways joined into rings."""
    if el['type'] == 'way':
        g = geom(el, project)
        return [g] if len(g) > 3 and el['geometry'][0] == el['geometry'][-1] else []
    lines = [tuple((p['lat'], p['lon']) for p in m['geometry'])
             for m in el.get('members', []) if m.get('type') == 'way' and m.get('geometry')]
    return [[project(lat, lon) for lat, lon in ring] for ring in join_rings(lines)]


def render_zoom(z, layers, origin, cols, rows, detail):
    ox, oy = origin
    c = Canvas(cols * TILE, rows * TILE)

    def project(lat, lon):
        x, y = world_xy(lat, lon, z)
        return int(round(x - ox)), int(round(y - oy))

    for el in layers['parks']:                       # areas first, lines on top
        t = el.get('tags', {})
        if 'railway' not in t:
            for ring in closed_rings(el, project):
                c.fill([ring], PARK, texture=True)
    for el in layers['water']:
        t = el.get('tags', {})
        if t.get('waterway') in ('river', 'canal'):
            c.line(geom(el, project), WATER, 3 if z >= 12 else 2)
        else:
            rings = closed_rings(el, project)
            if rings:
                c.fill(rings, WATER)
    if detail:
        for el in layers['parks']:
            if el.get('tags', {}).get('railway') == 'rail':
                c.line(geom(el, project), RAIL, 1, dash=3)
        for el in layers['minor']:
            c.line(geom(el, project), MINOR, 1)
    order = {'tertiary': 0, 'secondary': 1, 'primary': 2, 'trunk': 3, 'motorway': 3}
    majors = sorted(layers['major'], key=lambda e: order.get(e.get('tags', {}).get('highway', '').replace('_link', ''), 0))
    for el in majors:
        kind = el.get('tags', {}).get('highway', '').replace('_link', '')
        rank = order.get(kind, 0)
        if z <= 10 and rank < 2:
            continue                                 # only primary and up at the widest view
        if z == 11 and rank < 1:
            continue
        width = 1
        if rank == 3:
            width = 3 if z >= 13 else 2
        elif rank == 2 and z >= 12:
            width = 2
        c.line(geom(el, project), MAJOR if rank >= 2 else MID, width)
    return c


def to_depth(v, bpp):
    return v * ((1 << bpp) - 1) // 15


def packbits(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 127:
            j += 1
        if j > i:                                    # run of i..j
            out += bytes([257 - (j - i + 1), data[i]])
            i = j + 1
            continue
        j = i
        while j + 1 < n and j - i < 127 and not (j + 2 < n and data[j + 1] == data[j + 2]):
            j += 1
        out.append(j - i)
        out += data[i:j + 1]
        i = j + 1
    return bytes(out)


def unpackbits(data, size):
    out = bytearray()
    i = 0
    while len(out) < size:
        h = data[i]
        i += 1
        if h < 128:
            out += data[i:i + h + 1]
            i += h + 1
        elif h > 128:
            out += bytes([data[i]]) * (257 - h)
            i += 1
    return bytes(out[:size])


def tile_bytes(c, col, row, bpp):
    per = 8 // bpp
    rowbytes = TILE * bpp // 8
    out = bytearray(rowbytes * TILE)
    for y in range(TILE):
        src = (row * TILE + y) * c.w + col * TILE
        line = c.px[src:src + TILE]
        base = y * rowbytes
        for x in range(0, TILE, per):
            b = 0
            for k in range(per):
                b = (b << bpp) | to_depth(line[x + k], bpp)
            out[base + x // per] = b
    return bytes(out)


def write_png(path, c, bpp):
    """The canvas as the Visor will show it: greys reduced to the output depth."""
    top = (1 << bpp) - 1
    shade = bytes(255 - to_depth(min(v, 15), bpp) * 255 // top for v in range(256))
    raw = bytearray()
    for y in range(c.h):
        raw.append(0)
        raw += c.px[y * c.w:(y + 1) * c.w].translate(shade)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', c.w, c.h, 8, 0, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def write_pdb(path, name, records):
    now = int(time.time()) + 2082844800              # Palm epoch: 1904
    n = len(records)
    header = struct.pack('>32sHHIIIIII4s4sIIH', name.encode()[:31], 0x0008, 1, now, now, 0, 0, 0, 0,
                         b'Map ', b'VMsh', 0, 0, n)
    offset = len(header) + 8 * n + 2
    entries = b''
    for i, r in enumerate(records):
        entries += struct.pack('>IB3s', offset, 0, (i + 1).to_bytes(3, 'big'))
        offset += len(r)
    with open(path, 'wb') as f:
        f.write(header + entries + b'\0\0' + b''.join(records))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', required=True)
    ap.add_argument('--fetch', action='store_true')
    ap.add_argument('--bbox', default=','.join(str(v) for v in DEFAULT_BBOX))
    ap.add_argument('--detail-bbox', default=','.join(str(v) for v in DEFAULT_DETAIL))
    ap.add_argument('--bpp', type=int, default=2, choices=(1, 2, 4))
    ap.add_argument('--min-zoom', type=int, default=10)
    ap.add_argument('--max-zoom', type=int, default=13)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'build', 'VisorMeshMap.pdb'))
    ap.add_argument('--preview')
    a = ap.parse_args()
    s, w, n, e = (float(v) for v in a.bbox.split(','))
    os.makedirs(a.cache, exist_ok=True)
    if a.fetch:
        fetch(a.cache, (s, w, n, e))

    print('loading OSM data ...', flush=True)
    layers = {
        'major': load(a.cache, 'major.json'),
        'minor': load(a.cache, 'minor.json'),
        'water': load(a.cache, 'water_ways.json') + load(a.cache, 'water_rels.json'),
        'parks': load(a.cache, 'parks.json'),
    }

    zooms, tiles = [], []
    first = 1
    ds, dw, dn, de = (float(v) for v in a.detail_bbox.split(','))
    for z in range(a.min_zoom, a.max_zoom + 1):
        detail = z == a.max_zoom
        x0, y0 = world_xy(dn if detail else n, dw if detail else w, z)   # top-left
        x1, y1 = world_xy(ds if detail else s, de if detail else e, z)
        c0, r0 = int(x0 // TILE), int(y0 // TILE)
        cols, rows = int(x1 // TILE) - c0 + 1, int(y1 // TILE) - r0 + 1
        origin = (c0 * TILE, r0 * TILE)
        t0 = time.time()
        canvas = render_zoom(z, layers, origin, cols, rows, detail)
        if a.preview:
            os.makedirs(a.preview, exist_ok=True)
            write_png(os.path.join(a.preview, 'z%d.png' % z), canvas, a.bpp)
        size = 0
        for r in range(rows):
            for col in range(cols):
                raw = tile_bytes(canvas, col, r, a.bpp)
                if not any(raw):
                    rec = b'\0'
                else:
                    rec = packbits(raw)
                    assert unpackbits(rec, len(raw)) == raw
                tiles.append(rec)
                size += len(rec)
        zooms.append((z, cols, rows, first, origin))
        first += cols * rows
        print('zoom %d: %dx%d tiles, %d KB, %.0f s' % (z, cols, rows, size // 1024, time.time() - t0), flush=True)

    # projection tables at the max zoom, covering the bbox
    lat0, lon0 = int(round(s * 1e6)), int(round(w * 1e6))
    lat_n = int(math.ceil((n - s) * 1e6 / STEP_E6)) + 1
    lon_n = int(math.ceil((e - w) * 1e6 / STEP_E6)) + 1
    zmax = a.max_zoom
    lat_t = [int(round(world_xy((lat0 + i * STEP_E6) / 1e6, w, zmax)[1])) for i in range(lat_n)]
    lon_t = [int(round(world_xy(s, (lon0 + i * STEP_E6) / 1e6, zmax)[0])) for i in range(lon_n)]

    hdr = struct.pack('>4sHHBBiiiiIHH32s', b'VMap', 1, TILE, a.bpp, len(zooms),
                      lat0, lon0, int(round(n * 1e6)), int(round(e * 1e6)),
                      STEP_E6, lat_n, lon_n, ATTRIBUTION.encode('latin-1'))
    for z, cols, rows, frec, (ox, oy) in zooms:
        hdr += struct.pack('>BBHHHii', z, 0, cols, rows, frec, ox, oy)
    hdr += struct.pack('>%di' % lat_n, *lat_t) + struct.pack('>%di' % lon_n, *lon_t)

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    write_pdb(a.out, 'VisorMeshMap', [hdr] + tiles)
    print('%d tiles -> %s (%d KB)' % (len(tiles), os.path.relpath(a.out), os.path.getsize(a.out) // 1024))


if __name__ == '__main__':
    sys.exit(main())
