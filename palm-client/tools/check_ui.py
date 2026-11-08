#!/usr/bin/env python3
"""
Checks that the UI stays on the 160x160 screen, after PilRC has compiled the
resources (run by `make`, like check_sections.py):

  forms    every object's bounds (PilRC has already worked out AUTO, CENTER
           and PREVRIGHT) inside its form, and labels' text inside it too
  alerts   the buttons in a row fit across the alert, as Palm OS lays them
           out: from x 4, each its text + 12 wide, 4 apart, ending by 156
           (from the device: "2 weeks" "1 month" "3 months" "Cancel" lost
           Cancel; "2 wk" "1 mo" "3 mo" "Cancel" fit)
  C text   literal strings drawn with DrawCut(s, x, y, maxW) are no wider
           than maxW in the large UI font, so a sentence is never cut short
           (use DrawWrap), and WinDrawChars("...", n, x, y) ends by x 160

Text measured at run time (StrPrintF results, names) can't be checked here.
System-font widths come from PilRC itself: an AUTO button is its text + 12.

    tools/check_ui.py build/res resources/largefont.nfnt src/*.c src/ui/*.c
"""
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile

SCREEN = 160
ALERT_X, ALERT_GAP, ALERT_PAD, ALERT_RIGHT = 4, 4, 12, 156

# FormObjectKind
FIELD, CONTROL, LIST, TABLE, BITMAP, LINE, FRAME, RECT, LABEL, TITLE, POPUP, GSI, GADGET, SCROLL = range(14)
KINDS = ['field', 'control', 'list', 'table', 'bitmap', 'line', 'frame', 'rect',
         'label', 'title', 'popup', 'graffiti', 'gadget', 'scrollbar']


def cstr(d, p):
    return d[p:d.index(b'\0', p)]


def read_form(d):
    """(form bounds, [(kind, id, x, y, w, h, font, text)]) from a tFRM resource."""
    fx, fy, fw, fh = struct.unpack('>4h', d[0x0A:0x12])
    n = struct.unpack('>H', d[0x3E:0x40])[0]
    objs = []
    for i in range(n):
        kind, _, p = struct.unpack('>BBI', d[0x44 + 6 * i:0x4A + 6 * i])
        if kind in (FIELD, CONTROL, LIST, TABLE):
            oid, x, y, w, h = struct.unpack('>H4h', d[p:p + 10])
        elif kind == GADGET:
            oid = struct.unpack('>H', d[p:p + 2])[0]
            x, y, w, h = struct.unpack('>4h', d[p + 4:p + 12])
        elif kind == SCROLL:
            x, y, w, h, oid = struct.unpack('>4hH', d[p:p + 10])
        elif kind == LABEL:
            oid, x, y = struct.unpack('>H2h', d[p:p + 6])
            font = d[p + 8]
            objs.append((kind, oid, x, y, None, None, font, cstr(d, p + 14)))
            continue
        else:
            continue
        objs.append((kind, oid, x, y, w, h, None, None))
    return (fx, fy, fw, fh), objs


def read_alert(d):
    """(title, [button labels]) from a Talt resource."""
    nbuttons = struct.unpack('>H', d[4:6])[0]
    strs, p = [], 8
    for _ in range(2 + nbuttons):
        s = cstr(d, p)
        strs.append(s)
        p += len(s) + 1
    return strs[0], strs[2:]


def pilrc_widths(texts):
    """{(font, text): width} in the system fonts, measured by PilRC."""
    texts = sorted(texts)
    if not texts:
        return {}
    with tempfile.TemporaryDirectory() as tmp:
        rcp = b''
        for f, (font, text) in enumerate(texts):
            esc = text.replace(b'\\', b'\\\\').replace(b'"', b'\\"')
            rcp += b'FORM ID %d AT (0 0 160 160)\nBEGIN\n  BUTTON "%s" ID %d AT (0 0 AUTO AUTO) FONT %d\nEND\n' % (
                1000 + f, esc, 5000 + f, font)
        open(os.path.join(tmp, 'probe.rcp'), 'wb').write(rcp)
        subprocess.run(['pilrc', '-q', 'probe.rcp', '.'], cwd=tmp, check=True,
                       stdout=subprocess.DEVNULL)
        out = {}
        for f, key in enumerate(texts):
            d = open(os.path.join(tmp, 'tFRM%04x.bin' % (1000 + f)), 'rb').read()
            _, objs = read_form(d)
            out[key] = objs[0][4] - 12
        return out


def nfnt_widths(path):
    """Width of each byte in an NFNT font (missing characters take the missing glyph's)."""
    d = open(path, 'rb').read()
    first, last = struct.unpack('>2h', d[2:6])
    owtloc = struct.unpack('>h', d[16:18])[0]
    n = last - first + 1
    ow = struct.unpack('>%dH' % (n + 2), d[16 + 2 * owtloc:16 + 2 * owtloc + 2 * (n + 2)])
    missing = ow[n] & 0xFF
    return [ow[c - first] & 0xFF if first <= c <= last and ow[c - first] != 0xFFFF else missing
            for c in range(256)]


def c_literal(s):
    """The bytes of a C string literal's body (simple escapes only)."""
    out, i = bytearray(), 0
    raw = s.encode('latin-1')
    while i < len(raw):
        c = raw[i]
        if c == 0x5C and i + 1 < len(raw):
            e = chr(raw[i + 1])
            if e == 'x':
                m = re.match(rb'[0-9a-fA-F]{1,2}', raw[i + 2:])
                out.append(int(m.group(0), 16))
                i += 2 + len(m.group(0))
                continue
            out.append({'n': 10, 't': 9, '0': 0}.get(e, raw[i + 1]))
            i += 2
            continue
        out.append(c)
        i += 1
    return bytes(out)


def check_c(paths, widths):
    problems = []
    cut = re.compile(r'DrawCut\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)\s*,[^,]+,\s*(\d+)\s*\)')
    chars = re.compile(r'WinDrawChars\(\s*"((?:[^"\\]|\\.)*)"\s*,[^,]+,\s*(\d+)\s*,')
    for path in paths:
        for no, line in enumerate(open(path, encoding='latin-1'), 1):
            for m in cut.finditer(line):
                w = sum(widths[b] for b in c_literal(m.group(1)))
                if w > int(m.group(3)):
                    problems.append('%s:%d: "%s" is %d px, cut at %s: use DrawWrap' % (
                        path, no, m.group(1), w, m.group(3)))
            for m in chars.finditer(line):
                w = sum(widths[b] for b in c_literal(m.group(1)))
                if int(m.group(2)) + w > SCREEN:
                    problems.append('%s:%d: "%s" ends at x %d, past the screen' % (
                        path, no, m.group(1), int(m.group(2)) + w))
    return problems


def main(argv):
    resdir, font, sources = argv[1], argv[2], argv[3:]
    problems = []
    forms = {}
    for path in sorted(glob.glob(os.path.join(resdir, 'tFRM*.bin'))):
        forms[int(path[-8:-4], 16)] = read_form(open(path, 'rb').read())
    alerts = {}
    for path in sorted(glob.glob(os.path.join(resdir, 'Talt*.bin'))):
        alerts[int(path[-8:-4], 16)] = read_alert(open(path, 'rb').read())

    texts = set()
    for _, objs in forms.values():
        texts.update((o[6], o[7]) for o in objs if o[0] == LABEL)
    for _, buttons in alerts.values():
        texts.update((0, b) for b in buttons)
    sys_w = pilrc_widths(texts)

    for fid, ((_, _, fw, fh), objs) in sorted(forms.items()):
        for kind, oid, x, y, w, h, font_id, text in objs:
            if kind == LABEL:
                w, h = sys_w[(font_id, text)], 11
            what = 'form %d %s %d' % (fid, KINDS[kind], oid)
            if x < 0 or y < 0 or x + w > fw or y + h > fh:
                problems.append('%s at (%d %d %d %d) is outside the form (%d x %d)' % (what, x, y, w, h, fw, fh))
    for aid, (title, buttons) in sorted(alerts.items()):
        right = ALERT_X + sum(sys_w[(0, b)] + ALERT_PAD for b in buttons) + ALERT_GAP * (len(buttons) - 1)
        if right > ALERT_RIGHT:
            problems.append('alert %d "%s": buttons %s end at x %d, past %d' % (
                aid, title.decode('latin-1'), ' '.join('"%s"' % b.decode('latin-1') for b in buttons),
                right, ALERT_RIGHT))
    problems += check_c(sources, nfnt_widths(font))

    for p in problems:
        print('check_ui: ' + p)
    if problems:
        return 1
    print('ui OK: %d forms, %d alerts, %d source files' % (len(forms), len(alerts), len(sources)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
