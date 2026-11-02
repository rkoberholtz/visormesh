#include "text.h"
#include "fontmetrics.h"
#include "glyphs.h"

/* In the second code segment: see sections.h for the rules. */
static Char MapUnicode(UInt32 cp) VM_SECT2;

Boolean gTxtLarge = 0;

Int16 TxtSmallWidth(Char c)
{
	return gTxtLarge ? gLargeFontWidths[(UInt8)c] : gSmallFontWidths[(UInt8)c];
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
	UInt16 fit, brk, end, nl;

	/* a line break in the text ends the line there, if it comes before the wrap point */
	for (nl = 0; nl < len && s[nl] != '\n' && s[nl] != '\r'; nl++)
		;
	fit = TxtFit(s, nl, maxW, w);
	if (fit >= nl && nl < len) {
		*consumed = nl + 1;
		if (s[nl] == '\r' && nl + 1 < len && s[nl + 1] == '\n')
			(*consumed)++;              /* \r\n counts as one break */
		return nl;
	}

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

/*
 * Unicode beyond Latin-1 to what the small font can show: the UI symbols it
 * has (arrows, check marks, node-type marks), ASCII for typographic quotes and
 * dashes, a smiley for any emoji. Returns 0 for characters to drop (variation
 * selectors, zero-width joiners) and '?' for anything else.
 */
static Char MapUnicode(UInt32 cp)
{
	if (cp == 0xFE0F || cp == 0xFE0E || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060)
		return 0;                                       /* invisible */
	if ((cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF && cp != 0x2605 &&
	     cp != 0x2713 && cp != 0x2714 && cp != 0x2717 && cp != 0x2718 && cp != 0x2705 && cp != 0x274C))
		return GLYPH_EMOJI;
	switch (cp) {
	case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
	case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
	case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return '-';
	case 0x2022: case 0x25CF: case 0x26AB: return GLYPH_COMPANION;
	case 0x2026: return GLYPH_ELLIPSIS;
	case 0x2190: return GLYPH_LEFT;
	case 0x2192: case 0x27A1: return GLYPH_RIGHT;
	case 0x2191: return GLYPH_UP;
	case 0x2193: return GLYPH_DOWN;
	case 0x2039: return GLYPH_BACK;
	case 0x203A: return GLYPH_FORWARD;
	case 0x2605: case 0x2606: case 0x2B50: return GLYPH_SELF;
	case 0x2713: case 0x2714: case 0x2705: return GLYPH_ACKED;
	case 0x2717: case 0x2718: case 0x274C: return GLYPH_FAILED;
	case 0x25B2: case 0x25B3: return GLYPH_REPEATER;
	case 0x25A0: case 0x25A1: return GLYPH_ROOM;
	case 0x25C9: return GLYPH_SENSOR;
	case 0x25CB: return GLYPH_UNKNOWN;
	case 0x2500: case 0x2501: return GLYPH_RULE;
	case 0x2502: case 0x2503: return '|';
	default: return '?';
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
		if (cp >= 0xA0 && cp <= 0xFF) {
			out[o++] = (Char)cp;
		} else {
			Char c = MapUnicode(cp);

			if (c)
				out[o++] = c;
		}
	}
	out[o] = 0;
	return o;
}

void TxtUtf8NameToPalm(Char *name, UInt16 size)
{
	Char tmp[48];
	UInt16 len = 0;

	if (size > sizeof(tmp))
		size = sizeof(tmp);
	while (len < size - 1 && name[len])
		len++;
	TxtUtf8ToPalm((const UInt8 *)name, len, tmp, size);
	for (len = 0; len < size && (name[len] = tmp[len]) != 0; len++)
		;
}

/* Windows-1252 0x80-0x9F to Unicode (0 = unused slot). */
static UInt16 kCp1252[32] = {   /* not const: sections.h */
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

/* Arithmetic, not a digit table: a static table here sits outside vmcode2
 * and read as garbage on the Visor (sections.h). */
static Char HexDigit(UInt8 d) VM_SECT2;
static Char HexDigit(UInt8 d)
{
	return (Char)(d < 10 ? '0' + d : 'a' + d - 10);
}

void TxtHex(const UInt8 *bytes, UInt16 n, Char *out)
{
	UInt16 i;

	for (i = 0; i < n; i++) {
		*out++ = HexDigit(bytes[i] >> 4);
		*out++ = HexDigit(bytes[i] & 15);
	}
	*out = 0;
}
