#include <PalmOS.h>
#include "session.h"
#include "contacts.h"
#include "ui/text.h"
#include "info_fmt.h"

void InfoClock(Char *out, UInt16 size, const Char *label, UInt32 utc)
{
	DateTimeType dt, now;
	Char s[40];

	TimSecondsToDateTime(SessionLocalPalm(utc), &dt);
	TimSecondsToDateTime(SessionLocalPalm(SessionNow()), &now);
	if (dt.year != now.year || dt.month != now.month || dt.day != now.day)
		StrPrintF(s, "%s %u/%u %02u:%02u:%02u", label, dt.month, dt.day, dt.hour, dt.minute, dt.second);
	else
		StrPrintF(s, "%s %02u:%02u:%02u", label, dt.hour, dt.minute, dt.second);
	StrNCat(out, s, size);
}

void InfoHop(Char *out, UInt16 size, const UInt8 *hash, UInt8 hashSize)
{
	VmContact c;
	Char hex[12];
	UInt16 len;

	TxtHex(hash, hashSize, hex);
	StrNCat(out, hex, size);
	if (ContactsFindByPrefixN(hash, hashSize, &c)) {
		StrNCat(out, " ", size);
		len = StrLen(out);
		StrNCat(out, c.name, len + 12 + 1 < size ? len + 12 + 1 : size);
	}
}

void InfoVia(Char *out, UInt16 size, const UInt8 *path, UInt16 pathBytes, UInt8 hashSize, UInt16 hops)
{
	Char more[16];
	UInt16 i;

	if (hashSize == 0)
		return;
	for (i = 0; i + hashSize <= pathBytes; i += hashSize) {
		if (StrLen(out) + 40 > size) {      /* room for one more entry and the end */
			StrNCat(out, ", ...", size);
			return;
		}
		StrNCat(out, i ? ", " : " ", size);
		InfoHop(out, size, path + i, hashSize);
	}
	if (hops > pathBytes / hashSize) {
		StrPrintF(more, " +%u more", hops - pathBytes / hashSize);
		StrNCat(out, more, size);
	}
}
