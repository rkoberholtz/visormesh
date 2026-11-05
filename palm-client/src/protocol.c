#include "protocol.h"

/* In the second code segment: see sections.h for the rules. */
static UInt32 GetLE32(const UInt8 *p) VM_SECT2;
static void   CopyBytes(UInt8 *dst, const UInt8 *src, UInt16 n) VM_SECT2;
static void   CopyName(Char *dst, UInt16 dstSize, const UInt8 *src, UInt16 n) VM_SECT2;
static void   PutLE32(UInt8 *p, UInt32 v) VM_SECT2;

enum { ST_SYNC, ST_LEN_LO, ST_LEN_HI, ST_PAYLOAD };

#define DEVICE_INFO_MIN_LEN  80     /* through the version string */
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

Boolean VmParseDecimal(const Char *s, UInt16 scale, UInt32 *out)
{
	UInt32 v = 0;
	UInt16 decimals = 0, digits = 0;
	Boolean dot = 0;

	for (; *s; s++) {
		if (*s == '.') {
			if (dot)
				return 0;
			dot = 1;
			continue;
		}
		if (*s < '0' || *s > '9')
			return 0;
		if (dot && ++decimals > scale)
			return 0;
		if (v > 400000000UL)     /* keep v * 10 well inside 32 bits */
			return 0;
		v = v * 10 + (UInt32)(*s - '0');
		digits++;
	}
	if (digits == 0)
		return 0;
	for (; decimals < scale; decimals++) {
		if (v > 400000000UL)
			return 0;
		v *= 10;
	}
	*out = v;
	return 1;
}

void VmFormatDecimal(Char *out, UInt32 value, UInt16 scale)
{
	Char tmp[12];
	UInt16 n = 0, i, intDigits, frac;
	Char *p = out;

	do {
		tmp[n++] = (Char)('0' + value % 10);
		value /= 10;
	} while (value);
	while (n <= scale)
		tmp[n++] = '0';             /* at least one integer digit */
	intDigits = n - scale;
	for (i = 0; i < intDigits; i++)
		*p++ = tmp[n - 1 - i];
	/* fractional part without trailing zeros */
	frac = scale;
	while (frac > 0 && tmp[scale - frac] == '0')
		frac--;
	if (frac) {
		*p++ = '.';
		for (i = 0; i < frac; i++)
			*p++ = tmp[scale - 1 - i];
	}
	*p = 0;
}

Boolean VmParseInt(const Char *s, Int32 *out)
{
	Boolean neg = 0;
	Int32 v = 0;
	UInt16 digits = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || digits >= 9)
			return 0;
		v = v * 10 + (*s - '0');
		digits++;
	}
	if (digits == 0)
		return 0;
	*out = neg ? -v : v;
	return 1;
}

Boolean VmParseHex(const Char *s, UInt8 *out, UInt16 n)
{
	UInt16 got = 0;
	UInt8 v = 0;

	for (; *s; s++) {
		UInt8 d;

		if (*s == ' ')
			continue;
		if (*s >= '0' && *s <= '9')
			d = (UInt8)(*s - '0');
		else if (*s >= 'a' && *s <= 'f')
			d = (UInt8)(*s - 'a' + 10);
		else if (*s >= 'A' && *s <= 'F')
			d = (UInt8)(*s - 'A' + 10);
		else
			return 0;
		if (got >= 2 * n)
			return 0;
		v = (UInt8)((v << 4) | d);
		if (got++ & 1)
			out[got / 2 - 1] = v;
	}
	return got == 2 * n;
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

static void PutLE32(UInt8 *p, UInt32 v)
{
	p[0] = (UInt8)v;
	p[1] = (UInt8)(v >> 8);
	p[2] = (UInt8)(v >> 16);
	p[3] = (UInt8)(v >> 24);
}

/* CMD_SET_ADVERT_NAME: code, name (no terminator; the firmware keeps up to 31 chars). */
UInt16 VmBuildSetAdvertName(UInt8 *out, UInt16 outSize, const Char *name)
{
	UInt8 p[1 + VM_NAME_MAX];
	UInt16 n = 0;

	p[0] = VM_CMD_SET_ADVERT_NAME;
	while (name[n] && n < VM_NAME_MAX) {
		p[1 + n] = (UInt8)name[n];
		n++;
	}
	if (n == 0)
		return 0;
	return VmEncodeFrame(out, outSize, p, (UInt16)(1 + n));
}

/*
 * CMD_SET_RADIO_PARAMS: code, freq (kHz), bw (Hz), sf, cr, [client repeat, v9+].
 * Sending repeat = 0 turns client repeat off, so pass through what DEVICE_INFO reported.
 */
UInt16 VmBuildSetRadioParams(UInt8 *out, UInt16 outSize, UInt32 freqKHz, UInt32 bwHz, UInt8 sf, UInt8 cr,
                             Boolean includeRepeat, Boolean repeat)
{
	UInt8 p[12];

	p[0] = VM_CMD_SET_RADIO_PARAMS;
	PutLE32(p + 1, freqKHz);
	PutLE32(p + 5, bwHz);
	p[9] = sf;
	p[10] = cr;
	if (!includeRepeat)
		return VmEncodeFrame(out, outSize, p, 11);
	p[11] = repeat ? 1 : 0;
	return VmEncodeFrame(out, outSize, p, 12);
}

UInt16 VmBuildSetTxPower(UInt8 *out, UInt16 outSize, Int8 dbm)
{
	UInt8 p[2];

	p[0] = VM_CMD_SET_RADIO_TX_POWER;
	p[1] = (UInt8)dbm;
	return VmEncodeFrame(out, outSize, p, 2);
}

/* CMD_SET_PATH_HASH_MODE: code, 0 (sub-command), mode (path hash bytes - 1). */
UInt16 VmBuildSetPathHashMode(UInt8 *out, UInt16 outSize, UInt8 mode)
{
	UInt8 p[3];

	p[0] = VM_CMD_SET_PATH_HASH_MODE;
	p[1] = 0;
	p[2] = mode;
	return VmEncodeFrame(out, outSize, p, 3);
}

UInt16 VmBuildGetChannel(UInt8 *out, UInt16 outSize, UInt8 idx)
{
	UInt8 p[2];

	p[0] = VM_CMD_GET_CHANNEL;
	p[1] = idx;
	return VmEncodeFrame(out, outSize, p, 2);
}

/* CMD_SET_CHANNEL: code, idx, name[32] (NUL-padded), secret[16]. */
UInt16 VmBuildSetChannel(UInt8 *out, UInt16 outSize, UInt8 idx, const UInt8 *name, UInt16 nameLen,
                         const UInt8 *secret)
{
	UInt8 p[2 + VM_NAME_SIZE + VM_CHANNEL_SECRET_SIZE];
	UInt16 i;

	if (nameLen > VM_NAME_SIZE - 1)
		return 0;
	p[0] = VM_CMD_SET_CHANNEL;
	p[1] = idx;
	for (i = 0; i < VM_NAME_SIZE; i++)
		p[2 + i] = i < nameLen ? name[i] : 0;
	CopyBytes(p + 2 + VM_NAME_SIZE, secret, VM_CHANNEL_SECRET_SIZE);
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

/* CMD_SEND_TXT_MSG: code, txt_type, attempt, timestamp, recipient key prefix[6], text. */
UInt16 VmBuildSendTxt(UInt8 *out, UInt16 outSize, const UInt8 *prefix6, UInt32 timestamp, UInt8 attempt,
                      const UInt8 *text, UInt16 textLen)
{
	UInt8 p[13 + VM_MAX_DM_TEXT];

	if (textLen == 0 || textLen > VM_MAX_DM_TEXT)
		return 0;
	p[0] = VM_CMD_SEND_TXT_MSG;
	p[1] = VM_TXT_TYPE_PLAIN;
	p[2] = attempt;
	PutLE32(p + 3, timestamp);
	CopyBytes(p + 7, prefix6, 6);
	CopyBytes(p + 13, text, textLen);
	return VmEncodeFrame(out, outSize, p, (UInt16)(13 + textLen));
}

/* CMD_SEND_CHANNEL_TXT_MSG: code, txt_type, channel_idx, timestamp, text. The radio adds "Name: ". */
UInt16 VmBuildSendChannelTxt(UInt8 *out, UInt16 outSize, UInt8 channelIdx, UInt32 timestamp,
                             const UInt8 *text, UInt16 textLen)
{
	UInt8 p[7 + VM_MAX_CHANNEL_TEXT];

	if (textLen == 0 || textLen > VM_MAX_CHANNEL_TEXT)
		return 0;
	p[0] = VM_CMD_SEND_CHANNEL_TXT_MSG;
	p[1] = VM_TXT_TYPE_PLAIN;
	p[2] = channelIdx;
	PutLE32(p + 3, timestamp);
	CopyBytes(p + 7, text, textLen);
	return VmEncodeFrame(out, outSize, p, (UInt16)(7 + textLen));
}

UInt16 VmBuildGetTime(UInt8 *out, UInt16 outSize)
{
	UInt8 p[1];

	p[0] = VM_CMD_GET_DEVICE_TIME;
	return VmEncodeFrame(out, outSize, p, 1);
}

/* CMD_SET_DEVICE_TIME: the firmware only accepts a time at or after its own clock. */
UInt16 VmBuildSetTime(UInt8 *out, UInt16 outSize, UInt32 unixSeconds)
{
	UInt8 p[5];

	p[0] = VM_CMD_SET_DEVICE_TIME;
	PutLE32(p + 1, unixSeconds);
	return VmEncodeFrame(out, outSize, p, 5);
}

/* CMD_SEND_SELF_ADVERT: 1 = flood, 0 = zero hop (neighbours only). */
UInt16 VmBuildSendAdvert(UInt8 *out, UInt16 outSize, Boolean flood)
{
	UInt8 p[2];

	p[0] = VM_CMD_SEND_SELF_ADVERT;
	p[1] = flood ? 1 : 0;
	return VmEncodeFrame(out, outSize, p, 2);
}

/* CMD_SEND_LOGIN: code, pub_key[32], password (rest of frame). */
UInt16 VmBuildLogin(UInt8 *out, UInt16 outSize, const UInt8 *pubKey, const UInt8 *password, UInt16 pwLen)
{
	UInt8 p[1 + VM_PUB_KEY_SIZE + VM_NAME_SIZE];

	if (pwLen > VM_NAME_SIZE)
		return 0;
	p[0] = VM_CMD_SEND_LOGIN;
	CopyBytes(p + 1, pubKey, VM_PUB_KEY_SIZE);
	CopyBytes(p + 1 + VM_PUB_KEY_SIZE, password, pwLen);
	return VmEncodeFrame(out, outSize, p, (UInt16)(1 + VM_PUB_KEY_SIZE + pwLen));
}

UInt16 VmBuildKeyCommand(UInt8 *out, UInt16 outSize, UInt8 code, const UInt8 *pubKey)
{
	UInt8 p[1 + VM_PUB_KEY_SIZE];

	p[0] = code;
	CopyBytes(p + 1, pubKey, VM_PUB_KEY_SIZE);
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

/* CMD_ADD_UPDATE_CONTACT: code, pub_key[32], type, flags, out_path_len, out_path[64], name[32], last_advert. */
UInt16 VmBuildAddContact(UInt8 *out, UInt16 outSize, const UInt8 *pubKey, UInt8 type,
                         const UInt8 *name, UInt16 nameLen)
{
	UInt8 p[1 + VM_PUB_KEY_SIZE + 3 + VM_MAX_PATH_SIZE + VM_NAME_SIZE + 4];
	UInt16 i;

	if (nameLen == 0 || nameLen > VM_NAME_SIZE - 1)
		return 0;
	for (i = 0; i < sizeof(p); i++)
		p[i] = 0;
	p[0] = VM_CMD_ADD_UPDATE_CONTACT;
	CopyBytes(p + 1, pubKey, VM_PUB_KEY_SIZE);
	p[1 + VM_PUB_KEY_SIZE] = type;
	p[1 + VM_PUB_KEY_SIZE + 2] = VM_PATH_UNKNOWN;           /* no path yet: flood */
	CopyBytes(p + 1 + VM_PUB_KEY_SIZE + 3 + VM_MAX_PATH_SIZE, name, nameLen);
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

/* CMD_REBOOT: code + "reboot" (the firmware checks the word, so a stray byte can't reboot it). */
UInt16 VmBuildReboot(UInt8 *out, UInt16 outSize)
{
	static UInt8 p[7] = { VM_CMD_REBOOT, 'r', 'e', 'b', 'o', 'o', 't' };   /* not const: sections.h */

	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

/*
 * DEVICE_INFO: code, fw_ver_code, max_contacts / 2, max_channels, ble_pin (4),
 * build_date[12], manufacturer[40], version[20], then optional newer fields.
 */
Boolean VmParseDeviceInfo(const UInt8 *p, UInt16 len, VmDeviceInfo *out)
{
	if (len < DEVICE_INFO_MIN_LEN || p[0] != VM_RESP_DEVICE_INFO)
		return 0;
	out->fwVerCode = p[1];
	out->maxContacts = (UInt16)p[2] * 2;
	out->maxChannels = p[3];
	CopyName(out->buildDate, sizeof(out->buildDate), p + 8, 12);
	CopyName(out->manufacturer, sizeof(out->manufacturer), p + 20, 40);
	CopyName(out->version, sizeof(out->version), p + 60, 20);
	out->hasRepeat = len > 80;
	out->repeatEnabled = out->hasRepeat && p[80] != 0;
	out->hasPathHashMode = len > 81;
	out->pathHashMode = out->hasPathHashMode ? p[81] : 0;
	return 1;
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
	out->txPowerDbm = (Int8)p[2];
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
	out->hasSigner = 0;
	if (!out->isChannel && out->txtType == VM_TXT_TYPE_SIGNED_PLAIN) {
		if (len < i + 4)
			return 0;
		CopyBytes(out->signer, p + i, 4);    /* the author, e.g. of a room post */
		out->hasSigner = 1;
		i += 4;
	}
	out->text = p + i;
	out->textLen = (UInt16)(len - i);
	return 1;
}

/* CHANNEL_INFO: code, idx, name[32], secret[16]. */
Boolean VmParseChannelInfo(const UInt8 *p, UInt16 len, VmChannel *out)
{
	if (len < 2 + VM_NAME_SIZE + VM_CHANNEL_SECRET_SIZE || p[0] != VM_RESP_CHANNEL_INFO)
		return 0;
	out->idx = p[1];
	CopyName(out->name, sizeof(out->name), p + 2, VM_NAME_SIZE);
	CopyBytes(out->secret, p + 2 + VM_NAME_SIZE, VM_CHANNEL_SECRET_SIZE);
	return 1;
}

/* SENT: code, flood, expected_ack, est_timeout (ms). */
Boolean VmParseSent(const UInt8 *p, UInt16 len, VmSent *out)
{
	if (len < 10 || p[0] != VM_RESP_SENT)
		return 0;
	out->flood = p[1] != 0;
	out->expectedAck = GetLE32(p + 2);
	out->timeoutMs = GetLE32(p + 6);
	return 1;
}

/* PUSH_SEND_CONFIRMED: code, ack, round trip (ms). The same ack can arrive more than once. */
Boolean VmParseSendConfirmed(const UInt8 *p, UInt16 len, UInt32 *ack, UInt32 *tripMs)
{
	if (len < 9 || p[0] != VM_PUSH_SEND_CONFIRMED)
		return 0;
	*ack = GetLE32(p + 1);
	*tripMs = GetLE32(p + 5);
	return 1;
}

Boolean VmParseCurrTime(const UInt8 *p, UInt16 len, UInt32 *unixSeconds)
{
	if (len < 5 || p[0] != VM_RESP_CURR_TIME)
		return 0;
	*unixSeconds = GetLE32(p + 1);
	return 1;
}

/* LOGIN_SUCCESS: code, permissions, prefix[6], ...; LOGIN_FAIL: code, 0, prefix[6]. */
Boolean VmParseLoginResult(const UInt8 *p, UInt16 len, Boolean *ok, Boolean *isAdmin, UInt8 *prefix6)
{
	if (len < 8 || (p[0] != VM_PUSH_LOGIN_SUCCESS && p[0] != VM_PUSH_LOGIN_FAIL))
		return 0;
	*ok = p[0] == VM_PUSH_LOGIN_SUCCESS;
	*isAdmin = *ok && (p[1] & 1);
	CopyBytes(prefix6, p + 2, 6);
	return 1;
}

/* LOG_RX: code, snr*4, rssi, then the raw packet: header, [transport codes], path_len, path, payload. */
Boolean VmParseRxLog(const UInt8 *p, UInt16 len, VmRxLog *out)
{
	UInt16 i = 3, pathBytes;
	UInt8 header, route, size;

	if (len < 5 || p[0] != VM_PUSH_LOG_RX)
		return 0;
	out->snrQ = (Int8)p[1];
	out->rssi = (Int8)p[2];
	header = p[i++];
	route = header & 0x03;
	out->payloadType = (UInt8)((header >> 2) & 0x0F);
	if (route == 0 || route == 3)            /* transport codes */
		i += 4;
	if (len < i + 1)
		return 0;
	out->hops = VM_PATH_HOPS(p[i]);
	size = VM_PATH_HASH_BYTES(p[i]);
	if (size > 3)
		return 0;
	pathBytes = (UInt16)out->hops * size;
	i++;
	if (len < i + pathBytes)
		return 0;
	out->isAdvert = 0;
	out->fromLen = 0;
	out->flood = route == 1 || route == 0;
	out->hashSize = size;
	out->pathBytes = (UInt8)(pathBytes > VM_RX_PATH_MAX ? VM_RX_PATH_MAX : pathBytes);
	CopyBytes(out->path, p + i, out->pathBytes);
	out->payloadLen = (UInt8)(len - i - pathBytes > 8 ? 8 : len - i - pathBytes);
	CopyBytes(out->payload, p + i + pathBytes, out->payloadLen);
	if (out->payloadType == VM_PAYLOAD_ADVERT && len >= i + pathBytes + 6) {
		out->isAdvert = 1;
		CopyBytes(out->advertKey, p + i + pathBytes, 6);
	}
	if (out->flood) {                        /* the path is who relayed it, in order */
		if (out->hops > 0) {
			out->fromLen = size;
			CopyBytes(out->from, p + i + pathBytes - size, size);
		} else if (out->isAdvert) {
			out->fromLen = 6;
			CopyBytes(out->from, out->advertKey, 6);
		}
	}
	return 1;
}
