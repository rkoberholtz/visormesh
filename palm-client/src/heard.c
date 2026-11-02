#include <PalmOS.h>
#include "heard.h"
#include "app.h"

/* In an extra code segment (VM_SECT3): see sections.h for the rules. */
static void Add(UInt8 kind, const UInt8 *id, UInt8 idLen, const VmRxLog *rx, UInt32 now) VM_SECT3;

#define VM_HEARD_PREFS_ID      1
#define VM_HEARD_PREFS_VERSION 1
#define MERGE_SECS             60

static VmHeard gHeard[VM_HEARD_MAX];   /* ring, gNext is the oldest slot */
static UInt16  gUsed = 0, gNext = 0;

void HeardLoad(void)
{
	UInt16 size = sizeof(gHeard);

	if (PrefGetAppPreferences(VM_CREATOR, VM_HEARD_PREFS_ID, gHeard, &size, false) == VM_HEARD_PREFS_VERSION &&
	    size == sizeof(gHeard)) {
		for (gUsed = 0; gUsed < VM_HEARD_MAX && gHeard[gUsed].time; gUsed++)
			;
		gNext = gUsed % VM_HEARD_MAX;
		/* a full ring: the oldest is after the newest */
		if (gUsed == VM_HEARD_MAX) {
			UInt16 i, newest = 0;

			for (i = 1; i < VM_HEARD_MAX; i++)
				if (gHeard[i].time >= gHeard[newest].time)
					newest = i;
			gNext = (newest + 1) % VM_HEARD_MAX;
		}
	} else {
		MemSet(gHeard, sizeof(gHeard), 0);
		gUsed = gNext = 0;
	}
}

void HeardSave(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_HEARD_PREFS_ID, VM_HEARD_PREFS_VERSION, gHeard, sizeof(gHeard), false);
}

static void Add(UInt8 kind, const UInt8 *id, UInt8 idLen, const VmRxLog *rx, UInt32 now)
{
	UInt16 i;
	VmHeard *h;

	/* the same node heard the same way within a minute: update that entry */
	for (i = 0; i < gUsed; i++) {
		h = &gHeard[i];
		if (h->kind == kind && h->idLen == idLen && MemCmp(h->id, id, idLen) == 0 &&
		    now >= h->time && now - h->time < MERGE_SECS) {
			h->time = now;
			h->snrQ = (Int8)rx->snrQ;
			h->rssi = rx->rssi;
			if (h->count < 255)
				h->count++;
			return;
		}
	}
	h = &gHeard[gNext];
	MemSet(h, sizeof(*h), 0);
	h->time = now;
	MemMove(h->id, id, idLen);
	h->idLen = idLen;
	h->count = 1;
	h->snrQ = (Int8)rx->snrQ;
	h->rssi = rx->rssi;
	h->kind = kind;
	h->advertHops = rx->hops;
	gNext = (gNext + 1) % VM_HEARD_MAX;
	if (gUsed < VM_HEARD_MAX)
		gUsed++;
}

void HeardRecord(const VmRxLog *rx, UInt32 now)
{
	if (rx->isAdvert)
		Add(rx->hops == 0 ? VM_HEARD_ADVERT : VM_HEARD_ADVERT_FAR, rx->advertKey, 6, rx, now);
	if (rx->fromLen && rx->fromLen < 6)
		Add(VM_HEARD_RELAY, rx->from, rx->fromLen, rx, now);
}

UInt16 HeardFor(const UInt8 *pubKey, Boolean relays, VmHeard *out, UInt16 max)
{
	UInt16 n = 0, k;

	/* newest first: walk back from the slot before gNext */
	for (k = 1; k <= gUsed && n < max; k++) {
		const VmHeard *h = &gHeard[(gNext + VM_HEARD_MAX - k) % VM_HEARD_MAX];

		if (h->kind == VM_HEARD_RELAY && !relays)
			continue;
		if (MemCmp(h->id, pubKey, h->idLen) == 0)
			out[n++] = *h;
	}
	return n;
}
