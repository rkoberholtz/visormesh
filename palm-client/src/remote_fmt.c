#include "remote_fmt.h"

/* In an extra code segment (VM_SECT3): see sections.h for the rules. */
static UInt16 LE16(const UInt8 *p) VM_SECT3;
static UInt32 LE32(const UInt8 *p) VM_SECT3;
static Int32 BE(const UInt8 *p, UInt16 n, Boolean isSigned) VM_SECT3;
static Char *Put(Char *o, const Char *s) VM_SECT3;
static Char *PutU(Char *o, UInt32 v) VM_SECT3;
static Char *PutI(Char *o, Int32 v) VM_SECT3;
static Char *PutFixed(Char *o, Int32 v, UInt16 decimals) VM_SECT3;
static Char *PutDuration(Char *o, UInt32 secs) VM_SECT3;

static UInt16 LE16(const UInt8 *p)
{
	return (UInt16)(p[0] | (p[1] << 8));
}

static UInt32 LE32(const UInt8 *p)
{
	return (UInt32)p[0] | ((UInt32)p[1] << 8) | ((UInt32)p[2] << 16) | ((UInt32)p[3] << 24);
}

/* Cayenne LPP values are big-endian. */
static Int32 BE(const UInt8 *p, UInt16 n, Boolean isSigned)
{
	UInt32 v = 0;
	UInt16 i;

	for (i = 0; i < n; i++)
		v = (v << 8) | p[i];
	if (isSigned && n < 4 && (p[0] & 0x80))
		v |= ~(UInt32)0 << (8 * n);
	return (Int32)v;
}

static Char *Put(Char *o, const Char *s)
{
	while (*s)
		*o++ = *s++;
	*o = 0;
	return o;
}

static Char *PutU(Char *o, UInt32 v)
{
	Char tmp[11];
	UInt16 n = 0;

	do {
		tmp[n++] = (Char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		*o++ = tmp[--n];
	*o = 0;
	return o;
}

static Char *PutI(Char *o, Int32 v)
{
	if (v < 0) {
		*o++ = '-';
		return PutU(o, (UInt32)-v);
	}
	return PutU(o, (UInt32)v);
}

/* v / 10^decimals with that many decimals: (412, 2) -> "4.12". */
static Char *PutFixed(Char *o, Int32 v, UInt16 decimals)
{
	UInt32 div = 1, a;
	UInt16 i;

	for (i = 0; i < decimals; i++)
		div *= 10;
	if (v < 0) {
		*o++ = '-';
		a = (UInt32)-v;
	} else {
		a = (UInt32)v;
	}
	o = PutU(o, a / div);
	if (decimals) {
		UInt32 frac = a % div;

		*o++ = '.';
		for (i = decimals; i > 0; i--) {
			UInt32 d = div / 10, digit;
			UInt16 k;

			for (k = 1, d = 1; k < i; k++)
				d *= 10;
			digit = (frac / d) % 10;
			*o++ = (Char)('0' + digit);
		}
		*o = 0;
	}
	return o;
}

/* "3d 4h", "5h 2m", "12m", "40s". */
static Char *PutDuration(Char *o, UInt32 secs)
{
	if (secs >= 86400UL) {
		o = PutU(o, secs / 86400UL);
		o = Put(o, "d ");
		o = PutU(o, (secs % 86400UL) / 3600);
		return Put(o, "h");
	}
	if (secs >= 3600) {
		o = PutU(o, secs / 3600);
		o = Put(o, "h ");
		o = PutU(o, (secs % 3600) / 60);
		return Put(o, "m");
	}
	if (secs >= 60) {
		o = PutU(o, secs / 60);
		return Put(o, "m");
	}
	o = PutU(o, secs);
	return Put(o, "s");
}

UInt16 VmBuildSendCli(UInt8 *out, UInt16 outSize, const UInt8 *prefix6, UInt32 timestamp,
                      const UInt8 *text, UInt16 textLen)
{
	UInt16 n = VmBuildSendTxt(out, outSize, prefix6, timestamp, 0, text, textLen);

	if (n)
		out[VM_HEADER_LEN + 1] = VM_TXT_TYPE_CLI_DATA;     /* the radio uses its own clock for CLI */
	return n;
}

UInt16 VmBuildStatusReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey)
{
	return VmBuildKeyCommand(out, outSize, VM_CMD_SEND_STATUS_REQ, pubKey);
}

/* CMD_SEND_TELEMETRY_REQ: code, 3 reserved bytes, pub_key[32]. */
UInt16 VmBuildTelemetryReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey)
{
	UInt8 p[4 + VM_PUB_KEY_SIZE];
	UInt16 i;

	p[0] = VM_CMD_SEND_TELEMETRY_REQ;
	p[1] = p[2] = p[3] = 0;
	for (i = 0; i < VM_PUB_KEY_SIZE; i++)
		p[4 + i] = pubKey[i];
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

Boolean VmParseStatus(const UInt8 *p, UInt16 len, UInt8 *prefix6, VmRepeaterStats *s)
{
	UInt16 i;

	if (len < 8 + 48 || p[0] != VM_PUSH_STATUS_RESPONSE)
		return 0;
	for (i = 0; i < 6; i++)
		prefix6[i] = p[2 + i];
	p += 8;
	s->battMv = LE16(p);
	s->txQueue = LE16(p + 2);
	s->noiseFloor = (Int16)LE16(p + 4);
	s->lastRssi = (Int16)LE16(p + 6);
	s->recv = LE32(p + 8);
	s->sent = LE32(p + 12);
	s->txAirSecs = LE32(p + 16);
	s->upSecs = LE32(p + 20);
	s->sentFlood = LE32(p + 24);
	s->sentDirect = LE32(p + 28);
	s->recvFlood = LE32(p + 32);
	s->recvDirect = LE32(p + 36);
	s->errEvents = LE16(p + 40);
	s->lastSnrQ = (Int16)LE16(p + 42);
	s->directDups = LE16(p + 44);
	s->floodDups = LE16(p + 46);
	/* later firmware adds these */
	s->rxAirSecs = len >= 8 + 52 ? LE32(p + 48) : 0;
	s->recvErrors = len >= 8 + 56 ? LE32(p + 52) : 0;
	return 1;
}

Boolean VmParseTelemetry(const UInt8 *p, UInt16 len, UInt8 *prefix6, const UInt8 **data, UInt16 *dataLen)
{
	UInt16 i;

	if (len < 8 || p[0] != VM_PUSH_TELEMETRY_RESPONSE)
		return 0;
	for (i = 0; i < 6; i++)
		prefix6[i] = p[2 + i];
	*data = p + 8;
	*dataLen = (UInt16)(len - 8);
	return 1;
}

void VmFormatStatusLine(const VmRepeaterStats *s, UInt16 n, Char *o)
{
	*o = 0;
	switch (n) {
	case 0:
		o = Put(o, "Battery ");
		o = PutFixed(o, s->battMv, 3);
		o = Put(o, "V, up ");
		o = PutDuration(o, s->upSecs);
		o = Put(o, ", queue ");
		PutU(o, s->txQueue);
		break;
	case 1:
		o = Put(o, "Noise ");
		o = PutI(o, s->noiseFloor);
		o = Put(o, "dBm, last ");
		o = PutI(o, s->lastRssi);
		o = Put(o, "dBm ");
		o = PutFixed(o, s->lastSnrQ * 25L, 2);
		Put(o, "dB");
		break;
	case 2:
		o = Put(o, "Recv ");
		o = PutU(o, s->recv);
		o = Put(o, " (flood ");
		o = PutU(o, s->recvFlood);
		o = Put(o, ", direct ");
		o = PutU(o, s->recvDirect);
		Put(o, ")");
		break;
	case 3:
		o = Put(o, "Sent ");
		o = PutU(o, s->sent);
		o = Put(o, " (flood ");
		o = PutU(o, s->sentFlood);
		o = Put(o, ", direct ");
		o = PutU(o, s->sentDirect);
		Put(o, ")");
		break;
	case 4:
		o = Put(o, "Airtime tx ");
		o = PutDuration(o, s->txAirSecs);
		o = Put(o, ", rx ");
		PutDuration(o, s->rxAirSecs);
		break;
	case 5:
		o = Put(o, "Dups flood ");
		o = PutU(o, s->floodDups);
		o = Put(o, ", direct ");
		o = PutU(o, s->directDups);
		o = Put(o, "; errors ");
		o = PutU(o, s->recvErrors);
		o = Put(o, ", events ");
		PutU(o, s->errEvents);
		break;
	}
}

/* Cayenne LPP: channel, type, value. Sizes and scales from MeshCore's LPPDataHelpers.h. */
Boolean VmLppNext(const UInt8 *d, UInt16 len, UInt16 *pos, Char *o)
{
	UInt16 i = *pos, size;
	UInt8 ch, type;
	const UInt8 *v;

	if (i + 2 > len)
		return 0;
	ch = d[i];
	type = d[i + 1];
	if (ch == 0 && type == 0 && i + 2 >= len)
		return 0;                      /* padding */
	switch (type) {
	case 0: case 1: case 102: case 104: case 120: case 142: size = 1; break;
	case 2: case 3: case 101: case 103: case 115: case 116: case 117: case 121: case 125: case 128: case 132: size = 2; break;
	case 135: size = 3; break;
	case 100: case 118: case 130: case 131: case 133: size = 4; break;
	case 113: case 134: size = 6; break;
	case 136: size = 9; break;
	default:
		o = Put(o, "Ch");
		o = PutU(o, ch);
		o = Put(o, " type ");
		o = PutU(o, type);
		Put(o, ": not understood");
		*pos = len;
		return 1;
	}
	if (i + 2 + size > len)
		return 0;
	v = d + i + 2;
	o = Put(o, "Ch");
	o = PutU(o, ch);
	o = Put(o, " ");
	switch (type) {
	case 0:   o = Put(o, "Input ");       PutU(o, v[0]); break;
	case 1:   o = Put(o, "Output ");      PutU(o, v[0]); break;
	case 2:   o = Put(o, "Analog in ");   PutFixed(o, BE(v, 2, 1), 2); break;
	case 3:   o = Put(o, "Analog out ");  PutFixed(o, BE(v, 2, 1), 2); break;
	case 100: o = Put(o, "Value ");       PutU(o, (UInt32)BE(v, 4, 0)); break;
	case 101: o = Put(o, "Light ");       o = PutU(o, (UInt32)BE(v, 2, 0)); Put(o, " lux"); break;
	case 102: o = Put(o, "Presence ");    Put(o, v[0] ? "yes" : "no"); break;
	case 103: o = Put(o, "Temperature "); o = PutFixed(o, BE(v, 2, 1), 1); Put(o, "\xb0" "C"); break;
	case 104: o = Put(o, "Humidity ");    o = PutFixed(o, v[0] * 5L, 1); Put(o, "%"); break;
	case 113: o = Put(o, "Accel ");       o = PutFixed(o, BE(v, 2, 1), 3); o = Put(o, " ");
	          o = PutFixed(o, BE(v + 2, 2, 1), 3); o = Put(o, " "); o = PutFixed(o, BE(v + 4, 2, 1), 3);
	          Put(o, "G"); break;
	case 115: o = Put(o, "Pressure ");    o = PutFixed(o, BE(v, 2, 0), 1); Put(o, "hPa"); break;
	case 116: o = Put(o, "Voltage ");     o = PutFixed(o, BE(v, 2, 0), 2); Put(o, "V"); break;
	case 117: o = Put(o, "Current ");     o = PutFixed(o, BE(v, 2, 0), 3); Put(o, "A"); break;
	case 118: o = Put(o, "Frequency ");   o = PutU(o, (UInt32)BE(v, 4, 0)); Put(o, "Hz"); break;
	case 120: o = Put(o, "Level ");       o = PutU(o, v[0]); Put(o, "%"); break;
	case 121: o = Put(o, "Altitude ");    o = PutI(o, BE(v, 2, 1)); Put(o, "m"); break;
	case 125: o = Put(o, "Concentration "); o = PutU(o, (UInt32)BE(v, 2, 0)); Put(o, "ppm"); break;
	case 128: o = Put(o, "Power ");       o = PutU(o, (UInt32)BE(v, 2, 0)); Put(o, "W"); break;
	case 130: o = Put(o, "Distance ");    o = PutFixed(o, BE(v, 4, 0), 3); Put(o, "m"); break;
	case 131: o = Put(o, "Energy ");      o = PutFixed(o, BE(v, 4, 0), 3); Put(o, "kWh"); break;
	case 132: o = Put(o, "Direction ");   o = PutU(o, (UInt32)BE(v, 2, 0)); Put(o, "\xb0"); break;
	case 133: o = Put(o, "Time ");        PutU(o, (UInt32)BE(v, 4, 0)); break;
	case 134: o = Put(o, "Gyro ");        o = PutFixed(o, BE(v, 2, 1), 2); o = Put(o, " ");
	          o = PutFixed(o, BE(v + 2, 2, 1), 2); o = Put(o, " "); PutFixed(o, BE(v + 4, 2, 1), 2); break;
	case 135: o = Put(o, "Colour ");      o = PutU(o, v[0]); o = Put(o, ","); o = PutU(o, v[1]);
	          o = Put(o, ","); PutU(o, v[2]); break;
	case 136: o = Put(o, "GPS ");         o = PutFixed(o, BE(v, 3, 1), 4); o = Put(o, ", ");
	          o = PutFixed(o, BE(v + 3, 3, 1), 4); o = Put(o, " ");
	          o = PutFixed(o, BE(v + 6, 3, 1), 2); Put(o, "m"); break;
	case 142: o = Put(o, "Switch ");      Put(o, v[0] ? "on" : "off"); break;
	}
	*pos = (UInt16)(i + 2 + size);
	return 1;
}
