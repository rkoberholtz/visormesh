#!/usr/bin/env python3
"""Puts a C file's static functions in the second code segment: inserts a
'static <signature> VM_SECT2;' forward declaration for each one after the
last #include (see src/sections.h). Run once per file moved; idempotent."""
import re
import sys

for path in sys.argv[1:]:
    src = open(path).read()
    marker = '/* In the second code segment: see sections.h for the rules. */'
    if marker in src:
        print(f'{path}: already done')
        continue
    sigs = re.findall(r'^(static\s+(?!const\s+UInt|UInt8\s+g|Char\s+\*g)[^=;{]*?\w+\s*\([^;{]*?\))\s*\n\{', src, re.M)
    decls = '\n'.join(re.sub(r'\s+', ' ', s) + ' VM_SECT2;' for s in sigs)
    last_inc = list(re.finditer(r'^#include .*\n', src, re.M))[-1]
    src = src[:last_inc.end()] + f'\n{marker}\n{decls}\n' + src[last_inc.end():]
    open(path, 'w').write(src)
    print(f'{path}: {len(sigs)} static functions')
