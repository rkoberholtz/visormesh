#!/usr/bin/env python3
"""Checks the rules in src/sections.h on the compiled objects (run in the
toolchain container by `make`, after linking).

Code in the second segment (section vmcode2) may only use PC-relative
references (DISP8/DISP16 relocations) to symbols that are also in vmcode2:
a PC-relative reference to .text, .data or anything in another section is
wrong at run time on the Visor, and nothing else would catch it. Typical
causes: a prototype missing VM_SECT2, a string literal, or static const data.

    check_sections.py OBJECT...      exits 1 and lists every bad reference
"""
import subprocess
import sys

SECTION = 'vmcode2'
OBJDUMP = 'm68k-palmos-objdump'


def symbols_by_section(objs):
    """{symbol: section} for symbols defined in the objects."""
    where = {}
    for obj in objs:
        out = subprocess.run([OBJDUMP, '-t', obj], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            # "00000000 g     F vmcode2  00000012 VmSha256" / "00000000 l     d .text ..."
            if len(parts) >= 4 and parts[-2] != '*UND*' and not line.startswith(obj):
                sect = next((p for p in parts if p in ('.text', '.data', '.bss', SECTION)), None)
                if sect:
                    where[parts[-1]] = sect
    return where


def main():
    objs = sys.argv[1:]
    where = symbols_by_section(objs)
    bad = []
    for obj in objs:
        out = subprocess.run([OBJDUMP, '-r', '-j', SECTION, obj], capture_output=True, text=True)
        if out.returncode != 0:      # no vmcode2 section in this object
            continue
        for line in out.stdout.splitlines():
            parts = line.split()
            if len(parts) != 3 or not parts[1].startswith('DISP'):
                continue
            target = parts[2].split('+')[0].split('-')[0]
            sect = target if target.startswith('.') else where.get(target, SECTION)
            if sect != SECTION:
                bad.append(f'{obj}: PC-relative {parts[1]} at {parts[0]} to {parts[2]} (in {sect})')
    if bad:
        print('Second code segment refers PC-relative outside itself (see src/sections.h):')
        print('\n'.join('  ' + b for b in bad))
        sys.exit(1)
    print(f'sections OK: {len(objs)} objects checked')


if __name__ == '__main__':
    main()
