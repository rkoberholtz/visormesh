/*
 * Drawing helpers for the MeshTerm-style UI: the small font, section headers,
 * inverted name chips and right-aligned lanes.
 */
#ifndef VM_DRAW_H
#define VM_DRAW_H

#include <PalmOS.h>
#include "../sections.h"   /* this module is in the second code segment */
#include "glyphs.h"

#define vmSmallFont      ((FontID)fntAppFontCustomBase)   /* the UI font, small or large */

/* VM_SMALL_FONT_HEIGHT or VM_LARGE_FONT_HEIGHT, whichever font is in use. */
extern UInt16 gUiFontHeight;
#define VM_ROW_H         (gUiFontHeight + 1)   /* UI-font line pitch */

/* Registers the UI font, the larger one if large (call at launch / release at exit). */
Err  UiFontInit(Boolean large) VM_SECT2;
void UiFontRelease(void) VM_SECT2;
/* Switches between the fonts; forms opened afterwards lay out for the new one. */
Err  UiFontSetLarge(Boolean large) VM_SECT2;

/* "── Label ──" from x to x + width at y, in the small font. */
void DrawSection(const Char *label, Coord x, Coord y, Coord width) VM_SECT2;

/* A name chip: text inverted (white on black), VM_CHIP_H tall. Returns its width. */
#define VM_CHIP_H        (gUiFontHeight + 2)
Coord DrawChip(const Char *text, Coord x, Coord y) VM_SECT2;
/* The width DrawChip would take, without drawing. */
Coord ChipWidth(const Char *text) VM_SECT2;

/* Draws text in the current font, cut with an ellipsis to fit maxW. Returns the width drawn. */
Coord DrawCut(const Char *text, Coord x, Coord y, Coord maxW) VM_SECT2;

/* Draws text right-aligned so it ends at xRight. Returns its left edge. */
Coord DrawRight(const Char *text, Coord xRight, Coord y) VM_SECT2;

#endif
