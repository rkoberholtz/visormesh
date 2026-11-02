/*
 * Code segments. The app's code is more than the ~32 KB one 68k code resource
 * can reach, so modules also live in vmcode2 and vmcode3 (VM_SECT2/VM_SECT3; see
 * visormesh.def; multigen makes the stub and linker script). Everything below
 * applies to any extra section.
 *
 * How prc-tools/GCC 2.95 handles it (checked in the compiled objects):
 * - A function declared VM_SECT2 is placed in vmcode2; callers that see the
 *   declaration call it through the section's base pointer (__text__vmcode2),
 *   and taking its address (event handlers, callbacks) gives the real address.
 *   A function without it stays in the first segment and is still called
 *   correctly. So put VM_SECT2 on the header prototypes and on the file's
 *   static functions (tools/sect2.py adds the static declarations).
 * - String literals in a vmcode2 function are emitted into vmcode2 itself, and
 *   a file's static const data in .text is reached through __text__: both fine.
 *
 * Real traps:
 * - A const table shared across files (extern const): other files assume it's
 *   data and address it A5-relative, but it's in code. Shared tables must not
 *   be const (font widths and presets hit this).
 * - -fwritable-strings: literals move to .data but are still addressed
 *   PC-relative. Don't use it.
 * tools/check_sections.py runs after every link and fails the build on any
 * PC-relative reference from vmcode2 to outside it.
 * On the host (tests) and in the editor's clang checker the attribute compiles away.
 */
#ifndef VM_SECTIONS_H
#define VM_SECTIONS_H

#if defined(__palmos__) && !defined(__clang__)   /* clang: the editor's checker, which rejects the section name */
#define VM_SECT2 __attribute__ ((section ("vmcode2")))
#define VM_SECT3 __attribute__ ((section ("vmcode3")))
#else
#define VM_SECT2
#define VM_SECT3
#endif

#endif
