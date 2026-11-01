#include "protocol.h"

enum { ST_SYNC, ST_LEN_LO, ST_LEN_HI, ST_PAYLOAD };

UInt16 VmEncodeFrame(UInt8 *out, UInt16 outSize, const UInt8 *payload, UInt16 len)
{
	UInt16 i;

	if (len > VM_MAX_FRAME || outSize < (UInt16)(len + VM_HEADER_LEN))
		return 0;
	out[0] = VM_FRAME_TO_RADIO;
	out[1] = (UInt8)(len & 0xFF);
	out[2] = (UInt8)(len >> 8);
	for (i = 0; i < len; i++)
		out[VM_HEADER_LEN + i] = payload[i];
	return (UInt16)(len + VM_HEADER_LEN);
}

void VmDecoderReset(VmFrameDecoder *d)
{
	d->state = ST_SYNC;
	d->len = 0;
	d->pos = 0;
}

Boolean VmDecoderFeed(VmFrameDecoder *d, UInt8 byte)
{
	switch (d->state) {
	case ST_SYNC:
		if (byte == VM_FRAME_FROM_RADIO)
			d->state = ST_LEN_LO;
		return 0;
	case ST_LEN_LO:
		d->len = byte;
		d->state = ST_LEN_HI;
		return 0;
	case ST_LEN_HI:
		d->len |= (UInt16)byte << 8;
		d->pos = 0;
		if (d->len > VM_MAX_FRAME) {
			VmDecoderReset(d);
			return 0;
		}
		if (d->len == 0) {
			d->state = ST_SYNC;
			return 1;
		}
		d->state = ST_PAYLOAD;
		return 0;
	case ST_PAYLOAD:
		d->payload[d->pos++] = byte;
		if (d->pos == d->len) {
			d->state = ST_SYNC;
			return 1;
		}
		return 0;
	}
	VmDecoderReset(d);
	return 0;
}
