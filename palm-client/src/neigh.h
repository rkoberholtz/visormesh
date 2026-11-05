/*
 * A repeater's neighbours, from the last "neighbors" reply in its admin
 * console, for the Map: lines from the repeater to each neighbour that has a
 * position, shaded by SNR. Kept in memory for VM_NEIGH_SECS.
 */
#ifndef VM_NEIGH_H
#define VM_NEIGH_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

#define VM_NEIGH_MAX  16
#define VM_NEIGH_SECS (30UL * 60UL)

typedef struct {
	UInt8 prefix[4];
	Int16 snrQ;
} VmNeighbour;

/* A CLI reply from the repeater with this key prefix: true if it was a neighbour list (stored). */
Boolean NeighNote(const UInt8 *repeater6, const Char *text, UInt16 len) VM_SECT5;
/* The list if one is recent: fills repeater6 and up to VM_NEIGH_MAX entries, returns how many. */
UInt16  NeighGet(UInt8 *repeater6, VmNeighbour *out) VM_SECT5;

#endif
