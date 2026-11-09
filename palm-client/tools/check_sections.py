#!/usr/bin/env python3
"""Checks the rules in src/sections.h on the compiled objects (run in the
toolchain container by `make`, after linking).

Code in an extra segment (sections vmcode2, vmcode3, ...) may only use
PC-relative references (DISP8/DISP16 relocations) to symbols in the same section:
a PC-relative reference to .text, .data or anything in another section is
wrong at run time on the Visor, and nothing else would catch it. GCC routes
correct cross-section references through the section bases (END16 + 32-bit
relocations), so a PC-relative one means something it couldn't see, such as
-fwritable-strings literals.

    check_sections.py OBJECT...      exits 1 and lists every bad reference
"""
import re
import subprocess
import sys

SECTIONS = ('vmcode2', 'vmcode3', 'vmcode4', 'vmcode5')
OBJDUMP = 'm68k-palmos-objdump'


def section_names(obj):
    """Section names in header order (COFF symbols refer to them as (sec 1), (sec 2), ...)."""
    out = subprocess.run([OBJDUMP, '-h', obj], capture_output=True, text=True).stdout
    names = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].isdigit():
            names.append(parts[1])
    return names


def symbols_by_section(objs):
    """{symbol: section} for symbols defined in the objects."""
    where = {}
    for obj in objs:
        names = section_names(obj)
        out = subprocess.run([OBJDUMP, '-t', obj], capture_output=True, text=True).stdout
        for line in out.splitlines():
            # COFF: "[  5](sec  1)(fl 0x00)(ty   0)(scl   6) (nx 0) 0x00000000 MaxTop"
            m = re.search(r'\(sec\s+(-?\d+)\).*\s(\S+)$', line)
            if not m:
                continue
            sec = int(m.group(1))
            if 1 <= sec <= len(names) and names[sec - 1] in ('.text', '.data', '.bss') + SECTIONS:
                where.setdefault(m.group(2), names[sec - 1])
    return where


# Objects whose first-segment code is copied as one block (SbWakeArm): every
# .text symbol must lie between the markers, and nothing may be relocated.
BLOCKS = {'sb_spi.o': ('SbCoreStart', 'SbCoreEnd')}


def check_block(obj, first, last):
    """Problems with a copied block: code outside the markers, or references out of it."""
    bad = []
    out = subprocess.run([OBJDUMP, '-t', obj], capture_output=True, text=True).stdout
    names = section_names(obj)
    syms = {}
    for line in out.splitlines():
        m = re.search(r'\(sec\s+(-?\d+)\).*\s(0x[0-9a-f]+)\s(\S+)$', line)
        if m and 1 <= int(m.group(1)) <= len(names) and names[int(m.group(1)) - 1] == '.text':
            syms[m.group(3)] = int(m.group(2), 16)
    lo, hi = syms.get(first), syms.get(last)
    if lo is None or hi is None:
        return [f'{obj}: {first}/{last} not found in .text']
    for name, addr in syms.items():
        if name.startswith('.') or name in ('gcc2_compiled.', '__gnu_compiled_c'):
            continue
        if not lo <= addr <= hi:
            bad.append(f'{obj}: {name} is outside {first}..{last}: SbWakeArm would not copy it')
    relocs = subprocess.run([OBJDUMP, '-r', '-j', '.text', obj], capture_output=True, text=True).stdout
    for line in relocs.splitlines():
        parts = line.split()
        if len(parts) == 3 and re.match(r'^[0-9a-f]+$', parts[0]):
            bad.append(f'{obj}: .text relocation {parts[1]} at {parts[0]} to {parts[2]}: the copied block must refer only to itself')
    return bad


def main():
    objs = sys.argv[1:]
    where = symbols_by_section(objs)
    bad = []
    for obj in objs:
        for base, (first, last) in BLOCKS.items():
            if obj.endswith('/' + base):
                bad += check_block(obj, first, last)
    for obj in objs:
        for section in SECTIONS:
            out = subprocess.run([OBJDUMP, '-r', '-j', section, obj], capture_output=True, text=True)
            if out.returncode != 0:      # no such section in this object
                continue
            for line in out.stdout.splitlines():
                parts = line.split()
                if len(parts) != 3 or not parts[1].startswith('DISP'):
                    continue
                target = parts[2].split('+')[0].split('-')[0]
                sect = target if target.startswith('.') or target in SECTIONS else where.get(target, section)
                if sect != section:
                    bad.append(f'{obj}: {section}: PC-relative {parts[1]} at {parts[0]} to {parts[2]} (in {sect})')
    if bad:
        print('Code segment rules broken (see src/sections.h and src/sb_spi.h):')
        print('\n'.join('  ' + b for b in bad))
        sys.exit(1)
    print(f'sections OK: {len(objs)} objects checked')


if __name__ == '__main__':
    main()
