/* Host-side tests for the small-font text layout. Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/ui/text.h"
#include "../src/ui/fontmetrics.h"
#include "../src/ui/glyphs.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* Fixed-width stand-in: every character 4 px, so expectations are easy to read. */
static Int16 Four(Char c)
{
	(void)c;
	return 4;
}

static void test_metrics(void)
{
	CHECK(TxtSmallWidth('A') == 4);
	CHECK(TxtSmallWidth((Char)0x80) == 5);       /* the star glyph is drawn 5 px wide */
	CHECK(TxtWidth("abc", 3, TxtSmallWidth) == 12);
}

static void test_fit_and_wrap(void)
{
	const char *t = "I'll bring a spare antenna for anyone";
	UInt16 n, used;

	CHECK(TxtFit("abcdef", 6, 12, Four) == 3);
	CHECK(TxtFit("abcdef", 6, 100, Four) == 6);

	/* 20 px = 5 characters: "I'll " fits, so the break is after "I'll" */
	n = TxtWrap(t, strlen(t), 20, Four, &used);
	CHECK(n == 4 && used == 5);

	/* 13 chars wide: "I'll bring a " -> "I'll bring a" */
	n = TxtWrap(t, strlen(t), 13 * 4, Four, &used);
	CHECK(n == 12 && memcmp(t, "I'll bring a", 12) == 0 && used == 13);

	/* a word longer than the line is broken */
	n = TxtWrap("https://example.com/long", 24, 10 * 4, Four, &used);
	CHECK(n == 10 && used == 10);

	/* line breaks end the line and are consumed */
	n = TxtWrap("ab\ncd", 5, 100, Four, &used);
	CHECK(n == 2 && used == 3);
	n = TxtWrap("ab\r\ncd", 6, 100, Four, &used);
	CHECK(n == 2 && used == 4);
	n = TxtWrap("\n\nx", 3, 100, Four, &used);            /* an empty line */
	CHECK(n == 0 && used == 1);
	n = TxtWrap("abcdef ghij\nk", 13, 5 * 4, Four, &used);  /* the width wraps before the break */
	CHECK(n == 5 && used == 5);

	/* the rest fits */
	n = TxtWrap("short", 5, 100, Four, &used);
	CHECK(n == 5 && used == 5);

	/* a whole transcript paragraph wraps into lines that each fit */
	{
		const char *p = t;
		UInt16 left = strlen(t), lines = 0;

		while (left) {
			n = TxtWrap(p, left, 26 * 4, TxtSmallWidth, &used);
			CHECK(TxtWidth(p, n, TxtSmallWidth) <= 26 * 4);
			CHECK(used > 0);
			p += used;
			left -= used;
			lines++;
		}
		CHECK(lines == 2);
	}
}

static void test_age_and_snr(void)
{
	char buf[16];

	{
		static const UInt8 key[] = { 0x00, 0x9c, 0xd8, 0xff, 0x0a };
		Char hex[11];

		TxtHex(key, 5, hex);    CHECK(strcmp(hex, "009cd8ff0a") == 0);
		TxtHex(key, 0, hex);    CHECK(hex[0] == 0);
	}
	TxtFormatAge(buf, 0);       CHECK(strcmp(buf, "now") == 0);
	TxtFormatAge(buf, 59);      CHECK(strcmp(buf, "now") == 0);
	TxtFormatAge(buf, 300);     CHECK(strcmp(buf, "5m") == 0);
	TxtFormatAge(buf, 82800);   CHECK(strcmp(buf, "23h") == 0);
	TxtFormatAge(buf, 259200);  CHECK(strcmp(buf, "3d") == 0);
	TxtFormatAge(buf, 63072000UL); CHECK(strcmp(buf, "2y") == 0);

	TxtFormatSnr(buf, 24);      CHECK(strcmp(buf, "+6dB") == 0);
	TxtFormatSnr(buf, 26);      CHECK(strcmp(buf, "+7dB") == 0);   /* 6.5 rounds away from zero */
	TxtFormatSnr(buf, -10);     CHECK(strcmp(buf, "-3dB") == 0);   /* -2.5 */
	TxtFormatSnr(buf, 1);       CHECK(strcmp(buf, "0dB") == 0);
	TxtFormatSnr(buf, 127);     CHECK(strcmp(buf, "+32dB") == 0);
}

static void test_sender_split(void)
{
	CHECK(TxtSplitSender("Alice: hi all", 13) == 5);
	CHECK(TxtSplitSender("no sender here", 14) == 0);
	CHECK(TxtSplitSender("time 12:30 ok", 13) == 0);       /* colon not followed by a space */
	CHECK(TxtSplitSender(": empty name", 12) == 0);
	CHECK(TxtSplitSender("Alice:", 6) == 0);
}

static void test_utf8(void)
{
	char out[32];
	static const UInt8 cafe[] = { 'c', 'a', 'f', 0xC3, 0xA9 };
	static const UInt8 dots[] = { 'a', 0xE2, 0x80, 0xA6 };
	static const UInt8 emoji[] = { 'h', 'i', ' ', 0xF0, 0x9F, 0x91, 0x8B };
	static const UInt8 broken[] = { 'x', 0xC3 };

	CHECK(TxtUtf8ToPalm(cafe, sizeof(cafe), out, sizeof(out)) == 4 && (unsigned char)out[3] == 0xE9);
	CHECK(TxtUtf8ToPalm(dots, sizeof(dots), out, sizeof(out)) == 2 && out[1] == GLYPH_ELLIPSIS);
	CHECK(TxtUtf8ToPalm(emoji, sizeof(emoji), out, sizeof(out)) == 4 && out[3] == GLYPH_EMOJI);
	{
		/* "a→b" (arrow), "“hi”" (curly quotes), "x—y" (em dash), "❤️" (heart + VS16) */
		static const UInt8 arrow[] = { 'a', 0xE2, 0x86, 0x92, 'b' };
		static const UInt8 quotes[] = { 0xE2, 0x80, 0x9C, 'h', 'i', 0xE2, 0x80, 0x9D };
		static const UInt8 dash[] = { 'x', 0xE2, 0x80, 0x94, 'y' };
		static const UInt8 heart[] = { 0xE2, 0x9D, 0xA4, 0xEF, 0xB8, 0x8F };
		static const UInt8 check[] = { 0xE2, 0x9C, 0x93 };

		CHECK(TxtUtf8ToPalm(arrow, sizeof(arrow), out, sizeof(out)) == 3 && out[1] == GLYPH_RIGHT);
		CHECK(TxtUtf8ToPalm(quotes, sizeof(quotes), out, sizeof(out)) == 4 && strcmp(out, "\"hi\"") == 0);
		CHECK(TxtUtf8ToPalm(dash, sizeof(dash), out, sizeof(out)) == 3 && strcmp(out, "x-y") == 0);
		CHECK(TxtUtf8ToPalm(heart, sizeof(heart), out, sizeof(out)) == 1 && out[0] == GLYPH_EMOJI);
		CHECK(TxtUtf8ToPalm(check, sizeof(check), out, sizeof(out)) == 1 && out[0] == GLYPH_ACKED);
	}
	CHECK(TxtUtf8ToPalm(broken, sizeof(broken), out, sizeof(out)) == 2 && strcmp(out, "x?") == 0);
	CHECK(TxtUtf8ToPalm((const UInt8 *)"abcdef", 6, out, 4) == 3 && strcmp(out, "abc") == 0);
}

static void test_to_utf8(void)
{
	UInt8 out[16];
	char back[16];
	static const char cafe[] = { 'c', 'a', 'f', (char)0xE9 };
	static const char quotes[] = { (char)0x93, 'h', 'i', (char)0x94, (char)0x85 };

	CHECK(TxtPalmToUtf8(cafe, 4, out, sizeof(out)) == 5 && out[3] == 0xC3 && out[4] == 0xA9);
	CHECK(TxtPalmToUtf8(quotes, 5, out, sizeof(out)) == 11);
	CHECK(out[0] == 0xE2 && out[1] == 0x80 && out[2] == 0x9C);          /* U+201C */
	/* back to the Visor's charset: the quotes as ASCII (the font has no curly ones), the ellipsis kept */
	CHECK(TxtUtf8ToPalm(out, 11, back, sizeof(back)) == 5 && memcmp(back, "\"hi\"", 4) == 0 && back[4] == GLYPH_ELLIPSIS);
	/* doesn't fit: nothing rather than a cut character */
	CHECK(TxtPalmToUtf8(cafe, 4, out, 4) == 0);
}

int main(void)
{
	test_metrics();
	test_fit_and_wrap();
	test_age_and_snr();
	test_sender_split();
	test_utf8();
	test_to_utf8();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all text tests passed\n");
	return 0;
}
