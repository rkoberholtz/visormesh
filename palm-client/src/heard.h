/*
 * Signal history: who the radio heard directly, and how well, from its RX log
 * pushes. Each entry is a node heard at one time: a zero-hop advert names the
 * advertiser by key prefix, a flooded packet names its last relay by path
 * hash (1-3 bytes). Repeat hearings of the same node within a minute update
 * one entry. Kept in memory and in an unsaved preference across launches.
 */
#ifndef VM_HEARD_H
#define VM_HEARD_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the third code segment */
#include "protocol.h"

#define VM_HEARD_MAX 48

typedef struct {
	UInt32  time;           /* Unix seconds of the latest hearing */
	UInt8   id[6];          /* key prefix or path hash */
	UInt8   idLen;
	UInt8   count;          /* hearings merged into this entry */
	Int8    snrQ;           /* the latest: quarter dB */
	Int8    rssi;
	UInt8   kind;           /* VM_HEARD_ADVERT (its own advert) or VM_HEARD_RELAY (relaying a packet) */
	UInt8   advertHops;     /* VM_HEARD_ADVERT_FAR: an advert that came through repeaters */
} VmHeard;

#define VM_HEARD_ADVERT     0
#define VM_HEARD_RELAY      1
#define VM_HEARD_ADVERT_FAR 2   /* its advert, via advertHops repeaters: the signal is the last relay's */

void    HeardLoad(void) VM_SECT3;
void    HeardSave(void) VM_SECT3;
void    HeardRecord(const VmRxLog *rx, UInt32 now) VM_SECT3;
/*
 * The entries for a node, newest first: fills up to max, returns how many.
 * Relay entries only match when relays is true (repeaters: a companion
 * doesn't forward, so a hash match would be another node).
 */
UInt16  HeardFor(const UInt8 *pubKey, Boolean relays, VmHeard *out, UInt16 max) VM_SECT3;

#endif
