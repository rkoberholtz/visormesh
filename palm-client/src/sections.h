/*
 * Code segments. The app's code is close to the ~32 KB a single 68k code
 * resource can reach, so some modules live in a second one (see visormesh.def).
 *
 * Rules for a module in VM_SECT2 (prc-tools multiple code sections):
 * - Every function in it, static ones too, is declared VM_SECT2, and callers
 *   see that declaration: calls into another segment go through a jump table.
 * - No static const tables and no string literals: those stay in the first
 *   segment, and code in this one can't reach them PC-relative. Use plain
 *   (non-const) data, which is A5-relative.
 * On the host (tests) and in the editor's clang checker the attribute compiles away.
 */
#ifndef VM_SECTIONS_H
#define VM_SECTIONS_H

#if defined(__palmos__) && !defined(__clang__)   /* clang: the editor's checker, which rejects the section name */
#define VM_SECT2 __attribute__ ((section ("vmcode2")))
#else
#define VM_SECT2
#endif

#endif
