#ifndef VM_FONTMETRICS_H
#define VM_FONTMETRICS_H

#include "../protocol.h"   /* integer types on both Palm and host */

/* UI font character widths in pixels, indexed by (UInt8) character. */
/* Not const: see the note in fontmetrics.c (prc-tools puts const data in code). */
extern UInt8 gSmallFontWidths[256];
extern UInt8 gLargeFontWidths[256];

#endif
