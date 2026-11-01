/* Host-side tests for the small-font text layout. Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/ui/text.h"
#include "../src/ui/fontmetrics.h"

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
	CHECK(TxtUtf8ToPalm(dots, sizeof(dots), out, sizeof(out)) == 2 && (unsigned char)out[1] == 0x85);
	CHECK(TxtUtf8ToPalm(emoji, sizeof(emoji), out, sizeof(out)) == 4 && strcmp(out, "hi ?") == 0);
	CHECK(TxtUtf8ToPalm(broken, sizeof(broken), out, sizeof(out)) == 2 && strcmp(out, "x?") == 0);
	CHECK(TxtUtf8ToPalm((const UInt8 *)"abcdef", 6, out, 4) == 3 && strcmp(out, "abc") == 0);
}

int main(void)
{
	test_metrics();
	test_fit_and_wrap();
	test_age_and_snr();
	test_sender_split();
	test_utf8();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all text tests passed\n");
	return 0;
}
