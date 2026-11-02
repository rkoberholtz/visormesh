/*
 * The enlarged view of an emoji: its 16x16 picture at double size and its
 * name, in a box over the screen until the next tap. The pictures come from
 * resources/emoji.bin (tools/gen_font.py), a resource rather than a table in
 * the code (see sections.h).
 */
#ifndef VM_EMOJI_H
#define VM_EMOJI_H

#include <PalmOS.h>
#include "../sections.h"

/* Shows c if it's one of the font's emoji (or GLYPH_EMOJI); false if it isn't one. */
Boolean EmojiShow(Char c) VM_SECT3;

#endif
