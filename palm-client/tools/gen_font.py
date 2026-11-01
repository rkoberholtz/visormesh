#!/usr/bin/env python3
"""Generates VisorMesh's small font from tools/fonts/4x6.bdf (X.org misc-fixed,
public domain) plus hand-drawn UI symbols.

Outputs:
  resources/smallfont.nfnt the NFNT resource, written here directly (the bundled
                           PilRC's FONT compiler emits a corrupt header) and
                           included by PilRC as a DATA resource
  resources/smallfont.txt  the same glyphs as text, for reading and diffs
  src/ui/glyphs.h          code points for the UI symbols
  src/ui/fontmetrics.c     per-character widths, so host tests lay text out exactly

Palm fonts are 8-bit: 0x20-0x7E and 0xA0-0xFF come from the BDF (ASCII and
Latin-1); the UI symbols go in 0x80-0x9F, reusing Windows-1252's slots where it
has the same symbol (... 0x85, < 0x8B, > 0x9B).
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
BDF = os.path.join(HERE, 'fonts', '4x6.bdf')
OUT_FONT = os.path.join(HERE, '..', 'resources', 'smallfont.txt')
OUT_NFNT = os.path.join(HERE, '..', 'resources', 'smallfont.nfnt')
OUT_GLYPHS = os.path.join(HERE, '..', 'src', 'ui', 'glyphs.h')
OUT_METRICS = os.path.join(HERE, '..', 'src', 'ui', 'fontmetrics.c')

ASCENT, DESCENT = 5, 1
HEIGHT = ASCENT + DESCENT

# UI symbols: (code point, C name, source). Source is a Unicode code point to take
# from the BDF, or a hand-drawn bitmap (one string per row, HEIGHT rows, '#' = on).
SYMBOLS = [
    (0x80, 'GLYPH_SELF', ['--#--', '#####', '-###-', '-#-#-', '#---#', '-----']),      # star: you
    (0x81, 'GLYPH_COMPANION', ['-----', '-##-', '####', '####', '-##-', '----']),      # filled circle
    (0x82, 'GLYPH_REPEATER', ['-----', '--#--', '-###-', '#####', '-----', '-----']),  # triangle
    (0x83, 'GLYPH_ROOM', 0x25A0),                                                       # square
    (0x84, 'GLYPH_SENSOR', ['-###-', '#---#', '#-#-#', '#---#', '-###-', '-----']),    # fisheye
    (0x85, 'GLYPH_ELLIPSIS', 0x2026),
    (0x86, 'GLYPH_UNKNOWN', ['-----', '-##-', '#--#', '#--#', '-##-', '----']),        # open circle
    (0x87, 'GLYPH_ACKED', ['-----', '----#', '---#-', '#-#--', '-#---', '-----']),     # check mark
    (0x88, 'GLYPH_FAILED', ['-----', '#---#', '-#-#-', '--#--', '-#-#-', '#---#']),    # ballot x
    (0x89, 'GLYPH_UP', 0x2191),
    (0x8A, 'GLYPH_DOWN', 0x2193),
    (0x8B, 'GLYPH_BACK', 0x2039),
    (0x8C, 'GLYPH_LOCK', ['-##-', '#--#', '####', '#-##', '####', '----']),            # padlock: private channel
    (0x8D, 'GLYPH_RULE', 0x2500),
    (0x8E, 'GLYPH_LEFT', 0x2190),
    (0x8F, 'GLYPH_RIGHT', 0x2192),
    (0x90, 'GLYPH_PENDING', ['-----', '-###-', '#-#-#', '#-##-', '#---#', '-###-']),   # clock face
    (0x99, 'GLYPH_EMOJI', ['-###-', '#-#-#', '#---#', '##-##', '-###-', '-----']),     # smiley: any emoji
    (0x9B, 'GLYPH_FORWARD', 0x203A),
]


def parse_bdf(path):
    """Returns {codepoint: (width, rows)} with rows as HEIGHT strings of '#'/'-'."""
    glyphs = {}
    with open(path) as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        if not lines[i].startswith('STARTCHAR'):
            i += 1
            continue
        enc = dwidth = None
        bbx = None
        bitmap = []
        i += 1
        while not lines[i].startswith('ENDCHAR'):
            parts = lines[i].split()
            if parts[0] == 'ENCODING':
                enc = int(parts[1])
            elif parts[0] == 'DWIDTH':
                dwidth = int(parts[1])
            elif parts[0] == 'BBX':
                bbx = tuple(int(x) for x in parts[1:5])
            elif parts[0] == 'BITMAP':
                i += 1
                while not lines[i].startswith('ENDCHAR'):
                    bitmap.append(int(lines[i], 16))
                    i += 1
                break
            i += 1
        w, h, xoff, yoff = bbx
        cell = [['-'] * dwidth for _ in range(HEIGHT)]
        top = ASCENT - (yoff + h)        # cell row of the bitmap's first row
        nbits = ((w + 7) // 8) * 8
        for r, bits in enumerate(bitmap):
            for c in range(w):
                if bits & (1 << (nbits - 1 - c)):
                    y, x = top + r, xoff + c
                    if 0 <= y < HEIGHT and 0 <= x < dwidth:
                        cell[y][x] = '#'
        glyphs[enc] = (dwidth, [''.join(row) for row in cell])
        i += 1
    return glyphs


def build_nfnt(font, missing):
    """
    Palm OS FontType (Mac FONT layout, big-endian words):
      fontType, firstChar, lastChar, maxWidth, kernMax, nDescent, fRectWidth,
      fRectHeight, owTLoc, ascent, descent, leading, rowWords,
      bitmap[rowWords * fRectHeight]   all glyphs side by side, 1 bit per pixel
      locTable[n + 2]                  x of each glyph in the strip, then the strip width
      owTable[n + 2]                   (offset << 8 | width); 0xFFFF = not in font
    where n = lastChar - firstChar + 1 and entry n is the missing-char glyph.
    owTLoc is the distance in words from the owTLoc field to owTable.
    """
    first, last = min(font), max(font)
    n = last - first + 1
    order = [font.get(c) for c in range(first, last + 1)] + [missing]
    locs, x = [], 0
    for rows in order:
        locs.append(x)
        x += len(rows[0]) if rows else 0
    locs.append(x)
    strip_w = x
    row_words = (strip_w + 15) // 16
    bitmap = bytearray(row_words * 2 * HEIGHT)
    for rows, x0 in zip(order, locs):
        if not rows:
            continue
        for y, row in enumerate(rows):
            for dx, ch in enumerate(row):
                if ch == '#':
                    bx = x0 + dx
                    bitmap[y * row_words * 2 + bx // 8] |= 0x80 >> (bx % 8)
    ow = [(0 << 8 | len(rows[0])) if rows else 0xFFFF for rows in order] + [0xFFFF]
    max_w = max(len(r[0]) for r in order if r)
    header_words, ow_t_loc_index = 13, 8
    ow_t_loc = header_words + row_words * HEIGHT + len(locs) - ow_t_loc_index
    header = struct.pack('>13h', -28672, first, last, max_w, 0, 0, max_w, HEIGHT,
                         ow_t_loc, ASCENT, DESCENT, 0, row_words)
    return header + bytes(bitmap) + struct.pack('>%dH' % len(locs), *locs) + struct.pack('>%dH' % len(ow), *ow)


def decode_nfnt(data):
    """Reads an NFNT back into {code: rows} (and 'missing'), to check build_nfnt."""
    h = struct.unpack('>13h', data[:26])
    first, last, height, ow_t_loc, row_words = h[1], h[2], h[7], h[8], h[12]
    n = last - first + 1
    bm_off = 26
    loc_off = bm_off + row_words * 2 * height
    locs = struct.unpack('>%dH' % (n + 2), data[loc_off:loc_off + 2 * (n + 2)])
    ow_off = (ow_t_loc_index_bytes := 8 * 2) + ow_t_loc * 2
    ow = struct.unpack('>%dH' % (n + 2), data[ow_off:ow_off + 2 * (n + 2)])
    out = {}
    for i in range(n + 1):
        if ow[i] == 0xFFFF:
            continue
        x0, x1 = locs[i], locs[i + 1]
        rows = []
        for y in range(height):
            row = ''
            for x in range(x0, x1):
                byte = data[bm_off + y * row_words * 2 + x // 8]
                row += '#' if byte & (0x80 >> (x % 8)) else '-'
            rows.append(row)
        out['missing' if i == n else first + i] = rows
    return out


def main():
    bdf = parse_bdf(BDF)
    font = {}
    for code in list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)):
        if code in bdf:
            font[code] = bdf[code][1]
    for code, _name, src in SYMBOLS:
        rows = bdf[src][1] if isinstance(src, int) else src
        assert len(rows) == HEIGHT, (hex(code), rows)
        width = max(len(r) for r in rows)
        font[code] = [r.ljust(width, '-') for r in rows]
    missing = bdf[0xFFFD][1] if 0xFFFD in bdf else ['####', '#--#', '#--#', '#--#', '####', '----']

    os.makedirs(os.path.dirname(OUT_GLYPHS), exist_ok=True)
    nfnt = build_nfnt(font, missing)
    back = decode_nfnt(nfnt)
    for code, rows in font.items():
        assert back.get(code) == rows, 'NFNT round trip failed for 0x%02X' % code
    assert back['missing'] == missing
    with open(OUT_NFNT, 'wb') as f:
        f.write(nfnt)

    with open(OUT_FONT, 'w') as f:
        f.write('fontType 36864\nascent %d\ndescent %d\n\n' % (ASCENT, DESCENT))
        for code in sorted(font):
            f.write('GLYPH %d\n%s\n\n' % (code, '\n'.join(font[code])))
        f.write('GLYPH -1\n' + '\n'.join(missing) + '\n')     # PilRC wants the missing glyph last

    with open(OUT_GLYPHS, 'w') as f:
        f.write('/* GENERATED by tools/gen_font.py. UI symbols in the small font (vmSmallFont). */\n')
        f.write('#ifndef VM_GLYPHS_H\n#define VM_GLYPHS_H\n\n')
        for code, name, _src in SYMBOLS:
            f.write('#define %-16s ((Char)0x%02X)\n' % (name, code))
        f.write('\n#define VM_SMALL_FONT_HEIGHT %d\n\n#endif\n' % HEIGHT)

    widths = [len(font[c][0]) if c in font else len(missing[0]) for c in range(256)]
    with open(OUT_METRICS, 'w') as f:
        f.write('/* GENERATED by tools/gen_font.py: small-font character widths in pixels. */\n')
        f.write('#include "fontmetrics.h"\n\n')
        f.write('/* Not const: prc-tools puts const data in the code section, but other files reach an\n * extern table A5-relative as if it were data, so a const table reads as garbage on the Visor. */\n')
        f.write('UInt8 gSmallFontWidths[256] = {\n')
        for r in range(0, 256, 16):
            f.write('\t' + ', '.join(str(w) for w in widths[r:r + 16]) + ',\n')
        f.write('};\n')
    print('%d glyphs -> %s (%d bytes, round trip OK)' % (len(font), os.path.relpath(OUT_NFNT), len(nfnt)))


if __name__ == '__main__':
    main()
