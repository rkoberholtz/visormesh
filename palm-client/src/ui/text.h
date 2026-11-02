/*
 * Text layout and formatting for the small-font UI. Plain C (no Palm OS calls)
 * so it is tested on the host with the real small-font width table.
 */
#ifndef VM_TEXT_H
#define VM_TEXT_H

#include "../protocol.h"   /* also brings sections.h: this module is in the second code segment */

/* Width of one character in pixels (e.g. from gSmallFontWidths, or FntCharWidth on the device). */
typedef Int16 (*TxtWidthFn)(Char c);

/*
 * The UI font's widths from the generated tables: the small font, or the
 * larger one while gTxtLarge is set (draw.c keeps it in step with the font).
 */
extern Boolean gTxtLarge;
Int16  TxtSmallWidth(Char c) VM_SECT2;

Int16  TxtWidth(const Char *s, UInt16 len, TxtWidthFn w) VM_SECT2;

/* How many characters of s fit in maxW pixels. */
UInt16 TxtFit(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w) VM_SECT2;

/*
 * One line of word wrap: returns how many characters to draw on this line
 * (breaking after the last space that fits, or mid-word if a word is longer
 * than the line) and sets *consumed to that plus any spaces to skip before
 * the next line. A \n (or \r, \r\n) in the text ends the line there and is
 * consumed. Returns 0 with *consumed 0 only for empty input.
 */
UInt16 TxtWrap(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w, UInt16 *consumed) VM_SECT2;

/* "now", "5m", "23h", "3d", "2y" (MeshTerm's aligned-column ages). */
void   TxtFormatAge(Char *out, UInt32 seconds) VM_SECT2;

/*
 * n bytes as 2n lowercase hex digits plus a NUL. Not StrPrintF: the Visor's
 * %x prints 4 digits for a byte, so "%02x" per byte overlapped and showed zeros.
 */
void   TxtHex(const UInt8 *bytes, UInt16 n, Char *out) VM_SECT2;

/* SNR in quarter dB as a whole number: "+6dB", "-3dB", "0dB". */
void   TxtFormatSnr(Char *out, Int8 quarterDb) VM_SECT2;

/*
 * MeshCore channel messages carry the sender in the text as "Name: message".
 * Returns the name's length (0 if there's no such prefix); the message starts
 * at name length + 2.
 */
UInt16 TxtSplitSender(const Char *text, UInt16 len) VM_SECT2;

/*
 * UTF-8 to the Visor's Latin-1 / Windows-1252 character set. Characters it
 * can't show become '?'. Writes at most outSize - 1 bytes plus a terminator
 * and returns the length.
 */
UInt16 TxtUtf8ToPalm(const UInt8 *in, UInt16 inLen, Char *out, UInt16 outSize) VM_SECT2;

/* Converts a NUL-terminated UTF-8 name in place to the Visor's charset (size <= 48). */
void   TxtUtf8NameToPalm(Char *name, UInt16 size) VM_SECT2;

/*
 * The Visor's text (Windows-1252) to UTF-8 for sending. Returns the byte count,
 * or 0 if it won't fit in outSize (so a message is never cut mid-character).
 */
UInt16 TxtPalmToUtf8(const Char *in, UInt16 inLen, UInt8 *out, UInt16 outSize) VM_SECT2;

#endif
