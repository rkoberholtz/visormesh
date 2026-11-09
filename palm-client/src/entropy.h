/*
 * A random-number pool for new channel keys. Palm OS's SysRandom is a
 * seeded LCG, so the pool gathers what can't be predicted instead: the
 * exact tick of every pen and key event, where the pen was, and the signal
 * readings of every packet the radio reports. Samples go into a 64-byte
 * buffer that's folded into a SHA-256 pool each time it fills; a key is a
 * hash of the pool, which then moves on, so a key never shows the pool.
 *
 * The pool lives across launches in an unsaved preference (app.c), never
 * in a HotSync backup. Pure C: tests/test_entropy.c drives it on the host.
 */
#ifndef VM_ENTROPY_H
#define VM_ENTROPY_H

#include "protocol.h"

#define VM_ENTROPY_STATE  32
#define VM_ENTROPY_MIN    256   /* samples before a key: at half a bit each, 128 bits */

/* n bytes; credit counts it as one sample toward VM_ENTROPY_MIN. */
void    EntropyAdd(const UInt8 *p, UInt16 n, Boolean credit) VM_SECT5;
/* An event from the loop; pen and key events count as samples. */
void    EntropyEvent(UInt32 ticks, UInt16 type, Int16 x, Int16 y, UInt16 chr, Boolean credit) VM_SECT5;
/* Samples so far, up to VM_ENTROPY_MIN and a bit over. */
UInt16  EntropyCount(void) VM_SECT5;
/* n random bytes (n <= 32); false while there are too few samples. */
Boolean EntropyKey(UInt8 *out, UInt16 n) VM_SECT5;

/* For keeping it between launches: the pool (folded) and its count. */
void    EntropySave(UInt8 state[VM_ENTROPY_STATE], UInt16 *count) VM_SECT5;
void    EntropyLoad(const UInt8 state[VM_ENTROPY_STATE], UInt16 count) VM_SECT5;

#endif
