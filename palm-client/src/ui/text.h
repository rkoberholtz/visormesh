/*
 * Text layout and formatting for the small-font UI. Plain C (no Palm OS calls)
 * so it is tested on the host with the real small-font width table.
 */
#ifndef VM_TEXT_H
#define VM_TEXT_H

#include "../protocol.h"

/* Width of one character in pixels (e.g. from gSmallFontWidths, or FntCharWidth on the device). */
typedef Int16 (*TxtWidthFn)(Char c);

/* The small font's widths from the generated table. */
Int16  TxtSmallWidth(Char c);

Int16  TxtWidth(const Char *s, UInt16 len, TxtWidthFn w);

/* How many characters of s fit in maxW pixels. */
UInt16 TxtFit(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w);

/*
 * One line of word wrap: returns how many characters to draw on this line
 * (breaking after the last space that fits, or mid-word if a word is longer
 * than the line) and sets *consumed to that plus any spaces to skip before
 * the next line. Returns 0 with *consumed 0 only for empty input.
 */
UInt16 TxtWrap(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w, UInt16 *consumed);

/* "now", "5m", "23h", "3d", "2y" (MeshTerm's aligned-column ages). */
void   TxtFormatAge(Char *out, UInt32 seconds);

/* SNR in quarter dB as a whole number: "+6dB", "-3dB", "0dB". */
void   TxtFormatSnr(Char *out, Int8 quarterDb);

/*
 * MeshCore channel messages carry the sender in the text as "Name: message".
 * Returns the name's length (0 if there's no such prefix); the message starts
 * at name length + 2.
 */
UInt16 TxtSplitSender(const Char *text, UInt16 len);

/*
 * UTF-8 to the Visor's Latin-1 / Windows-1252 character set. Characters it
 * can't show become '?'. Writes at most outSize - 1 bytes plus a terminator
 * and returns the length.
 */
UInt16 TxtUtf8ToPalm(const UInt8 *in, UInt16 inLen, Char *out, UInt16 outSize);

#endif
