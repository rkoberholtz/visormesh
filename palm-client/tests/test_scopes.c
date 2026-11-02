/* Host-side tests for flood scopes (src/scope_proto.c). Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/scope_proto.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* The radio gave the hashtag channel "#test" this key (docs/progress.md); scopes use the same scheme. */
static const UInt8 kTestKey[16] = {
	0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b, 0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f
};

static void test_key(void)
{
	UInt8 key[16], key2[16];

	VmScopeKey("test", key);
	CHECK(memcmp(key, kTestKey, 16) == 0);
	VmScopeKey("#test", key2);              /* a typed '#' changes nothing */
	CHECK(memcmp(key, key2, 16) == 0);
	VmScopeKey("Test", key2);               /* names are case-sensitive */
	CHECK(memcmp(key, key2, 16) != 0);
}

static void test_names(void)
{
	CHECK(VmScopeNameValid("phl"));
	CHECK(VmScopeNameValid("#us-pa-phl"));
	CHECK(!VmScopeNameValid(""));
	CHECK(!VmScopeNameValid("#"));
	CHECK(!VmScopeNameValid("$private"));
	CHECK(!VmScopeNameValid("has space"));
	CHECK(VmScopeNameValid("abcdefghijklmnopqrstuvwxyz1234"));    /* 30 */
	CHECK(!VmScopeNameValid("abcdefghijklmnopqrstuvwxyz12345"));  /* 31 */
	CHECK(strcmp(VmScopeBareName("#phl"), "phl") == 0);
}

static void test_frames(void)
{
	UInt8 out[64];
	UInt16 n;

	n = VmBuildSetScopeKey(out, sizeof(out), kTestKey);
	CHECK(n == 3 + 18);
	CHECK(out[0] == '<' && out[1] == 18 && out[2] == 0);
	CHECK(out[3] == VM_CMD_SET_FLOOD_SCOPE_KEY && out[4] == 0);
	CHECK(memcmp(out + 5, kTestKey, 16) == 0);

	n = VmBuildSetScopeKey(out, sizeof(out), NULL);
	CHECK(n == 5 && out[1] == 2 && out[3] == 54 && out[4] == 0);

	n = VmBuildSetUnscoped(out, sizeof(out));
	CHECK(n == 5 && out[3] == 54 && out[4] == 1);

	n = VmBuildSetDefaultScope(out, sizeof(out), "#test");
	CHECK(n == 3 + 48);
	CHECK(out[3] == VM_CMD_SET_DEFAULT_FLOOD_SCOPE);
	CHECK(memcmp(out + 4, "test\0", 5) == 0);              /* stored without the '#' */
	CHECK(out[4 + 30] == 0);
	CHECK(memcmp(out + 4 + 31, kTestKey, 16) == 0);

	n = VmBuildSetDefaultScope(out, sizeof(out), NULL);
	CHECK(n == 4 && out[3] == 63);
	CHECK(VmBuildSetDefaultScope(out, sizeof(out), "$secret") == 0);

	n = VmBuildGetDefaultScope(out, sizeof(out));
	CHECK(n == 4 && out[3] == 64);
	CHECK(VmBuildSetScopeKey(out, 10, kTestKey) == 0);     /* too small */
}

static void test_parse(void)
{
	UInt8 p[1 + 31 + 16];
	Char name[VM_SCOPE_NAME_SIZE];
	UInt8 key[16];

	memset(p, 0, sizeof(p));
	p[0] = VM_RESP_DEFAULT_FLOOD_SCOPE;
	memcpy(p + 1, "test", 4);
	memcpy(p + 32, kTestKey, 16);
	CHECK(VmParseDefaultScope(p, sizeof(p), name, key));
	CHECK(strcmp(name, "test") == 0 && memcmp(key, kTestKey, 16) == 0);

	CHECK(VmParseDefaultScope(p, 1, name, key));             /* none */
	CHECK(name[0] == 0 && key[0] == 0);
	CHECK(!VmParseDefaultScope(p, 20, name, key));           /* truncated */
	p[0] = 0;
	CHECK(!VmParseDefaultScope(p, sizeof(p), name, key));    /* wrong code */

	memset(p + 1, 'x', 31);                                  /* no NUL in the field */
	p[0] = VM_RESP_DEFAULT_FLOOD_SCOPE;
	CHECK(VmParseDefaultScope(p, sizeof(p), name, key));
	CHECK(strlen(name) == 30);
}

static void test_regions(void)
{
	static const UInt8 frame[] = {
		VM_PUSH_BINARY_RESPONSE, 0, 0x78, 0x56, 0x34, 0x12, 1, 2, 3, 4,
		'*', ',', 'u', 's', '-', 'p', 'a', ',', '$', 'p', 'r', 'v', ',', 'p', 'h', 'l', 0
	};
	UInt8 key[32], out[64];
	const UInt8 *names;
	UInt16 len, pos = 0, n;
	UInt32 tag;
	Char name[VM_SCOPE_NAME_SIZE];

	memset(key, 0xAB, sizeof(key));
	n = VmBuildRegionsReq(out, sizeof(out), key);
	CHECK(n == 3 + 35);
	CHECK(out[3] == VM_CMD_SEND_ANON_REQ && out[4] == 0xAB && out[35] == 0xAB);
	CHECK(out[36] == VM_ANON_REQ_REGIONS && out[37] == 0);

	CHECK(VmParseRegionsReply(frame, sizeof(frame), &tag, &names, &len));
	CHECK(tag == 0x12345678);
	CHECK(len == 16);                            /* stops at the NUL */
	CHECK(VmNextRegionName(names, len, &pos, name) && strcmp(name, "*") == 0);
	CHECK(VmNextRegionName(names, len, &pos, name) && strcmp(name, "us-pa") == 0);
	CHECK(VmNextRegionName(names, len, &pos, name) && strcmp(name, "$prv") == 0);
	CHECK(VmNextRegionName(names, len, &pos, name) && strcmp(name, "phl") == 0);
	CHECK(!VmNextRegionName(names, len, &pos, name));

	CHECK(VmParseRegionsReply(frame, 10, &tag, &names, &len) && len == 0);   /* no regions */
	CHECK(!VmParseRegionsReply(frame, 9, &tag, &names, &len));
}

int main(void)
{
	test_key();
	test_names();
	test_frames();
	test_parse();
	test_regions();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("scopes: all tests passed\n");
	return 0;
}
