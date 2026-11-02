#!/usr/bin/env python3
"""Generates VisorMesh's UI fonts from X.org misc-fixed (public domain) plus
hand-drawn UI symbols: the small font from tools/fonts/4x6.bdf and the larger
one (the Text size option) from tools/fonts/5x8.bdf.

Outputs:
  resources/smallfont.nfnt the NFNT resource, written here directly (the bundled
                           PilRC's FONT compiler emits a corrupt header) and
                           included by PilRC as a DATA resource
  resources/largefont.nfnt the same for the larger font (and largefont.txt)
  resources/smallfont.txt  the same glyphs as text, for reading and diffs
  src/ui/glyphs.h          code points for the UI symbols
  src/ui/fontmetrics.c     per-character widths of both, for text layout
  resources/emoji.bin      16x16 pictures and names of the emoji in the font,
                           for the tap-to-enlarge view (ui/emoji.c)

Palm fonts are 8-bit: 0x20-0x7E and 0xA0-0xFF come from the BDF (ASCII and
Latin-1); the UI symbols go in 0x80-0x9F, reusing Windows-1252's slots where it
has the same symbol (... 0x85, < 0x8B, > 0x9B).
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, '..', 'resources')
OUT_GLYPHS = os.path.join(HERE, '..', 'src', 'ui', 'glyphs.h')
OUT_METRICS = os.path.join(HERE, '..', 'src', 'ui', 'fontmetrics.c')
OUT_EMOJI = os.path.join(HERE, '..', 'resources', 'emoji.bin')

# (name, BDF, ascent, descent): the hand-drawn symbols below are drawn for the
# small font's 5 + 1 and are padded to the larger cell, sitting on the baseline.
FONTS = [
    ('small', os.path.join(HERE, 'fonts', '4x6.bdf'), 5, 1),
    ('large', os.path.join(HERE, 'fonts', '5x8.bdf'), 7, 1),
]
SYMBOL_ASCENT, SYMBOL_DESCENT = 5, 1

# UI symbols: (code point, C name, source). Source is a Unicode code point to take
# from the BDF, or a hand-drawn bitmap (one string per row, 6 rows, '#' = on).
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

# The emoji with their own glyph (src/ui/text.c MapUnicode says which code points
# land on each): (code point in the font, C name, 6-row glyph, 16x16 picture for
# the enlarged view, name shown there). GLYPH_EMOJI, the stand-in for the rest,
# comes first in emoji.bin.
FACE = [
    '.....######.....',
    '...##......##...',
    '..#..........#..',
    '.#............#.',
    '.#............#.',
    '#..............#',
    '#..............#',
    '#..............#',
    '#..............#',
    '#..............#',
    '.#............#.',
    '.#............#.',
    '..#..........#..',
    '...##......##...',
    '.....######.....',
    '................',
]


def face(features):
    """FACE with (row, col) pixels added."""
    rows = [list(r) for r in FACE]
    for r, c in features:
        rows[r][c] = '#'
    return [''.join(r) for r in rows]


def pts(row, cols):
    return [(row, c) for c in cols]


EMOJI = [
    (0x99, 'GLYPH_EMOJI', None,
     face(pts(5, [5, 10]) + pts(6, [5, 10]) + pts(9, [4, 11]) + pts(10, [5, 10]) + pts(11, [6, 7, 8, 9])),
     "Not in the Visor's set"),
    (0x91, 'GLYPH_THUMBS', ['--#---', '-##---', '#####-', '#####-', '####--', '------'], [
        '................',
        '.......##.......',
        '......#..#......',
        '......#..#......',
        '.....#...#......',
        '.....#...#......',
        '....#....######.',
        '###.#.........#.',
        '#.#.#........##.',
        '#.#.#.........#.',
        '#.#.#........##.',
        '#.#.#.........#.',
        '#.#.#........##.',
        '###..########...',
        '................',
        '................'], 'Thumbs up'),
    (0x92, 'GLYPH_LAUGH', ['-###-', '##-##', '#####', '##-##', '-###-', '#---#'],
     face(pts(4, [5, 10]) + pts(5, [4, 6, 9, 11]) + pts(6, [3, 12]) + pts(7, [3, 12]) +
          pts(8, range(3, 13)) + pts(9, [3, 12]) + pts(10, [4, 11]) + pts(11, range(5, 11))), 'Tears of joy'),
    (0x93, 'GLYPH_HEART', ['##-##', '#####', '#####', '-###-', '--#--', '-----'], [
        '................',
        '..####....####..',
        '.######..######.',
        '################',
        '################',
        '################',
        '################',
        '.##############.',
        '..############..',
        '...##########...',
        '....########....',
        '.....######.....',
        '......####......',
        '.......##.......',
        '................',
        '................'], 'Red heart'),
    (0x94, 'GLYPH_CRY', ['-###-', '##-##', '#---#', '#-#-#', '-###-', '-#---'],
     face(pts(5, [4, 5, 10, 11]) + pts(6, [4, 5, 10, 11]) + pts(8, [4]) + pts(9, [4]) +
          pts(10, range(5, 11)) + pts(11, [4, 11])), 'Crying face'),
    (0x95, 'GLYPH_PRAY', ['--#--', '-#-#-', '-#-#-', '#-#-#', '##-##', '-----'], [
        '......####......',
        '.....#.##.#.....',
        '.....#.##.#.....',
        '.....#.##.#.....',
        '....#..##..#....',
        '....#..##..#....',
        '...#...##...#...',
        '...#...##...#...',
        '..#....##....#..',
        '..#....##....#..',
        '..#....##....#..',
        '..#...#..#...#..',
        '..#..#....#..#..',
        '.####......####.',
        '.####......####.',
        '................'], 'Folded hands'),
    (0x96, 'GLYPH_WAVE', ['-#-#--', '#-#-#-', '#####-', '#####-', '-###--', '------'], [
        '...#.#.#........',
        '...#.#.#.#......',
        '...#.#.#.#...#..',
        '...#.#.#.#..#...',
        '...#.#.#.#.#..#.',
        '...#######.#.#..',
        '...#.......#.#..',
        '...#......#.....',
        '...#.....#..#...',
        '...#....#..#....',
        '....#..#........',
        '.....##.........',
        '................',
        '................',
        '................',
        '................'], 'Waving hand'),
    (0x97, 'GLYPH_FIRE', ['--#--', '-##-#', '-####', '##-##', '-###-', '-----'], [
        '.......#........',
        '.......##.......',
        '......###.......',
        '......####......',
        '.....#####...#..',
        '....#######.##..',
        '....##########..',
        '...###########..',
        '..#####.######..',
        '..####...#####..',
        '..###.....####..',
        '..###..#..###...',
        '..###.###.###...',
        '...##.###.##....',
        '....#######.....',
        '................'], 'Fire'),
    (0x98, 'GLYPH_PARTY', ['--#-#-', '#--#--', '-##-#-', '-###--', '####--', '------'], [
        '.......#....#...',
        '...#.........#..',
        '.....#..##......',
        '#......#..#..#..',
        '......#...#.....',
        '.....#....#..#..',
        '....#.....#.....',
        '...#.....#...#..',
        '..#.....#.......',
        '..#....#....#...',
        '.#....#.........',
        '.#...#....#.....',
        '#...#...........',
        '#.##............',
        '##..............',
        '................'], 'Party popper'),
    (0x9A, 'GLYPH_THINK', ['-###--', '##-##-', '#---#-', '#-###-', '-##--#', '---##-'],
     face(pts(3, [9, 10, 11]) + pts(5, [5, 10]) + pts(6, [5, 10]) + pts(10, range(6, 10))) [:12] + [
        '..####.##.......',
        '.#....#.........',
        '.#....#.........',
        '..####..........'], 'Thinking face'),
    (0x9C, 'GLYPH_EYES', ['------', '##-##-', '#####-', '##-##-', '------', '------'], [
        '................',
        '................',
        '................',
        '..####....####..',
        '.#....#..#....#.',
        '#......##......#',
        '#...##.##...##.#',
        '#...##.##...##.#',
        '#......##......#',
        '.#....#..#....#.',
        '..####....####..',
        '................',
        '................',
        '................',
        '................',
        '................'], 'Eyes'),
    (0x9D, 'GLYPH_DISH', ['-#-#-', '#-#-#', '--#--', '-###-', '#-#-#', '-----'], [
        '................',
        '..#....#....#...',
        '.#..#..#..#..#..',
        '.#.#...#...#.#..',
        '.#.#..###..#.#..',
        '.#..#.###.#..#..',
        '..#...#.#...#...',
        '......#.#.......',
        '.....#...#......',
        '.....#...#......',
        '....#.###.#.....',
        '....#.....#.....',
        '...#.......#....',
        '...#.......#....',
        '..###.....###...',
        '................'], 'Antenna'),
    (0x9E, 'GLYPH_HUNDRED', ['#-###-###-', '#-#-#-#-#-', '#-#-#-#-#-', '#-###-###-', '----------', '#########-'], [
        '................',
        '................',
        '.##..###...###..',
        '###.#...#.#...#.',
        '.##.#...#.#...#.',
        '.##.#...#.#...#.',
        '.##.#...#.#...#.',
        '.##.#...#.#...#.',
        '.##.#...#.#...#.',
        '####.###...###..',
        '................',
        '###############.',
        '................',
        '###############.',
        '................',
        '................'], 'Hundred points'),
]
SYMBOLS += [(code, name, small) for code, name, small, _big, _title in EMOJI if small]


def parse_bdf(path, ascent, descent):
    """Returns {codepoint: (width, rows)} with rows as ascent + descent strings of '#'/'-'."""
    height = ascent + descent
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
        cell = [['-'] * dwidth for _ in range(height)]
        top = ascent - (yoff + h)        # cell row of the bitmap's first row
        nbits = ((w + 7) // 8) * 8
        for r, bits in enumerate(bitmap):
            for c in range(w):
                if bits & (1 << (nbits - 1 - c)):
                    y, x = top + r, xoff + c
                    if 0 <= y < height and 0 <= x < dwidth:
                        cell[y][x] = '#'
        glyphs[enc] = (dwidth, [''.join(row) for row in cell])
        i += 1
    return glyphs


def build_nfnt(font, missing, ascent, descent):
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
    height = ascent + descent
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
    bitmap = bytearray(row_words * 2 * height)
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
    ow_t_loc = header_words + row_words * height + len(locs) - ow_t_loc_index
    header = struct.pack('>13h', -28672, first, last, max_w, 0, 0, max_w, height,
                         ow_t_loc, ascent, descent, 0, row_words)
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


def pad_symbol(rows, ascent, descent):
    """Fits a 5 + 1 hand-drawn symbol into a taller cell, keeping it on the baseline."""
    width = len(rows[0])
    blank = '-' * width
    return [blank] * (ascent - SYMBOL_ASCENT) + rows + [blank] * (descent - SYMBOL_DESCENT)


def make_font(name, path, ascent, descent):
    """Builds one font, writes resources/<name>font.nfnt and .txt; returns its 256 widths."""
    height = ascent + descent
    bdf = parse_bdf(path, ascent, descent)
    font = {}
    for code in list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)):
        if code in bdf:
            font[code] = bdf[code][1]
    for code, _name, src in SYMBOLS:
        if isinstance(src, int):
            rows = bdf[src][1]
        else:
            assert len(src) == SYMBOL_ASCENT + SYMBOL_DESCENT, (hex(code), src)
            width = max(len(r) for r in src)
            rows = pad_symbol([r.ljust(width, '-') for r in src], ascent, descent)
        assert len(rows) == height, (name, hex(code), rows)
        font[code] = rows
    missing = bdf[0xFFFD][1]

    nfnt = build_nfnt(font, missing, ascent, descent)
    back = decode_nfnt(nfnt)
    for code, rows in font.items():
        assert back.get(code) == rows, 'NFNT round trip failed for %s 0x%02X' % (name, code)
    assert back['missing'] == missing
    out_nfnt = os.path.join(RES, name + 'font.nfnt')
    with open(out_nfnt, 'wb') as f:
        f.write(nfnt)
    with open(os.path.join(RES, name + 'font.txt'), 'w') as f:
        f.write('fontType 36864\nascent %d\ndescent %d\n\n' % (ascent, descent))
        for code in sorted(font):
            f.write('GLYPH %d\n%s\n\n' % (code, '\n'.join(font[code])))
        f.write('GLYPH -1\n' + '\n'.join(missing) + '\n')     # PilRC wants the missing glyph last
    print('%d glyphs -> %s (%d bytes, round trip OK)' % (len(font), os.path.relpath(out_nfnt), len(nfnt)))
    return [len(font[c][0]) if c in font else len(missing[0]) for c in range(256)]


def main():
    widths, heights = {}, {}
    for name, path, ascent, descent in FONTS:
        widths[name] = make_font(name, path, ascent, descent)
        heights[name] = ascent + descent

    os.makedirs(os.path.dirname(OUT_GLYPHS), exist_ok=True)
    with open(OUT_GLYPHS, 'w') as f:
        f.write('/* GENERATED by tools/gen_font.py. UI symbols in the UI fonts (vmSmallFont). */\n')
        f.write('#ifndef VM_GLYPHS_H\n#define VM_GLYPHS_H\n\n')
        for code, name, _src in SYMBOLS:
            f.write('#define %-16s ((Char)0x%02X)\n' % (name, code))
        f.write('\n#define VM_SMALL_FONT_HEIGHT %d\n#define VM_LARGE_FONT_HEIGHT %d\n\n#endif\n'
                % (heights['small'], heights['large']))

    # emoji.bin: count, then per emoji: code, name[23], 16 rows of 2 bytes (MSB = left)
    out = bytearray([len(EMOJI)])
    for code, _cname, _small, big, title in EMOJI:
        assert len(big) == 16 and all(len(r) == 16 for r in big), (hex(code), [len(r) for r in big])
        out.append(code)
        out += title.encode('latin-1')[:22].ljust(23, b'\0')
        for r in big:
            out += int(r.replace('.', '0').replace('#', '1'), 2).to_bytes(2, 'big')
    with open(OUT_EMOJI, 'wb') as f:
        f.write(out)

    with open(OUT_METRICS, 'w') as f:
        f.write('/* GENERATED by tools/gen_font.py: UI font character widths in pixels. */\n')
        f.write('#include "fontmetrics.h"\n\n')
        f.write('/* Not const: prc-tools puts const data in the code section, but other files reach an\n * extern table A5-relative as if it were data, so a const table reads as garbage on the Visor. */\n')
        for name in ('small', 'large'):
            f.write('UInt8 g%sFontWidths[256] = {\n' % name.capitalize())
            for r in range(0, 256, 16):
                f.write('\t' + ', '.join(str(w) for w in widths[name][r:r + 16]) + ',\n')
            f.write('};\n')


if __name__ == '__main__':
    main()
