#!/usr/bin/env python3
"""Puts a C file's static functions in an extra code segment: inserts a
'static <signature> VM_SECT2;' (or --sect3: VM_SECT3) forward declaration for
each one after the last #include (see src/sections.h). Run once per file
moved; idempotent."""
import re
import sys

macro = 'VM_SECT3' if '--sect3' in sys.argv else 'VM_SECT2'
for path in [a for a in sys.argv[1:] if not a.startswith('--')]:
    src = open(path).read()
    marker = '/* In an extra code segment (' + macro + '): see sections.h for the rules. */'
    if 'code segment: see sections.h' in src or 'code segment (VM_SECT' in src:
        print(f'{path}: already done')
        continue
    if marker in src:
        print(f'{path}: already done')
        continue
    sigs = re.findall(r'^(static\s+(?!const\s+UInt|UInt8\s+g|Char\s+\*g)[^=;{]*?\w+\s*\([^;{]*?\))\s*\n\{', src, re.M)
    decls = '\n'.join(re.sub(r'\s+', ' ', s) + ' ' + macro + ';' for s in sigs)
    last_inc = list(re.finditer(r'^#include .*\n', src, re.M))[-1]
    src = src[:last_inc.end()] + f'\n{marker}\n{decls}\n' + src[last_inc.end():]
    open(path, 'w').write(src)
    print(f'{path}: {len(sigs)} static functions')
