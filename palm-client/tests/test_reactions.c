/* Host-side tests for MeshCore One reactions (src/reaction_fmt.c) and message paths (src/rxpath.c). Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/reaction_fmt.h"
#include "../src/rxpath.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static const char *HashOf(const char *text, UInt32 ts)
{
	static Char out[VM_REACT_HASH_LEN + 1];
	UInt8 h[VM_REACT_HASH_BYTES];

	VmReactHash((const UInt8 *)text, (UInt16)strlen(text), ts, h);
	VmReactHashText(h, out);
	return out;
}

static Boolean Parse(Boolean ch, const char *text, VmReaction *r)
{
	return VmReactParse(ch, (const UInt8 *)text, (UInt16)strlen(text), r);
}

/* The vectors in MC1's spec. */
static void test_hash(void)
{
	CHECK(strcmp(HashOf("Hello", 1704067200UL), "b45pc4ek") == 0);
	CHECK(strcmp(HashOf("Hello world", 1704067200UL), "7a6r3xy1") == 0);
	CHECK(strcmp(HashOf("Hello", 1704067201UL), "3wh8x8z5") == 0);
	CHECK(strcmp(HashOf("World", 1704067200UL), "vp1vxnf4") == 0);
	CHECK(strcmp(HashOf("Message 0", 0), "7mt5pe1m") == 0);
	CHECK(strcmp(HashOf("caf\xC3\xA9 \xF0\x9F\x91\x8D ok", 1760000000UL), "8b5qzwf6") == 0);
	CHECK(strcmp(HashOf("", 4294967295UL), "npah66y0") == 0);
}

static void test_body(void)
{
	const char *t = "Alice:  Hello \t";
	UInt16 n;
	const UInt8 *b = VmReactChannelBody((const UInt8 *)t, (UInt16)strlen(t), &n);

	CHECK(n == 5 && memcmp(b, "Hello", 5) == 0);
	t = "Bob: time: 12:00";                       /* only the first ':' */
	b = VmReactChannelBody((const UInt8 *)t, (UInt16)strlen(t), &n);
	CHECK(n == 11 && memcmp(b, "time: 12:00", 11) == 0);
}

static void test_build(void)
{
	UInt8 h[VM_REACT_HASH_BYTES], out[64];
	UInt16 n;

	VmReactHash((const UInt8 *)"Hello", 5, 1704067200UL, h);
	n = VmReactBuild(1, (const UInt8 *)"AlphaNode", 9, (const UInt8 *)"\xF0\x9F\x91\x8D", 4, h, out, sizeof(out));
	CHECK(n == strlen("@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4ek") &&
	      memcmp(out, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4ek", n) == 0);
	n = VmReactBuild(0, 0, 0, (const UInt8 *)"\xF0\x9F\x91\x8D", 4, h, out, sizeof(out));
	CHECK(n == 13 && memcmp(out, "\xF0\x9F\x91\x8D\nb45pc4ek", n) == 0);
	CHECK(VmReactBuild(1, (const UInt8 *)"AlphaNode", 9, (const UInt8 *)"\xF0\x9F\x91\x8D", 4, h, out, 20) == 0);
}

static void test_parse(void)
{
	VmReaction r;
	UInt8 h[VM_REACT_HASH_BYTES];

	VmReactHash((const UInt8 *)"Hello", 5, 1704067200UL, h);
	CHECK(Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4ek", &r));
	CHECK(r.targetLen == 9 && memcmp(r.target, "AlphaNode", 9) == 0);
	CHECK(r.emojiLen == 4 && memcmp(r.hash, h, 5) == 0);
	CHECK(Parse(1, "\xF0\x9F\x91\x8D@[AlphaNode]\nb45pc4ek", &r));     /* before 1.4.1 */
	CHECK(r.targetLen == 9 && r.emojiLen == 4 && memcmp(r.hash, h, 5) == 0);
	CHECK(Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D\nB45PC4EK", &r));      /* either case */
	CHECK(Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4ek", &r) && memcmp(r.hash, h, 5) == 0);
	CHECK(Parse(0, "\xF0\x9F\x91\x8D\nb45pc4ek", &r) && r.target == 0 && memcmp(r.hash, h, 5) == 0);
	CHECK(Parse(0, "\xE2\x9D\xA4\xEF\xB8\x8F\nb45pc4ek", &r));          /* heart + VS16 */
	CHECK(Parse(0, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD\nb45pc4ek", &r));  /* skin tone */
	CHECK(Parse(0, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\nb45pc4ek", &r));  /* flag */
	CHECK(Parse(0, "1\xEF\xB8\x8F\xE2\x83\xA3\nb45pc4ek", &r));         /* keycap */
	CHECK(Parse(0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB\nb45pc4ek", &r)); /* ZWJ */
	CHECK(Parse(0, "\xF0\x9F\x91\x8D\nb45pc4eo", &r));                  /* o reads as 0 */

	/* ordinary text */
	CHECK(!Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D nice\nb45pc4ek", &r));
	CHECK(!Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D", &r));
	CHECK(!Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4e", &r));
	CHECK(!Parse(1, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4eu", &r));     /* u isn't Base32 */
	CHECK(!Parse(1, "@[]\xF0\x9F\x91\x8D\nb45pc4ek", &r));
	CHECK(!Parse(1, "@[AlphaNode]ok\nb45pc4ek", &r));
	CHECK(!Parse(1, "\xF0\x9F\x91\x8D\nb45pc4ek", &r));                 /* a channel one names the target */
	CHECK(!Parse(0, "@[AlphaNode]\xF0\x9F\x91\x8D\nb45pc4ek", &r));     /* a DM one doesn't */
	CHECK(!Parse(0, "1\nb45pc4ek", &r));
	CHECK(!Parse(0, "\xC2\xA9\nb45pc4ek", &r));                          /* bare (c) */
	CHECK(!Parse(0, "\xF0\x9F\x91\x8D\xF0\x9F\x91\x8D\nb45pc4ek", &r));  /* two */
	CHECK(!Parse(0, "hello\nb45pc4ek", &r));
	CHECK(!Parse(0, "\nb45pc4ek", &r));
}

static VmRxLog Rx(UInt8 type, Boolean flood, UInt8 hops, UInt8 b0, UInt8 b1, UInt8 b2)
{
	VmRxLog rx;

	memset(&rx, 0, sizeof(rx));
	rx.payloadType = type;
	rx.flood = flood;
	rx.hops = hops;
	rx.hashSize = 2;
	rx.pathBytes = (UInt8)(hops * 2);
	rx.path[0] = 0xa1;
	rx.payloadLen = 3;
	rx.payload[0] = b0;
	rx.payload[1] = b1;
	rx.payload[2] = b2;
	return rx;
}

static void test_rxpath(void)
{
	static const UInt8 alice[6] = { 0x5a, 1, 2, 3, 4, 5 };
	VmRxLog a, out;

	RxPathClear();
	a = Rx(VM_PAYLOAD_ADVERT, 1, 1, 1, 2, 3);
	RxPathNote(&a, 1000);                                       /* not text: ignored */
	CHECK(!RxPathTake(1, 1 | 0x40, 0, 1001, &out));
	a = Rx(VM_PAYLOAD_GRP_TXT, 1, 2, 7, 7, 7);
	RxPathNote(&a, 1000);
	a = Rx(VM_PAYLOAD_GRP_TXT, 1, 3, 7, 7, 7);                  /* the same packet by a longer way */
	RxPathNote(&a, 1001);
	a = Rx(VM_PAYLOAD_GRP_TXT, 1, 3, 8, 8, 8);
	RxPathNote(&a, 1002);
	CHECK(RxPathTake(1, 2 | 0x40, 0, 1003, &out) && out.hops == 2 && out.payload[0] == 7);
	/* its 3-hop copy is used up, so a 3-hop message gets the other packet */
	CHECK(RxPathTake(1, 3 | 0x40, 0, 1003, &out) && out.payload[0] == 8);
	CHECK(!RxPathTake(1, 3 | 0x40, 0, 1003, &out));

	/* DMs match the sender's hash byte; direct ones a direct packet */
	a = Rx(VM_PAYLOAD_TXT, 1, 1, 0x11, 0x99, 1);
	RxPathNote(&a, 2000);
	a = Rx(VM_PAYLOAD_TXT, 1, 1, 0x11, 0x5a, 2);
	RxPathNote(&a, 2000);
	a = Rx(VM_PAYLOAD_TXT, 0, 0, 0x11, 0x5a, 3);
	RxPathNote(&a, 2000);
	CHECK(RxPathTake(0, 1 | 0x40, alice, 2001, &out) && out.payload[2] == 2);
	CHECK(RxPathTake(0, VM_PATH_UNKNOWN, alice, 2001, &out) && out.payload[2] == 3);
	CHECK(!RxPathTake(1, 1 | 0x40, 0, 2001, &out));            /* a channel message isn't a DM packet */

	/* too old */
	a = Rx(VM_PAYLOAD_GRP_TXT, 1, 1, 9, 9, 9);
	RxPathNote(&a, 3000);
	CHECK(!RxPathTake(1, 1 | 0x40, 0, 3000 + VM_RXPATH_SECS + 1, &out));
}

int main(void)
{
	test_rxpath();
	test_hash();
	test_body();
	test_build();
	test_parse();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("reactions: all tests passed\n");
	return 0;
}
