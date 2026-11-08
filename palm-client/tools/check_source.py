#!/usr/bin/env python3
"""
Source rules no compiler or host test catches (run by `make`, like
check_sections.py and check_ui.py):

  tables    no static const inside a function that's in an extra code
            segment (VM_SECT2-5): it reads as garbage on the Visor (sections.h)
  menus     every menu item has a case somewhere in the code; no shortcut
            letter twice on one menu bar; app items keep clear of the Edit
            menu's U X C P S K G
  forms     every form gets an event handler in main.c's frmLoadEvent switch,
            or is opened as a dialog with FrmInitForm
  ids       no number used twice within one form, one menu bar or one
            resource type (forms, alerts, menu bars); numbers may repeat
            across those, which Palm OS keeps apart

    tools/check_source.py resources/visormesh.rcp src/resources.h SOURCE...
"""
import re
import sys

EDIT_KEYS = set('UXCPSKG')
EXTRA = re.compile(r'\bVM_SECT[2-5]\b')


def strip_comments(text):
    """C source with comments and string contents blanked, line numbers kept."""
    def blank(m):
        return re.sub(r'[^\n]', ' ', m.group(0))
    text = re.sub(r'/\*.*?\*/', blank, text, flags=re.S)
    text = re.sub(r'//[^\n]*', blank, text)
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', text)


def extra_segment_functions(sources):
    """Names declared VM_SECT2-5 in any header or source."""
    names = set()
    for path in sources:
        text = strip_comments(open(path, encoding='latin-1').read())
        for m in re.finditer(r'(\w+)\s*\(([^;{}()]|\([^;{}()]*\))*\)\s*VM_SECT[2-5]\s*;', text):
            names.add(m.group(1))
    return names


def check_tables(c_files, extra):
    """static const inside the body of an extra-segment function (K&R: { and } at column 0)."""
    problems = []
    for path in c_files:
        lines = strip_comments(open(path, encoding='latin-1').read()).split('\n')
        func = head = None
        for no, line in enumerate(lines, 1):
            if func is None:
                m = re.match(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(', line)
                if m and not line.rstrip().endswith(';'):
                    head = m.group(1)
                if line.startswith('{'):
                    func = head
                continue
            if line.startswith('}'):
                func = None
                continue
            if func in extra and re.search(r'\bstatic\s+const\b|\bconst\s+static\b', line):
                problems.append('%s:%d: static const inside %s, which is in an extra segment: '
                                'it reads as garbage on the Visor (compute it, or move it to file level)'
                                % (path, no, func))
    return problems


def read_menus(rcp):
    """[(bar, [(label, id, key)])] from the MENU blocks."""
    bars, bar = [], None
    for line in rcp.split('\n'):
        m = re.match(r'\s*MENU\s+ID\s+(\w+)', line)
        if m:
            bar = (m.group(1), [])
            bars.append(bar)
            continue
        m = re.match(r'\s*MENUITEM\s+"([^"]*)"\s+(?:ID\s+(\w+)|(AUTOID))(?:\s+"(.)")?', line)
        if m and bar:
            bar[1].append((m.group(1), m.group(2), m.group(4)))
    return bars


def check_menus(rcp, code):
    problems = []
    for bar, items in read_menus(rcp):
        keys = {}
        for label, item_id, key in items:
            if label == '-' or item_id is None or item_id.isdigit():
                continue   # separators and the system's Edit commands
            if not re.search(r'\bcase\s+%s\s*:|==\s*%s\b' % (item_id, item_id), code):
                problems.append('menu %s: "%s" (%s) has no case in the code: it would do nothing'
                                % (bar, label, item_id))
            if key:
                k = key.upper()
                if k in EDIT_KEYS:
                    problems.append('menu %s: "%s" uses /%s, an Edit menu shortcut' % (bar, label, k))
                if k in keys:
                    problems.append('menu %s: "%s" and "%s" both use /%s' % (bar, keys[k], label, k))
                keys[k] = label
    return problems


def check_forms(rcp, code, main_c):
    problems = []
    for form in re.findall(r'(?m)^FORM\s+ID\s+(\w+)', rcp):
        if re.search(r'\bcase\s+%s\s*:' % form, main_c) or re.search(r'FrmInitForm\s*\(\s*%s\s*\)' % form, code):
            continue
        problems.append('form %s gets no event handler in main.c and isn\'t opened with FrmInitForm' % form)
    return problems


def check_ids(rcp, header):
    values = {m.group(1): int(m.group(2)) for m in re.finditer(r'(?m)^\s*#define\s+(\w+)\s+(\d+)\b', header)}

    def num(name):
        return int(name) if name.isdigit() else values.get(name)

    problems = []
    groups = {}    # (kind, where) -> {number: name}

    def add(kind, where, name):
        n = num(name)
        if n is None:
            return
        seen = groups.setdefault((kind, where), {})
        if n in seen:
            problems.append('%s %s: %s and %s are both %d' % (kind, where, seen[n], name, n) if seen[n] != name
                            else '%s %s: %s is used twice' % (kind, where, name))
        seen.setdefault(n, name)

    block = None
    for line in rcp.split('\n'):
        m = re.match(r'\s*(FORM|ALERT|MENU)\s+ID\s+(\w+)', line)
        if m:
            kind, name = m.groups()
            add('resource', kind, name)
            block = (kind.lower(), name)
            continue
        if block is None:
            continue
        if block[0] == 'form':
            if re.match(r'\s*POPUPLIST\b', line):
                continue   # names its trigger and list, both declared on their own lines
            for name in re.findall(r'\bID\s+(\w+)', line):
                add('form', block[1], name)
        elif block[0] == 'menu':
            m = re.match(r'\s*MENUITEM\s+"[^"]*"\s+ID\s+(\w+)', line)
            if m:
                add('menu', block[1], m.group(1))
    return problems


def main(argv):
    rcp_path, header_path, sources = argv[1], argv[2], argv[3:]
    rcp = open(rcp_path, encoding='latin-1').read()
    header = open(header_path, encoding='latin-1').read()
    c_files = [p for p in sources if p.endswith('.c')]
    code = '\n'.join(strip_comments(open(p, encoding='latin-1').read()) for p in c_files)
    main_c = strip_comments(open([p for p in c_files if p.endswith('src/main.c')][0], encoding='latin-1').read())

    problems = check_tables(c_files, extra_segment_functions(sources))
    problems += check_menus(rcp, code)
    problems += check_forms(rcp, code, main_c)
    problems += check_ids(rcp, header)
    for p in problems:
        print('check_source: ' + p)
    if problems:
        return 1
    print('source OK: %d files' % len(sources))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
