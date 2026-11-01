#include "protocol.h"

enum { ST_SYNC, ST_LEN_LO, ST_LEN_HI, ST_PAYLOAD };

#define SELF_INFO_FIXED_LEN  58     /* everything before the variable-length name */
#define CONTACT_LEN          (1 + VM_PUB_KEY_SIZE + 3 + VM_MAX_PATH_SIZE + VM_NAME_SIZE + 16)
#define MSG_V3_HEADER_LEN    4      /* code, snr, 2 reserved */

static UInt32 GetLE32(const UInt8 *p)
{
	return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

static void CopyBytes(UInt8 *dst, const UInt8 *src, UInt16 n)
{
	while (n--)
		*dst++ = *src++;
}

/* Copies up to n bytes of a possibly unterminated string, always NUL-terminating dst (size n + 1 or more). */
static void CopyName(Char *dst, UInt16 dstSize, const UInt8 *src, UInt16 n)
{
	UInt16 i;

	if (n > dstSize - 1)
		n = dstSize - 1;
	for (i = 0; i < n && src[i]; i++)
		dst[i] = (Char)src[i];
	dst[i] = 0;
}

UInt16 VmEncodeFrame(UInt8 *out, UInt16 outSize, const UInt8 *payload, UInt16 len)
{
	if (len > VM_MAX_FRAME || outSize < (UInt16)(len + VM_HEADER_LEN))
		return 0;
	out[0] = VM_FRAME_TO_RADIO;
	out[1] = (UInt8)(len & 0xFF);
	out[2] = (UInt8)(len >> 8);
	CopyBytes(out + VM_HEADER_LEN, payload, len);
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

UInt16 VmBuildDeviceQuery(UInt8 *out, UInt16 outSize)
{
	UInt8 p[2];

	p[0] = VM_CMD_DEVICE_QUERY;
	p[1] = VM_APP_TARGET_VER;
	return VmEncodeFrame(out, outSize, p, 2);
}

/* CMD_APP_START: code, 7 reserved bytes, app name (no terminator). */
UInt16 VmBuildAppStart(UInt8 *out, UInt16 outSize, const Char *appName)
{
	UInt8 p[8 + VM_NAME_SIZE];
	UInt16 n = 0, i;

	p[0] = VM_CMD_APP_START;
	for (i = 1; i < 8; i++)
		p[i] = 0;
	while (appName[n] && n < VM_NAME_SIZE) {
		p[8 + n] = (UInt8)appName[n];
		n++;
	}
	return VmEncodeFrame(out, outSize, p, (UInt16)(8 + n));
}

/* CMD_GET_CONTACTS with optional 'since' (lastmod of the newest contact already held; 0 = all). */
UInt16 VmBuildGetContacts(UInt8 *out, UInt16 outSize, UInt32 since)
{
	UInt8 p[5];

	p[0] = VM_CMD_GET_CONTACTS;
	if (since == 0)
		return VmEncodeFrame(out, outSize, p, 1);
	p[1] = (UInt8)since;
	p[2] = (UInt8)(since >> 8);
	p[3] = (UInt8)(since >> 16);
	p[4] = (UInt8)(since >> 24);
	return VmEncodeFrame(out, outSize, p, 5);
}

UInt16 VmBuildSyncNextMessage(UInt8 *out, UInt16 outSize)
{
	UInt8 p[1];

	p[0] = VM_CMD_SYNC_NEXT_MESSAGE;
	return VmEncodeFrame(out, outSize, p, 1);
}

/*
 * SELF_INFO: code, adv_type, tx_power, max_tx_power, pub_key[32], lat, lon,
 * multi_acks, advert_loc_policy, telemetry_modes, manual_add_contacts,
 * freq (kHz), bw (Hz), sf, cr, name (rest of frame, unterminated).
 */
Boolean VmParseSelfInfo(const UInt8 *p, UInt16 len, VmSelfInfo *out)
{
	if (len < SELF_INFO_FIXED_LEN || p[0] != VM_RESP_SELF_INFO)
		return 0;
	out->advType = p[1];
	out->txPowerDbm = p[2];
	out->maxTxPowerDbm = p[3];
	CopyBytes(out->pubKey, p + 4, VM_PUB_KEY_SIZE);
	out->latE6 = (Int32)GetLE32(p + 36);
	out->lonE6 = (Int32)GetLE32(p + 40);
	/* p[44..47]: multi_acks, advert_loc_policy, telemetry modes, manual_add_contacts */
	out->freqKHz = GetLE32(p + 48);
	out->bwHz = GetLE32(p + 52);
	out->sf = p[56];
	out->cr = p[57];
	CopyName(out->name, VM_NAME_SIZE, p + SELF_INFO_FIXED_LEN, (UInt16)(len - SELF_INFO_FIXED_LEN));
	return 1;
}

Boolean VmParseContactsStart(const UInt8 *p, UInt16 len, UInt32 *count)
{
	if (len < 5 || p[0] != VM_RESP_CONTACTS_START)
		return 0;
	*count = GetLE32(p + 1);
	return 1;
}

/*
 * CONTACT: code, pub_key[32], type, flags, out_path_len, out_path[64],
 * name[32], last_advert, lat, lon, lastmod.
 */
Boolean VmParseContact(const UInt8 *p, UInt16 len, VmContact *out)
{
	const UInt8 *q;

	if (len < CONTACT_LEN || p[0] != VM_RESP_CONTACT)
		return 0;
	CopyBytes(out->pubKey, p + 1, VM_PUB_KEY_SIZE);
	q = p + 1 + VM_PUB_KEY_SIZE;
	out->type = q[0];
	out->flags = q[1];
	out->outPathLen = q[2];
	q += 3 + VM_MAX_PATH_SIZE;
	CopyName(out->name, VM_NAME_SIZE, q, VM_NAME_SIZE);
	q += VM_NAME_SIZE;
	out->lastAdvert = GetLE32(q);
	out->latE6 = (Int32)GetLE32(q + 4);
	out->lonE6 = (Int32)GetLE32(q + 8);
	out->lastMod = GetLE32(q + 12);
	return 1;
}

Boolean VmParseEndOfContacts(const UInt8 *p, UInt16 len, UInt32 *lastMod)
{
	if (len < 5 || p[0] != VM_RESP_END_OF_CONTACTS)
		return 0;
	*lastMod = GetLE32(p + 1);
	return 1;
}

/*
 * v3 message frames (app_target_ver >= 3):
 *   contact: code, snr, rsv, rsv, sender_prefix[6], path_len, txt_type, timestamp, [sig 4 if signed], text
 *   channel: code, snr, rsv, rsv, channel_idx, path_len, txt_type, timestamp, text
 */
Boolean VmParseMessage(const UInt8 *p, UInt16 len, VmMessage *out)
{
	UInt16 i;

	if (len < MSG_V3_HEADER_LEN)
		return 0;
	out->snrQuarterDb = (Int8)p[1];
	i = MSG_V3_HEADER_LEN;

	if (p[0] == VM_RESP_CONTACT_MSG_V3) {
		if (len < i + 6 + 6)
			return 0;
		out->isChannel = 0;
		out->channelIdx = 0;
		CopyBytes(out->senderPrefix, p + i, 6);
		i += 6;
	} else if (p[0] == VM_RESP_CHANNEL_MSG_V3) {
		if (len < i + 1 + 6)
			return 0;
		out->isChannel = 1;
		out->channelIdx = p[i++];
	} else {
		return 0;
	}

	out->pathLen = p[i++];
	out->txtType = p[i++];
	out->timestamp = GetLE32(p + i);
	i += 4;
	if (!out->isChannel && out->txtType == VM_TXT_TYPE_SIGNED_PLAIN) {
		if (len < i + 4)
			return 0;
		i += 4;     /* signer's key prefix */
	}
	out->text = p + i;
	out->textLen = (UInt16)(len - i);
	return 1;
}
