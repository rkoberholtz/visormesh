#include "rxpath.h"

static Boolean SamePacket(const VmRxLog *a, const VmRxLog *b) VM_SECT3;

typedef struct {
	UInt32  time;
	Boolean used;
	VmRxLog rx;
} Slot;

static Slot   gSlots[VM_RXPATH_SLOTS];
static UInt16 gNext;      /* the oldest slot, written next */

void RxPathClear(void)
{
	UInt16 i;

	for (i = 0; i < VM_RXPATH_SLOTS; i++)
		gSlots[i].used = 1;
	gNext = 0;
}

void RxPathNote(const VmRxLog *rx, UInt32 now)
{
	if (rx->payloadType != VM_PAYLOAD_TXT && rx->payloadType != VM_PAYLOAD_GRP_TXT)
		return;
	gSlots[gNext].time = now;
	gSlots[gNext].used = 0;
	gSlots[gNext].rx = *rx;
	gNext = (UInt16)((gNext + 1) % VM_RXPATH_SLOTS);
}

static Boolean SamePacket(const VmRxLog *a, const VmRxLog *b)
{
	UInt16 i;

	if (a->payloadType != b->payloadType || a->payloadLen != b->payloadLen || a->payloadLen == 0)
		return 0;
	for (i = 0; i < a->payloadLen; i++)
		if (a->payload[i] != b->payload[i])
			return 0;
	return 1;
}

Boolean RxPathTake(Boolean isChannel, UInt8 pathLen, const UInt8 *senderPrefix, UInt32 now, VmRxLog *out)
{
	UInt8 type = isChannel ? VM_PAYLOAD_GRP_TXT : VM_PAYLOAD_TXT;
	Boolean direct = pathLen == VM_PATH_UNKNOWN;
	UInt16 k, i;
	Slot *s;

	/* oldest first: messages come off the radio's queue in the order they arrived */
	for (k = 0; k < VM_RXPATH_SLOTS; k++) {
		s = &gSlots[(gNext + k) % VM_RXPATH_SLOTS];
		if (s->used || s->rx.payloadType != type || now - s->time > VM_RXPATH_SECS)
			continue;
		if (direct ? s->rx.flood : (!s->rx.flood || s->rx.hops != VM_PATH_HOPS(pathLen)))
			continue;
		/* TXT_MSG payload: dest hash, src hash (one byte each), MAC, ciphertext */
		if (senderPrefix && (s->rx.payloadLen < 2 || s->rx.payload[1] != senderPrefix[0]))
			continue;
		*out = s->rx;
		for (i = 0; i < VM_RXPATH_SLOTS; i++)
			if (!gSlots[i].used && SamePacket(&gSlots[i].rx, out))
				gSlots[i].used = 1;
		s->used = 1;
		return 1;
	}
	return 0;
}
