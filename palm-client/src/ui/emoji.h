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
/* The emoji's name (out: EMOJI_NAME_SIZE); false if c isn't one. */
#define EMOJI_NAME_SIZE 24
Boolean EmojiName(Char c, Char *out) VM_SECT3;
/* A grid of the font's emoji to pick one from; 0 if cancelled. */
Char    EmojiPick(void) VM_SECT3;

#endif
