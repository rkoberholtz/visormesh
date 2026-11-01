#include "text.h"
#include "fontmetrics.h"

Int16 TxtSmallWidth(Char c)
{
	return gSmallFontWidths[(UInt8)c];
}

Int16 TxtWidth(const Char *s, UInt16 len, TxtWidthFn w)
{
	Int16 total = 0;
	UInt16 i;

	for (i = 0; i < len; i++)
		total += w(s[i]);
	return total;
}

UInt16 TxtFit(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w)
{
	Int16 used = 0;
	UInt16 i;

	for (i = 0; i < len; i++) {
		used += w(s[i]);
		if (used > maxW)
			break;
	}
	return i;
}

UInt16 TxtWrap(const Char *s, UInt16 len, Int16 maxW, TxtWidthFn w, UInt16 *consumed)
{
	UInt16 fit = TxtFit(s, len, maxW, w);
	UInt16 brk, end;

	if (fit >= len) {
		*consumed = len;
		return len;
	}
	if (fit == 0)
		fit = 1;    /* always make progress, even if one character is too wide */
	/* break after the last space in the part that fits */
	for (brk = fit; brk > 0 && s[brk] != ' '; brk--)
		;
	if (brk == 0)
		brk = fit;  /* one word longer than the line: break it */
	end = brk;
	while (end > 0 && s[end - 1] == ' ')
		end--;
	while (brk < len && s[brk] == ' ')
		brk++;
	*consumed = brk;
	return end;
}

void TxtFormatAge(Char *out, UInt32 seconds)
{
	UInt32 v;
	Char unit;
	Char digits[12];
	UInt16 n = 0, i = 0;

	if (seconds < 60) {
		out[0] = 'n'; out[1] = 'o'; out[2] = 'w'; out[3] = 0;
		return;
	}
	if (seconds < 3600UL)            { v = seconds / 60;        unit = 'm'; }
	else if (seconds < 86400UL)      { v = seconds / 3600;      unit = 'h'; }
	else if (seconds < 31536000UL)   { v = seconds / 86400;     unit = 'd'; }
	else                             { v = seconds / 31536000UL; unit = 'y'; }
	do {
		digits[n++] = (Char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		out[i++] = digits[--n];
	out[i++] = unit;
	out[i] = 0;
}

void TxtFormatSnr(Char *out, Int8 quarterDb)
{
	Int16 q = quarterDb;
	Int16 db = (q >= 0 ? q + 2 : q - 2) / 4;     /* round half away from zero */
	UInt16 i = 0, n = 0;
	Char digits[4];
	Int16 a = db < 0 ? -db : db;

	if (db > 0)
		out[i++] = '+';
	else if (db < 0)
		out[i++] = '-';
	do {
		digits[n++] = (Char)('0' + a % 10);
		a /= 10;
	} while (a);
	while (n)
		out[i++] = digits[--n];
	out[i++] = 'd';
	out[i++] = 'B';
	out[i] = 0;
}

UInt16 TxtSplitSender(const Char *text, UInt16 len)
{
	UInt16 i;

	for (i = 0; i < len && i <= VM_NAME_MAX; i++) {
		if (text[i] == ':')
			return (i > 0 && i + 1 < len && text[i + 1] == ' ') ? i : 0;
	}
	return 0;
}

/* Windows-1252's 0x80-0x9F punctuation, for the Unicode characters people type. */
static Char Cp1252(UInt32 cp)
{
	switch (cp) {
	case 0x2026: return (Char)0x85;   /* ellipsis */
	case 0x2018: return (Char)0x91;
	case 0x2019: return (Char)0x92;
	case 0x201C: return (Char)0x93;
	case 0x201D: return (Char)0x94;
	case 0x2022: return (Char)0x95;   /* bullet */
	case 0x2013: return (Char)0x96;   /* en dash */
	case 0x2014: return (Char)0x97;   /* em dash */
	case 0x20AC: return (Char)0x80;   /* euro */
	default:     return '?';
	}
}

UInt16 TxtUtf8ToPalm(const UInt8 *in, UInt16 inLen, Char *out, UInt16 outSize)
{
	UInt16 i = 0, o = 0;

	if (outSize == 0)
		return 0;
	while (i < inLen && o + 1 < outSize) {
		UInt8 b = in[i];
		UInt32 cp;
		UInt16 extra, k;

		if (b < 0x80) {
			out[o++] = (Char)b;
			i++;
			continue;
		}
		if ((b & 0xE0) == 0xC0)      { cp = b & 0x1F; extra = 1; }
		else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
		else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
		else {                          /* stray continuation or invalid byte */
			out[o++] = '?';
			i++;
			continue;
		}
		for (k = 1; k <= extra; k++) {
			if (i + k >= inLen || (in[i + k] & 0xC0) != 0x80)
				break;
			cp = (cp << 6) | (in[i + k] & 0x3F);
		}
		if (k <= extra) {               /* truncated sequence */
			out[o++] = '?';
			i += k;
			continue;
		}
		i += 1 + extra;
		if (cp >= 0xA0 && cp <= 0xFF)
			out[o++] = (Char)cp;
		else
			out[o++] = Cp1252(cp);
	}
	out[o] = 0;
	return o;
}

/* Windows-1252 0x80-0x9F to Unicode (0 = unused slot). */
static const UInt16 kCp1252[32] = {
	0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

UInt16 TxtPalmToUtf8(const Char *in, UInt16 inLen, UInt8 *out, UInt16 outSize)
{
	UInt16 i, o = 0;

	for (i = 0; i < inLen; i++) {
		UInt8 c = (UInt8)in[i];
		UInt16 cp = c;

		if (c >= 0x80 && c < 0xA0)
			cp = kCp1252[c - 0x80] ? kCp1252[c - 0x80] : '?';
		if (cp < 0x80) {
			if (o + 1 > outSize)
				return 0;
			out[o++] = (UInt8)cp;
		} else if (cp < 0x800) {
			if (o + 2 > outSize)
				return 0;
			out[o++] = (UInt8)(0xC0 | (cp >> 6));
			out[o++] = (UInt8)(0x80 | (cp & 0x3F));
		} else {
			if (o + 3 > outSize)
				return 0;
			out[o++] = (UInt8)(0xE0 | (cp >> 12));
			out[o++] = (UInt8)(0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (UInt8)(0x80 | (cp & 0x3F));
		}
	}
	return o;
}
