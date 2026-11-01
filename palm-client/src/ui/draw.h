/*
 * Drawing helpers for the MeshTerm-style UI: the small font, section headers,
 * inverted name chips and right-aligned lanes.
 */
#ifndef VM_DRAW_H
#define VM_DRAW_H

#include <PalmOS.h>
#include "glyphs.h"

#define vmSmallFont      ((FontID)fntAppFontCustomBase)
#define VM_ROW_H         (VM_SMALL_FONT_HEIGHT + 1)   /* small-font line pitch */

/* Registers the small font (call once at launch / release at exit). */
Err  UiFontInit(void);
void UiFontRelease(void);

/* "── Label ──" across the full width at y, in the small font. */
void DrawSection(const Char *label, Coord y);

/* A name chip: text inverted (white on black), VM_CHIP_H tall. Returns its width. */
#define VM_CHIP_H        (VM_SMALL_FONT_HEIGHT + 2)
Coord DrawChip(const Char *text, Coord x, Coord y);

/* Draws text in the current font, cut with an ellipsis to fit maxW. Returns the width drawn. */
Coord DrawCut(const Char *text, Coord x, Coord y, Coord maxW);

/* Draws text right-aligned so it ends at xRight. Returns its left edge. */
Coord DrawRight(const Char *text, Coord xRight, Coord y);

#endif
