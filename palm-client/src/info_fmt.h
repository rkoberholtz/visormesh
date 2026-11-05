/*
 * Text shared by the detail alerts (a message's path, a heard packet, a
 * trace): clock times and repeater lists with the names of matching
 * contacts. Each appends to out, a string of size bytes, and stops short of
 * the end.
 */
#ifndef VM_INFO_FMT_H
#define VM_INFO_FMT_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

/* "label HH:MM:SS" in local time, with the date if it isn't today. */
void InfoClock(Char *out, UInt16 size, const Char *label, UInt32 utc) VM_SECT5;
/* "3F2A RptA": a hop's hash and up to 12 characters of a contact it matches. */
void InfoHop(Char *out, UInt16 size, const UInt8 *hash, UInt8 hashSize) VM_SECT5;
/* " 3F2A RptA, 91C0, ..." for pathBytes of hops (hashSize each); "+N more" if hops is more than that. */
void InfoVia(Char *out, UInt16 size, const UInt8 *path, UInt16 pathBytes, UInt8 hashSize, UInt16 hops) VM_SECT5;

#endif
