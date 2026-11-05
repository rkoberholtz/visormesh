#include "protocol2.h"

static UInt16 Get16(const UInt8 *p) VM_SECT5;
static UInt32 Get32(const UInt8 *p) VM_SECT5;
static void   Put32(UInt8 *p, UInt32 v) VM_SECT5;
static Char   HexDigit(UInt8 v) VM_SECT5;
static Char   HexUpper(UInt8 v) VM_SECT5;
static Int16  HexValue(Char c) VM_SECT5;
static Boolean Unreserved(UInt8 c) VM_SECT5;
static UInt16 PutStr(Char *out, UInt16 size, UInt16 at, const Char *s) VM_SECT5;
static UInt16 PutEscaped(Char *out, UInt16 size, UInt16 at, const UInt8 *s, UInt16 n) VM_SECT5;
static UInt16 PutHex(Char *out, UInt16 size, UInt16 at, const UInt8 *b, UInt16 n) VM_SECT5;
static Boolean Prefix(const Char *s, UInt16 len, const Char *p) VM_SECT5;
static UInt16 Unescape(const Char *s, UInt16 len, UInt8 *out, UInt16 max) VM_SECT5;
static Boolean HexBytes(const Char *s, UInt16 len, UInt8 *out, UInt16 n) VM_SECT5;
static Boolean TagChar(Char c) VM_SECT5;

static UInt16 Get16(const UInt8 *p)
{
	return (UInt16)(p[0] | ((UInt16)p[1] << 8));
}

static UInt32 Get32(const UInt8 *p)
{
	return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

static void Put32(UInt8 *p, UInt32 v)
{
	p[0] = (UInt8)v;
	p[1] = (UInt8)(v >> 8);
	p[2] = (UInt8)(v >> 16);
	p[3] = (UInt8)(v >> 24);
}

/* Computed, not a table: a function-level const table reads as garbage in an extra segment. */
static Char HexDigit(UInt8 v)
{
	return (Char)(v < 10 ? '0' + v : 'a' + v - 10);
}

static Char HexUpper(UInt8 v)
{
	return (Char)(v < 10 ? '0' + v : 'A' + v - 10);
}

static Int16 HexValue(Char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

UInt16 VmBuildCode(UInt8 *out, UInt16 outSize, UInt8 code)
{
	return VmEncodeFrame(out, outSize, &code, 1);
}

UInt16 VmBuildGetStats(UInt8 *out, UInt16 outSize, UInt8 type)
{
	UInt8 p[2];

	p[0] = VM_CMD_GET_STATS;
	p[1] = type;
	return VmEncodeFrame(out, outSize, p, 2);
}

UInt16 VmBuildGetContactByKey(UInt8 *out, UInt16 outSize, const UInt8 *pubKey)
{
	UInt8 p[1 + VM_PUB_KEY_SIZE];
	UInt16 i;

	p[0] = VM_CMD_GET_CONTACT_BY_KEY;
	for (i = 0; i < VM_PUB_KEY_SIZE; i++)
		p[1 + i] = pubKey[i];
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

/* code, tag, auth (unused: 0), flags (low 2 bits: log2 of the hash size), path */
UInt16 VmBuildTrace(UInt8 *out, UInt16 outSize, UInt32 tag, UInt8 hashSize, const UInt8 *path, UInt16 pathBytes)
{
	UInt8 p[10 + VM_TRACE_MAX];
	UInt16 i;

	if (pathBytes == 0 || pathBytes > VM_TRACE_MAX || (hashSize != 1 && hashSize != 2 && hashSize != 4) ||
	    pathBytes % hashSize != 0)
		return 0;
	p[0] = VM_CMD_SEND_TRACE_PATH;
	Put32(p + 1, tag);
	Put32(p + 5, 0);
	p[9] = hashSize == 1 ? 0 : hashSize == 2 ? 1 : 2;
	for (i = 0; i < pathBytes; i++)
		p[10 + i] = path[i];
	return VmEncodeFrame(out, outSize, p, 10 + pathBytes);
}

/* code, reserved, path bytes, flags, tag, auth, hashes, an SNR per hop, then ours */
Boolean VmParseTrace(const UInt8 *p, UInt16 len, VmTrace *out)
{
	UInt16 bytes, hops, i;

	if (len < 12 || p[0] != VM_PUSH_TRACE_DATA)
		return 0;
	bytes = p[2];
	out->hashSize = (UInt8)(1 << (p[3] & 3));
	hops = bytes / out->hashSize;
	if (bytes > sizeof(out->hashes) || bytes % out->hashSize || len < 12 + bytes + hops + 1)
		return 0;
	out->tag = Get32(p + 4);
	out->hops = (UInt8)hops;
	for (i = 0; i < bytes; i++)
		out->hashes[i] = p[12 + i];
	for (i = 0; i <= hops; i++)
		out->snrQ[i] = (Int8)p[12 + bytes + i];
	return 1;
}

/* code, millivolts, then (newer firmware) storage used and total in KB */
Boolean VmParseBattery(const UInt8 *p, UInt16 len, VmBattery *out)
{
	if (len < 3 || p[0] != VM_RESP_BATT_AND_STORAGE)
		return 0;
	out->battMv = Get16(p + 1);
	out->hasStorage = len >= 11;
	out->usedKb = out->hasStorage ? Get32(p + 3) : 0;
	out->totalKb = out->hasStorage ? Get32(p + 7) : 0;
	return 1;
}

Boolean VmParseStats(const UInt8 *p, UInt16 len, VmStats *out)
{
	if (len < 2 || p[0] != VM_RESP_STATS)
		return 0;
	switch (p[1]) {
	case VM_STATS_CORE:         /* millivolts, uptime, error flags, queue length */
		if (len < 11)
			return 0;
		out->battMv = Get16(p + 2);
		out->uptimeSecs = Get32(p + 4);
		out->errFlags = Get16(p + 8);
		out->queueLen = p[10];
		out->hasCore = 1;
		return 1;
	case VM_STATS_RADIO:        /* noise floor, last RSSI, last SNR x 4, TX and RX airtime */
		if (len < 14)
			return 0;
		out->noiseFloor = (Int16)Get16(p + 2);
		out->lastRssi = (Int8)p[4];
		out->lastSnrQ = (Int8)p[5];
		out->txAirSecs = Get32(p + 6);
		out->rxAirSecs = Get32(p + 10);
		out->hasRadio = 1;
		return 1;
	case VM_STATS_PACKETS:      /* received, sent, sent flood/direct, received flood/direct */
		if (len < 26)
			return 0;
		out->recv = Get32(p + 2);
		out->sent = Get32(p + 6);
		out->sentFlood = Get32(p + 10);
		out->sentDirect = Get32(p + 14);
		out->recvFlood = Get32(p + 18);
		out->recvDirect = Get32(p + 22);
		out->hasPackets = 1;
		return 1;
	}
	return 0;
}

Boolean VmContactPath(const UInt8 *p, UInt16 len, UInt8 *pathLen, UInt8 *path)
{
	UInt16 i;

	if (len < VM_CONTACT_FRAME_LEN || p[0] != VM_RESP_CONTACT)
		return 0;
	*pathLen = p[35];
	for (i = 0; i < VM_MAX_PATH_SIZE; i++)
		path[i] = p[36 + i];
	return 1;
}

/*
 * Sent without the last-modified field, so the radio stamps the change with
 * its clock and the next contact sync picks it up.
 */
UInt16 VmBuildContactRoute(UInt8 *out, UInt16 outSize, const UInt8 *contact, UInt16 len,
                           UInt8 hashSize, const UInt8 *path, UInt8 hops)
{
	UInt8 p[VM_CONTACT_FRAME_LEN - 4];
	UInt16 i, bytes = (UInt16)hops * hashSize;

	if (len < VM_CONTACT_FRAME_LEN || contact[0] != VM_RESP_CONTACT || hashSize < 1 || hashSize > 3 ||
	    hops > 63 || bytes > VM_MAX_PATH_SIZE)
		return 0;
	for (i = 0; i < sizeof(p); i++)
		p[i] = contact[i];
	p[0] = VM_CMD_ADD_UPDATE_CONTACT;
	p[35] = (UInt8)(((hashSize - 1) << 6) | hops);
	for (i = 0; i < VM_MAX_PATH_SIZE; i++)
		p[36 + i] = i < bytes ? path[i] : 0;
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

Int16 VmParseHopCodes(const Char *s, UInt8 *out, UInt16 max, UInt8 *size)
{
	UInt16 i = 0, start, n, hops = 0, b;

	*size = 0;
	for (;;) {
		while (s[i] == ' ' || s[i] == ',' || s[i] == '\n')
			i++;
		if (!s[i])
			return (Int16)hops;
		for (start = i; HexValue(s[i]) >= 0; i++)
			;
		n = i - start;
		if (n == 0 || (s[i] && s[i] != ' ' && s[i] != ',' && s[i] != '\n'))
			return -1;
		if (!*size) {
			if (n != 2 && n != 4 && n != 8)
				return -1;
			*size = (UInt8)(n / 2);
		}
		if (n != *size * 2U || (hops + 1U) * *size > max)
			return -1;
		for (b = 0; b < *size; b++)
			out[hops * *size + b] = (UInt8)(HexValue(s[start + 2 * b]) << 4 | HexValue(s[start + 2 * b + 1]));
		hops++;
	}
}

/* URL characters that need no %-escape (RFC 3986) */
static Boolean Unreserved(UInt8 c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
	       c == '-' || c == '_' || c == '.' || c == '~';
}

/* The Put* helpers append at 'at' and return the new length, or size if it ran out (the caller checks). */
static UInt16 PutStr(Char *out, UInt16 size, UInt16 at, const Char *s)
{
	while (*s && at < size)
		out[at++] = *s++;
	return *s ? size : at;
}

static UInt16 PutEscaped(Char *out, UInt16 size, UInt16 at, const UInt8 *s, UInt16 n)
{
	UInt16 i;

	for (i = 0; i < n && at < size; i++) {
		if (Unreserved(s[i])) {
			out[at++] = (Char)s[i];
		} else if (at + 3 <= size) {
			out[at++] = '%';
			out[at++] = HexUpper(s[i] >> 4);
			out[at++] = HexUpper(s[i] & 15);
		} else {
			return size;
		}
	}
	return i < n ? size : at;
}

static UInt16 PutHex(Char *out, UInt16 size, UInt16 at, const UInt8 *b, UInt16 n)
{
	UInt16 i;

	if (at + 2 * n > size)
		return size;
	for (i = 0; i < n; i++) {
		out[at++] = HexDigit(b[i] >> 4);
		out[at++] = HexDigit(b[i] & 15);
	}
	return at;
}

UInt16 VmShareContact(Char *out, UInt16 size, const UInt8 *name, UInt16 nameLen, const UInt8 *key, UInt8 type)
{
	UInt16 at;

	at = PutStr(out, size, 0, "meshcore://contact/add?name=");
	at = PutEscaped(out, size, at, name, nameLen);
	at = PutStr(out, size, at, "&public_key=");
	at = PutHex(out, size, at, key, VM_PUB_KEY_SIZE);
	at = PutStr(out, size, at, "&type=");
	if (at + 1 >= size)
		return 0;
	out[at++] = (Char)('0' + type % 10);
	return at;
}

UInt16 VmShareChannel(Char *out, UInt16 size, const UInt8 *name, UInt16 nameLen, const UInt8 *secret)
{
	UInt16 at;

	at = PutStr(out, size, 0, "meshcore://channel/add?name=");
	at = PutEscaped(out, size, at, name, nameLen);
	at = PutStr(out, size, at, "&secret=");
	at = PutHex(out, size, at, secret, VM_CHANNEL_SECRET_SIZE);
	return at >= size ? 0 : at;
}

static Boolean Prefix(const Char *s, UInt16 len, const Char *p)
{
	UInt16 i;

	for (i = 0; p[i]; i++)
		if (i >= len || s[i] != p[i])
			return 0;
	return 1;
}

/* A query value: '+' is a space, %XX a byte. Returns the length, or max + 1 if it doesn't fit. */
static UInt16 Unescape(const Char *s, UInt16 len, UInt8 *out, UInt16 max)
{
	UInt16 i, n = 0;
	Int16 hi, lo;

	for (i = 0; i < len; i++) {
		UInt8 c = (UInt8)s[i];

		if (c == '+') {
			c = ' ';
		} else if (c == '%') {
			hi = i + 1 < len ? HexValue(s[i + 1]) : -1;
			lo = i + 2 < len ? HexValue(s[i + 2]) : -1;
			if (hi < 0 || lo < 0)
				return max + 1;
			c = (UInt8)(hi << 4 | lo);
			i += 2;
		}
		if (n >= max)
			return max + 1;
		out[n++] = c;
	}
	return n;
}

static Boolean HexBytes(const Char *s, UInt16 len, UInt8 *out, UInt16 n)
{
	UInt16 i;
	Int16 hi, lo;

	if (len != 2 * n)
		return 0;
	for (i = 0; i < n; i++) {
		hi = HexValue(s[2 * i]);
		lo = HexValue(s[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return 0;
		out[i] = (UInt8)(hi << 4 | lo);
	}
	return 1;
}

Boolean VmShareParse(const Char *s, UInt16 len, VmShare *out)
{
	UInt16 i, start, eq, end;
	Boolean hasKey = 0, hasSecret = 0;

	for (i = 0; i < sizeof(*out); i++)
		((UInt8 *)out)[i] = 0;
	if (Prefix(s, len, "meshcore://contact/add?")) {
		out->kind = VM_SHARE_CONTACT;
		out->type = VM_ADV_TYPE_CHAT;
		i = 23;
	} else if (Prefix(s, len, "meshcore://channel/add?")) {
		out->kind = VM_SHARE_CHANNEL;
		i = 23;
	} else {
		return 0;
	}
	/* name=value pairs; unknown ones (region_scope) are skipped */
	while (i < len) {
		start = i;
		while (i < len && s[i] != '&' && s[i] > ' ')
			i++;
		end = i;
		for (eq = start; eq < end && s[eq] != '='; eq++)
			;
		if (eq < end) {
			const Char *v = s + eq + 1;
			UInt16 vn = end - eq - 1, kn = eq - start;

			if (kn == 4 && Prefix(s + start, kn, "name")) {
				out->nameLen = Unescape(v, vn, out->name, sizeof(out->name) - 1);
				if (out->nameLen >= sizeof(out->name))
					return 0;
			} else if (kn == 10 && Prefix(s + start, kn, "public_key")) {
				hasKey = HexBytes(v, vn, out->key, VM_PUB_KEY_SIZE);
			} else if (kn == 6 && Prefix(s + start, kn, "secret")) {
				hasSecret = HexBytes(v, vn, out->secret, VM_CHANNEL_SECRET_SIZE);
			} else if (kn == 4 && Prefix(s + start, kn, "type") && vn == 1 && v[0] >= '1' && v[0] <= '4') {
				out->type = (UInt8)(v[0] - '0');
			}
		}
		if (i >= len || s[i] != '&')
			break;
		i++;
	}
	if (!out->nameLen)
		return 0;
	return out->kind == VM_SHARE_CONTACT ? hasKey : hasSecret;
}

static Boolean TagChar(Char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

Boolean VmHashtagAt(const Char *text, UInt16 len, UInt16 k, Char *out, UInt16 size)
{
	UInt16 start = k, end, i;

	if (k >= len)
		return 0;
	/* back to the '#' */
	while (start > 0 && text[start] != '#' && TagChar(text[start]))
		start--;
	if (text[start] != '#' || start + 1 >= len || text[start + 1] == '-' || !TagChar(text[start + 1]))
		return 0;
	for (end = start + 1; end < len && TagChar(text[end]); end++)
		;
	if (k >= end || end - start + 1 > size)
		return 0;
	for (i = start; i < end; i++)
		out[i - start] = (Char)(text[i] >= 'A' && text[i] <= 'Z' ? text[i] + 32 : text[i]);
	out[end - start] = 0;
	return 1;
}
