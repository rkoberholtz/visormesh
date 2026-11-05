/* Host-side tests for the newer companion commands and share links (src/protocol2.c). Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/protocol2.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_trace(void)
{
	UInt8 path[] = { 0x3f, 0x2a, 0x91, 0xc0, 0x3f, 0x2a };
	UInt8 f[64];
	UInt8 push[] = { 0x89, 0, 6, 1, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0,
	                 0x3f, 0x2a, 0x91, 0xc0, 0x3f, 0x2a,      /* hashes */
	                 24, 0xF8, 10,                             /* per hop: 6, -2, 2.5 dB */
	                 20 };                                     /* ours: 5 dB */
	VmTrace t;
	UInt16 n;

	n = VmBuildTrace(f, sizeof(f), 0x12345678, 2, path, sizeof(path));
	CHECK(n == 3 + 10 + 6);
	CHECK(f[0] == '<' && f[1] == 16 && f[3] == VM_CMD_SEND_TRACE_PATH);
	CHECK(f[4] == 0x78 && f[7] == 0x12 && f[12] == 1 && f[13] == 0x3f && f[18] == 0x2a);
	CHECK(VmBuildTrace(f, sizeof(f), 1, 2, path, 5) == 0);     /* not whole hops */
	CHECK(VmBuildTrace(f, sizeof(f), 1, 3, path, 6) == 0);     /* no 3-byte traces */
	CHECK(VmBuildTrace(f, sizeof(f), 1, 1, path, 0) == 0);

	CHECK(VmParseTrace(push, sizeof(push), &t));
	CHECK(t.tag == 0x12345678 && t.hashSize == 2 && t.hops == 3);
	CHECK(t.hashes[2] == 0x91 && t.snrQ[0] == 24 && t.snrQ[1] == -8 && t.snrQ[3] == 20);
	CHECK(!VmParseTrace(push, sizeof(push) - 1, &t));
}

static void test_stats(void)
{
	UInt8 batt[] = { 12, 0xD2, 0x0F, 12, 0, 0, 0, 0xE4, 1, 0, 0 };
	UInt8 old[] = { 12, 0xD2, 0x0F };
	UInt8 radio[] = { 24, 1, 0x90, 0xFF, 0xA0, 0xEC, 120, 0, 0, 0, 0x10, 0x0E, 0, 0 };
	UInt8 core[] = { 24, 0, 0xD2, 0x0F, 0x80, 0x51, 0x01, 0, 0, 0, 2 };
	UInt8 pk[26] = { 24, 2, 100 };
	VmBattery b;
	VmStats s;

	CHECK(VmParseBattery(batt, sizeof(batt), &b) && b.battMv == 4050 && b.hasStorage && b.usedKb == 12 && b.totalKb == 484);
	CHECK(VmParseBattery(old, sizeof(old), &b) && b.battMv == 4050 && !b.hasStorage);
	memset(&s, 0, sizeof(s));
	CHECK(VmParseStats(radio, sizeof(radio), &s) && s.hasRadio && !s.hasCore);
	CHECK(s.noiseFloor == -112 && s.lastRssi == -96 && s.lastSnrQ == -20 && s.txAirSecs == 120 && s.rxAirSecs == 3600);
	CHECK(VmParseStats(core, sizeof(core), &s) && s.hasCore && s.uptimeSecs == 86400 && s.queueLen == 2);
	CHECK(VmParseStats(pk, sizeof(pk), &s) && s.hasPackets && s.recv == 100);
	CHECK(!VmParseStats(radio, 10, &s));
}

static void test_route(void)
{
	UInt8 c[VM_CONTACT_FRAME_LEN], f[200], pathLen, path[64], hops[] = { 0xaa, 0xbb, 0xcc, 0xdd };
	UInt16 n, i;

	memset(c, 0, sizeof(c));
	c[0] = VM_RESP_CONTACT;
	c[1] = 0x11;
	c[33] = VM_ADV_TYPE_REPEATER;
	c[35] = 0x41;           /* 2-byte hashes, 1 hop */
	c[36] = 0x12; c[37] = 0x34;
	memcpy(c + 100, "Rpt", 3);
	c[147] = 0x99;          /* lastmod: not sent back */
	CHECK(VmContactPath(c, sizeof(c), &pathLen, path) && pathLen == 0x41 && path[1] == 0x34);
	n = VmBuildContactRoute(f, sizeof(f), c, sizeof(c), 2, hops, 2);
	CHECK(n == 3 + 144);
	CHECK(f[3] == VM_CMD_ADD_UPDATE_CONTACT && f[4] == 0x11 && f[3 + 33] == VM_ADV_TYPE_REPEATER);
	CHECK(f[3 + 35] == 0x42 && f[3 + 36] == 0xaa && f[3 + 39] == 0xdd && f[3 + 40] == 0);
	CHECK(memcmp(f + 3 + 100, "Rpt", 3) == 0);
	n = VmBuildContactRoute(f, sizeof(f), c, sizeof(c), 2, hops, 0);
	CHECK(n && f[3 + 35] == 0x40);
	for (i = 0; i < 4; i++)
		CHECK(f[3 + 36 + i] == 0);
	CHECK(VmBuildContactRoute(f, sizeof(f), c, 100, 2, hops, 1) == 0);
}

static void test_hops(void)
{
	UInt8 b[16], size;

	CHECK(VmParseHopCodes("3f2a, 91C0 3f2a", b, sizeof(b), &size) == 3 && size == 2 && b[2] == 0x91 && b[3] == 0xc0);
	CHECK(VmParseHopCodes("", b, sizeof(b), &size) == 0);
	CHECK(VmParseHopCodes("a1 b2c3", b, sizeof(b), &size) == -1);   /* mixed sizes */
	CHECK(VmParseHopCodes("abc", b, sizeof(b), &size) == -1);
	CHECK(VmParseHopCodes("12x4", b, sizeof(b), &size) == -1);
	CHECK(VmParseHopCodes("11223344", b, sizeof(b), &size) == 1 && size == 4);
	CHECK(VmParseHopCodes("1122 3344 5566", b, 4, &size) == -1);    /* too many */
}

static void test_share(void)
{
	Char out[200];
	UInt8 key[32], secret[16];
	VmShare s;
	UInt16 n, i;
	const char *ch = "meshcore://channel/add?name=%23test+one&secret=000102030405060708090a0b0c0d0e0f&region_scope=pa";

	for (i = 0; i < 32; i++)
		key[i] = (UInt8)i;
	for (i = 0; i < 16; i++)
		secret[i] = (UInt8)(0xf0 + i);
	n = VmShareContact(out, sizeof(out), (const UInt8 *)"Bob K", 5, key, 2);
	out[n] = 0;
	CHECK(strcmp(out, "meshcore://contact/add?name=Bob%20K&public_key=000102030405060708090a0b0c0d0e0f"
	                  "101112131415161718191a1b1c1d1e1f&type=2") == 0);
	CHECK(VmShareParse(out, n, &s) && s.kind == VM_SHARE_CONTACT && s.type == 2 && s.nameLen == 5 &&
	      memcmp(s.name, "Bob K", 5) == 0 && s.key[31] == 31);
	CHECK(VmShareContact(out, 60, (const UInt8 *)"Bob", 3, key, 1) == 0);

	n = VmShareChannel(out, sizeof(out), (const UInt8 *)"#test", 5, secret);
	out[n] = 0;
	CHECK(strcmp(out, "meshcore://channel/add?name=%23test&secret=f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff") == 0);
	CHECK(VmShareParse(ch, (UInt16)strlen(ch), &s) && s.kind == VM_SHARE_CHANNEL && s.nameLen == 9 &&
	      memcmp(s.name, "#test one", 9) == 0 && s.secret[15] == 15);
	CHECK(!VmShareParse("meshcore://channel/add?name=x&secret=00", 39, &s));
	CHECK(!VmShareParse("meshcore://contact/add?public_key=00", 36, &s));
	CHECK(!VmShareParse("https://example.com", 19, &s));
	/* a UTF-8 name round trip */
	n = VmShareContact(out, sizeof(out), (const UInt8 *)"Caf\xc3\xa9", 5, key, 1);
	CHECK(VmShareParse(out, n, &s) && s.nameLen == 5 && memcmp(s.name, "Caf\xc3\xa9", 5) == 0);
}

static void test_hashtag(void)
{
	const char *t = "join #Philly-Mesh now, or #1 #-x #";
	Char out[32];

	CHECK(VmHashtagAt(t, (UInt16)strlen(t), 5, out, sizeof(out)) && strcmp(out, "#philly-mesh") == 0);
	CHECK(VmHashtagAt(t, (UInt16)strlen(t), 12, out, sizeof(out)) && strcmp(out, "#philly-mesh") == 0);
	CHECK(!VmHashtagAt(t, (UInt16)strlen(t), 17, out, sizeof(out)));    /* the space after it */
	CHECK(!VmHashtagAt(t, (UInt16)strlen(t), 2, out, sizeof(out)));
	CHECK(VmHashtagAt(t, (UInt16)strlen(t), 26, out, sizeof(out)) && strcmp(out, "#1") == 0);
	CHECK(!VmHashtagAt(t, (UInt16)strlen(t), 30, out, sizeof(out)));    /* #-x */
	CHECK(!VmHashtagAt(t, (UInt16)strlen(t), 33, out, sizeof(out)));    /* a lone # */
	CHECK(!VmHashtagAt(t, (UInt16)strlen(t), 5, out, 5));               /* too long for out */
}

int main(void)
{
	test_trace();
	test_stats();
	test_route();
	test_hops();
	test_share();
	test_hashtag();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("protocol2: all tests passed\n");
	return 0;
}
