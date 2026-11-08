/*
 * Host tests for the screens' run-time lines (src/screen_fmt.c): each fed its
 * longest realistic values must fit its screen's width in the large font,
 * which is what the user sees with Text size Large. Run with `make test`.
 */
#include <stdio.h>
#include <string.h>
#include "../src/screen_fmt.h"
#include "../src/ui/text.h"
#include "../src/ui/glyphs.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* Widths available: x to the right edge of each screen (see the Draw code). */
#define W_STATS    154   /* x 3, cut at 154 */
#define W_NODE     155   /* x 5 */
#define W_CONTACT  151   /* the 153-wide gadget, x 1, right x - 1 */
#define W_TRACE    151
#define STATS_ROWS ((147 - 18) / (VM_LARGE_FONT_HEIGHT + 1))   /* from y 18 to the Done button */

static void fits(const char *what, const Char *s, int maxW)
{
	int w = TxtWidth(s, (UInt16)strlen(s), TxtSmallWidth);

	if (w > maxW) {
		printf("FAIL %s: \"%s\" is %d px, room for %d\n", what, s, w, maxW);
		failures++;
	}
}

/* Rows s takes when word-wrapped to maxW. */
static int wrapped(const Char *s, int maxW)
{
	UInt16 len = (UInt16)strlen(s), used;
	int rows = 0;

	while (len) {
		TxtWrap(s, len, maxW, TxtSmallWidth, &used);
		if (used == 0)
			break;
		s += used;
		len -= used;
		rows++;
	}
	return rows;
}

static void test_secs(void)
{
	Char s[SCR_LINE];

	ScrSecs(s, 45);         CHECK(strcmp(s, "45s") == 0);
	ScrSecs(s, 720);        CHECK(strcmp(s, "12m") == 0);
	ScrSecs(s, 12000);      CHECK(strcmp(s, "3h 20m") == 0);
	ScrSecs(s, 187200UL);   CHECK(strcmp(s, "2d 4h") == 0);
}

static void test_stats(void)
{
	VmStats st;
	VmBattery b;
	Char lines[SCR_STATS_LINES][SCR_LINE];
	UInt16 n, i, rows;

	memset(&st, 0, sizeof(st));
	memset(&b, 0, sizeof(b));
	b.battMv = 4205;
	b.hasStorage = 1;
	b.usedKb = 312;
	b.totalKb = 4096;
	st.hasCore = st.hasRadio = st.hasPackets = 1;
	st.uptimeSecs = 9;
	st.queueLen = 2;
	st.noiseFloor = -112;
	st.lastRssi = -87;
	st.lastSnrQ = 25;
	st.recv = 5;
	st.recvFlood = 3;
	st.recvDirect = 2;
	n = ScrStatsLines(&st, &b, 1, lines);
	CHECK(n == 8);
	for (i = 0; i < n; i++)
		fits("Radio Stats (typical)", lines[i], W_STATS);
	CHECK(strcmp(lines[0], "Battery 4.20 V") == 0);
	CHECK(strcmp(lines[1], "Storage 312 of 4096 KB used") == 0);
	CHECK(strcmp(lines[2], "Up 9s, 2 queued") == 0);
	CHECK(strcmp(lines[3], "Noise floor -112 dBm") == 0);
	CHECK(strcmp(lines[4], "Last heard -87 dBm, +6dB") == 0);
	CHECK(strcmp(lines[6], "Heard 5: 3 flood, 2 direct") == 0);
	st.errFlags = 0x0104;
	ScrStatsLines(&st, &b, 1, lines);
	CHECK(strcmp(lines[3], "Errors 0104") == 0);

	/* a radio up for a year that hears a busy mesh */
	b.battMv = 65535;
	b.usedKb = 99999;
	b.totalKb = 99999;
	st.uptimeSecs = 365UL * 86400 + 23 * 3600;
	st.queueLen = 255;
	st.errFlags = 0xFFFF;
	st.noiseFloor = -128;
	st.lastRssi = -128;
	st.lastSnrQ = -128;
	st.txAirSecs = st.rxAirSecs = 365UL * 86400 + 23 * 3600;
	st.recv = st.recvFlood = st.recvDirect = 9999999UL;
	st.sent = st.sentFlood = st.sentDirect = 9999999UL;
	n = ScrStatsLines(&st, &b, 1, lines);
	for (i = rows = 0; i < n; i++)
		rows += wrapped(lines[i], W_STATS);
	/* the lines wrap (DrawWrap) and the graph gives way; they must still fit above Done */
	if (rows > STATS_ROWS) {
		printf("FAIL Radio Stats: %d rows wrapped, room for %d\n", rows, STATS_ROWS);
		failures++;
	}
}

static void test_node(void)
{
	ScrTime t = { 12, 31, 23, 59 };
	Char s[SCR_LINE];

	ScrVisorClock(s, &t, -300);
	CHECK(strcmp(s, "Visor 12/31 23:59 (UTC-5:00)") == 0);
	ScrVisorClock(s, &t, 765);   /* +12:45, the widest zone */
	fits("Node clock", s, W_NODE);
	ScrRadioClock(s, &t, 0, -765);
	CHECK(strcmp(s, "Radio 12/31 23:59 UTC, sys -765") == 0);
	fits("Node radio clock", s, W_NODE);
	ScrRadioClock(s, &t, 1, 0);
	fits("Node radio clock", s, W_NODE);
	ScrRadioClock(s, NULL, 0, 300);
	CHECK(strcmp(s, "Radio -- UTC, sys 300") == 0);
}

static void test_contact(void)
{
	ScrTime t = { 12, 31, 23, 59 };
	Char s[SCR_LINE];

	/* only repeaters and rooms are logged into; 32 hops is the most a path holds */
	ScrContactSummary(s, GLYPH_REPEATER, "Repeater", "32 hops", 1);
	fits("Contact summary", s, W_CONTACT);
	ScrContactSummary(s, GLYPH_ROOM, "Room", "32 hops", 1);
	fits("Contact summary", s, W_CONTACT);
	ScrContactSummary(s, GLYPH_COMPANION, "Companion", "32 hops", 0);
	fits("Contact summary", s, W_CONTACT);
	ScrContactAdvert(s, &t, "364d");
	CHECK(strcmp(s, "Advert 12-31 23:59 (364d ago)") == 0);
	fits("Contact advert", s, W_CONTACT);
}

static void test_trace(void)
{
	Char s[SCR_LINE];

	ScrTraceBack(s, 1250, 1);
	CHECK(strcmp(s, "Back in 1.2 s, 1 hop") == 0);
	ScrTraceBack(s, 99999UL, 64);
	fits("Trace", s, W_TRACE);
}

int main(void)
{
	gTxtLarge = 1;
	test_secs();
	test_stats();
	test_node();
	test_contact();
	test_trace();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("screens: all tests passed\n");
	return 0;
}
