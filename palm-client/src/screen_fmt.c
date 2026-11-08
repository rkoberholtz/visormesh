#include "screen_fmt.h"
#include "ui/text.h"

/* In the fifth code segment: see sections.h for the rules. */
static void Put(Char *out, UInt16 *i, const Char *s) VM_SECT5;
static void PutNum(Char *out, UInt16 *i, UInt32 v, UInt16 minDigits) VM_SECT5;
static void PutInt(Char *out, UInt16 *i, Int32 v) VM_SECT5;
static void PutDate(Char *out, UInt16 *i, const ScrTime *t, Char sep, Boolean pad) VM_SECT5;

/* Every writer stops at SCR_LINE - 1 and keeps out[] terminated. */
static void Put(Char *out, UInt16 *i, const Char *s)
{
	while (*s && *i < SCR_LINE - 1)
		out[(*i)++] = *s++;
	out[*i] = 0;
}

static void PutNum(Char *out, UInt16 *i, UInt32 v, UInt16 minDigits)
{
	Char d[11];
	UInt16 n = 0;

	do {
		d[n++] = (Char)('0' + v % 10);
		v /= 10;
	} while (v || n < minDigits);
	while (n && *i < SCR_LINE - 1)
		out[(*i)++] = d[--n];
	out[*i] = 0;
}

static void PutInt(Char *out, UInt16 *i, Int32 v)
{
	if (v < 0) {
		Put(out, i, "-");
		v = -v;
	}
	PutNum(out, i, (UInt32)v, 1);
}

/* "11/8 14:02", or with pad "11-08 14:02" */
static void PutDate(Char *out, UInt16 *i, const ScrTime *t, Char sep, Boolean pad)
{
	Char s[2];

	s[0] = sep;
	s[1] = 0;
	PutNum(out, i, t->month, pad ? 2 : 1);
	Put(out, i, s);
	PutNum(out, i, t->day, pad ? 2 : 1);
	Put(out, i, " ");
	PutNum(out, i, t->hour, 2);
	Put(out, i, ":");
	PutNum(out, i, t->minute, 2);
}

void ScrSecs(Char *out, UInt32 s)
{
	UInt16 i = 0;

	out[0] = 0;
	if (s < 60) {
		PutNum(out, &i, s, 1);
		Put(out, &i, "s");
	} else if (s < 3600) {
		PutNum(out, &i, s / 60, 1);
		Put(out, &i, "m");
	} else if (s < 86400UL) {
		PutNum(out, &i, s / 3600, 1);
		Put(out, &i, "h ");
		PutNum(out, &i, s / 60 % 60, 1);
		Put(out, &i, "m");
	} else {
		PutNum(out, &i, s / 86400UL, 1);
		Put(out, &i, "d ");
		PutNum(out, &i, s / 3600 % 24, 1);
		Put(out, &i, "h");
	}
}

UInt16 ScrStatsLines(const VmStats *st, const VmBattery *b, Boolean hasBatt, Char lines[][SCR_LINE])
{
	UInt16 n = 0, i;
	Char a[16], c[16];
	UInt8 hex[2];

	if (hasBatt) {
		i = 0;
		Put(lines[n], &i, "Battery ");
		PutNum(lines[n], &i, b->battMv / 1000, 1);
		Put(lines[n], &i, ".");
		PutNum(lines[n], &i, b->battMv % 1000 / 10, 2);
		Put(lines[n++], &i, " V");
		if (b->hasStorage) {
			i = 0;
			Put(lines[n], &i, "Storage ");
			PutNum(lines[n], &i, b->usedKb, 1);
			Put(lines[n], &i, " of ");
			PutNum(lines[n], &i, b->totalKb, 1);
			Put(lines[n++], &i, " KB used");
		}
	}
	if (st->hasCore) {
		i = 0;
		ScrSecs(a, st->uptimeSecs);
		Put(lines[n], &i, "Up ");
		Put(lines[n], &i, a);
		Put(lines[n], &i, ", ");
		PutNum(lines[n], &i, st->queueLen, 1);
		Put(lines[n++], &i, " queued");
		if (st->errFlags) {
			i = 0;
			hex[0] = (UInt8)(st->errFlags >> 8);
			hex[1] = (UInt8)st->errFlags;
			TxtHex(hex, 2, c);
			Put(lines[n], &i, "Errors ");
			Put(lines[n++], &i, c);
		}
	}
	if (st->hasRadio) {
		i = 0;
		Put(lines[n], &i, "Noise floor ");
		PutInt(lines[n], &i, st->noiseFloor);
		Put(lines[n++], &i, " dBm");
		i = 0;
		TxtFormatSnr(c, st->lastSnrQ);
		Put(lines[n], &i, "Last heard ");
		PutInt(lines[n], &i, st->lastRssi);
		Put(lines[n], &i, " dBm, ");
		Put(lines[n++], &i, c);
		i = 0;
		ScrSecs(a, st->txAirSecs);
		ScrSecs(c, st->rxAirSecs);
		Put(lines[n], &i, "Airtime: sent ");
		Put(lines[n], &i, a);
		Put(lines[n], &i, ", heard ");
		Put(lines[n++], &i, c);
	}
	if (st->hasPackets) {
		i = 0;
		Put(lines[n], &i, "Heard ");
		PutNum(lines[n], &i, st->recv, 1);
		Put(lines[n], &i, ": ");
		PutNum(lines[n], &i, st->recvFlood, 1);
		Put(lines[n], &i, " flood, ");
		PutNum(lines[n], &i, st->recvDirect, 1);
		Put(lines[n++], &i, " direct");
		i = 0;
		Put(lines[n], &i, "Sent ");
		PutNum(lines[n], &i, st->sent, 1);
		Put(lines[n], &i, ": ");
		PutNum(lines[n], &i, st->sentFlood, 1);
		Put(lines[n], &i, " flood, ");
		PutNum(lines[n], &i, st->sentDirect, 1);
		Put(lines[n++], &i, " direct");
	}
	return n;
}

void ScrVisorClock(Char *out, const ScrTime *local, Int16 off)
{
	UInt16 i = 0;
	Int16 a = off < 0 ? -off : off;

	out[0] = 0;
	Put(out, &i, "Visor ");
	PutDate(out, &i, local, '/', 0);
	Put(out, &i, off < 0 ? " (UTC-" : " (UTC+");
	PutNum(out, &i, a / 60, 1);
	Put(out, &i, ":");
	PutNum(out, &i, a % 60, 2);
	Put(out, &i, ")");
}

void ScrRadioClock(Char *out, const ScrTime *radio, Boolean mismatch, Int32 sys)
{
	UInt16 i = 0;

	out[0] = 0;
	Put(out, &i, "Radio ");
	if (radio)
		PutDate(out, &i, radio, '/', 0);
	else
		Put(out, &i, "--");
	if (mismatch) {
		Put(out, &i, " UTC: MISMATCH");
		return;
	}
	Put(out, &i, " UTC, sys ");
	PutInt(out, &i, sys);
}

void ScrContactSummary(Char *out, Char glyph, const Char *type, const Char *route, Boolean loggedIn)
{
	UInt16 i = 1;

	out[0] = glyph;
	out[1] = 0;
	Put(out, &i, " ");
	Put(out, &i, type);
	Put(out, &i, ", ");
	Put(out, &i, route);
	if (loggedIn)
		Put(out, &i, ", logged in");
}

void ScrContactAdvert(Char *out, const ScrTime *when, const Char *ago)
{
	UInt16 i = 0;

	out[0] = 0;
	Put(out, &i, "Advert ");
	PutDate(out, &i, when, '-', 1);
	Put(out, &i, " (");
	Put(out, &i, ago);
	Put(out, &i, " ago)");
}

void ScrTraceBack(Char *out, UInt32 ms, UInt16 hops)
{
	UInt16 i = 0;

	out[0] = 0;
	Put(out, &i, "Back in ");
	PutNum(out, &i, ms / 1000, 1);
	Put(out, &i, ".");
	PutNum(out, &i, ms % 1000 / 100, 1);
	Put(out, &i, " s, ");
	PutNum(out, &i, hops, 1);
	Put(out, &i, hops == 1 ? " hop" : " hops");
}
