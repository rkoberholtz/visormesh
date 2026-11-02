#!/usr/bin/env python3
"""Converts an X11 PCF bitmap font (optionally .gz) to BDF, so tools/gen_font.py
can read X.org misc-fixed fonts that are installed only as PCF (XQuartz has
/opt/X11/share/fonts/misc/5x8.pcf.gz).

    tools/pcf2bdf.py /opt/X11/share/fonts/misc/5x8.pcf.gz tools/fonts/5x8.bdf

Handles what bitmap fonts use: properties, accelerators, metrics (plain or
compressed), bitmaps (any padding / bit and byte order) and encodings.
"""
import gzip
import struct
import sys

PCF_PROPERTIES = 1 << 0
PCF_ACCELERATORS = 1 << 1
PCF_METRICS = 1 << 2
PCF_BITMAPS = 1 << 3
PCF_BDF_ENCODINGS = 1 << 5
PCF_BDF_ACCELERATORS = 1 << 8

PCF_BYTE_MSB = 1 << 2
PCF_BIT_MSB = 1 << 3
PCF_COMPRESSED_METRICS = 0x100


class Table:
    def __init__(self, data, offset):
        self.data = data
        self.pos = offset
        self.format = struct.unpack_from('<i', data, offset)[0]
        self.pos += 4
        self.endian = '>' if self.format & PCF_BYTE_MSB else '<'

    def read(self, fmt):
        fmt = self.endian + fmt
        values = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return values if len(values) > 1 else values[0]


def read_metrics(t, compressed):
    if compressed:
        l, r, w, a, d = (b - 0x80 for b in t.read('5B'))
    else:
        l, r, w, a, d, _attr = t.read('5hH')
    return l, r, w, a, d


def convert(src, dst):
    raw = open(src, 'rb').read()
    if raw[:2] == b'\x1f\x8b':
        raw = gzip.decompress(raw)
    assert raw[:4] == b'\x01fcp', 'not a PCF file'
    count = struct.unpack_from('<i', raw, 4)[0]
    tables = {}
    for i in range(count):
        ttype, _fmt, _size, offset = struct.unpack_from('<4i', raw, 8 + 16 * i)
        tables[ttype] = offset

    props = {}
    t = Table(raw, tables[PCF_PROPERTIES])
    n = t.read('i')
    entries = [t.read('ibi') for _ in range(n)]
    if n & 3:
        t.pos += 4 - (n & 3)
    t.read('i')                                   # string pool size
    pool = t.pos

    def cstr(off):
        end = raw.index(b'\0', pool + off)
        return raw[pool + off:end].decode('latin-1')
    for name_off, is_str, value in entries:
        props[cstr(name_off)] = cstr(value) if is_str else value

    t = Table(raw, tables.get(PCF_BDF_ACCELERATORS, tables[PCF_ACCELERATORS]))
    t.pos += 8                                    # flag bytes
    ascent, descent = t.read('ii')

    t = Table(raw, tables[PCF_METRICS])
    compressed = bool(t.format & PCF_COMPRESSED_METRICS)
    nmetrics = t.read('h' if compressed else 'i')
    metrics = [read_metrics(t, compressed) for _ in range(nmetrics)]

    t = Table(raw, tables[PCF_BITMAPS])
    fmt = t.format
    nglyphs = t.read('i')
    offsets = [t.read('i') for _ in range(nglyphs)]
    sizes = [t.read('i') for _ in range(4)]
    pad = 1 << (fmt & 3)
    unit = 1 << ((fmt >> 4) & 3)
    bits_base = t.pos
    msb_bit = bool(fmt & PCF_BIT_MSB)
    msb_byte = bool(fmt & PCF_BYTE_MSB)

    def glyph_rows(i):
        l, r, _w, a, d = metrics[i]
        width, height = r - l, a + d
        stride = ((width + 7) // 8 + pad - 1) // pad * pad
        out = []
        base = bits_base + offsets[i]
        for y in range(height):
            row = bytearray(raw[base + y * stride: base + (y + 1) * stride])
            if msb_bit != msb_byte and unit > 1:  # swap bytes within each scan unit
                for k in range(0, len(row), unit):
                    row[k:k + unit] = row[k:k + unit][::-1]
            if not msb_bit:
                row = bytearray(int('{:08b}'.format(b)[::-1], 2) for b in row)
            out.append(row[:(width + 7) // 8])
        return out

    t = Table(raw, tables[PCF_BDF_ENCODINGS])
    min2, max2, min1, max1, _default = t.read('5h')
    glyphs = []
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            idx = t.read('H')
            if idx != 0xFFFF:
                glyphs.append(((b1 << 8) | b2, idx))

    with open(dst, 'w') as f:
        f.write('STARTFONT 2.1\n')
        f.write('COMMENT converted from %s by tools/pcf2bdf.py\n' % src.split('/')[-1])
        f.write('FONT %s\n' % props.get('FONT', 'unknown'))
        f.write('SIZE %d 75 75\n' % props.get('PIXEL_SIZE', ascent + descent))
        lo = min(m[0] for m in metrics)
        f.write('FONTBOUNDINGBOX %d %d %d %d\n' % (max(m[1] - m[0] for m in metrics), ascent + descent, lo, -descent))
        keep = {k: v for k, v in props.items() if k != 'FONT'}
        f.write('STARTPROPERTIES %d\n' % (len(keep) + 2))
        for k, v in keep.items():
            f.write('%s "%s"\n' % (k, v.replace('"', '""')) if isinstance(v, str) else '%s %d\n' % (k, v))
        f.write('FONT_ASCENT %d\nFONT_DESCENT %d\nENDPROPERTIES\n' % (ascent, descent))
        f.write('CHARS %d\n' % len(glyphs))
        for enc, idx in glyphs:
            l, r, w, a, d = metrics[idx]
            f.write('STARTCHAR U+%04X\nENCODING %d\n' % (enc, enc))
            f.write('SWIDTH %d 0\nDWIDTH %d 0\n' % (w * 1000 // max(1, ascent + descent), w))
            f.write('BBX %d %d %d %d\nBITMAP\n' % (r - l, a + d, l, -d))
            for row in glyph_rows(idx):
                f.write(row.hex().upper() + '\n')
            f.write('ENDCHAR\n')
        f.write('ENDFONT\n')
    print('%d glyphs, ascent %d, descent %d -> %s' % (len(glyphs), ascent, descent, dst))


if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
