/* Host-side tests for the companion protocol. Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/protocol.h"
#include "fixtures.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static int feed(VmFrameDecoder *d, const UInt8 *bytes, int n)
{
	int i, frames = 0;
	for (i = 0; i < n; i++)
		frames += VmDecoderFeed(d, bytes[i]);
	return frames;
}

/* Decodes a single complete frame into d; fails the test if it isn't exactly one frame. */
static void decode_one(VmFrameDecoder *d, const UInt8 *bytes, int n)
{
	VmDecoderReset(d);
	CHECK(feed(d, bytes, n) == 1);
}

static void test_encode_device_query(void)
{
	UInt8 payload[] = { VM_CMD_DEVICE_QUERY, VM_APP_TARGET_VER };
	UInt8 out[8];
	UInt8 expect[] = { '<', 0x02, 0x00, 0x16, 0x03 };

	CHECK(VmEncodeFrame(out, sizeof(out), payload, 2) == 5);
	CHECK(memcmp(out, expect, 5) == 0);
	CHECK(VmEncodeFrame(out, 4, payload, 2) == 0);
}

static void test_decode_with_leading_junk(void)
{
	VmFrameDecoder d;
	UInt8 in[] = { 0x00, '<', 0xFF, '>', 0x03, 0x00, 0x0D, 0xAA, 0xBB };

	VmDecoderReset(&d);
	CHECK(feed(&d, in, sizeof(in)) == 1);
	CHECK(d.len == 3);
	CHECK(d.payload[0] == 0x0D && d.payload[2] == 0xBB);
}

static void test_decode_back_to_back(void)
{
	VmFrameDecoder d;
	UInt8 in[] = { '>', 0x01, 0x00, 0x83, '>', 0x02, 0x00, 0x05, 0x06 };

	VmDecoderReset(&d);
	CHECK(feed(&d, in, 4) == 1 && d.len == 1 && d.payload[0] == 0x83);
	CHECK(feed(&d, in + 4, 5) == 1 && d.len == 2 && d.payload[1] == 0x06);
}

static void test_decode_oversize_resyncs(void)
{
	VmFrameDecoder d;
	UInt8 bad[] = { '>', 0xFF, 0x00 };
	UInt8 good[] = { '>', 0x01, 0x00, 0x42 };

	VmDecoderReset(&d);
	CHECK(feed(&d, bad, sizeof(bad)) == 0);
	CHECK(feed(&d, good, sizeof(good)) == 1 && d.payload[0] == 0x42);
}

static void test_decode_max_frame(void)
{
	VmFrameDecoder d;
	UInt8 hdr[] = { '>', VM_MAX_FRAME & 0xFF, VM_MAX_FRAME >> 8 };
	int i, frames;

	VmDecoderReset(&d);
	frames = feed(&d, hdr, 3);
	for (i = 0; i < VM_MAX_FRAME; i++)
		frames += VmDecoderFeed(&d, (UInt8)i);
	CHECK(frames == 1 && d.len == VM_MAX_FRAME && d.payload[VM_MAX_FRAME - 1] == (VM_MAX_FRAME - 1));
}

/* DEVICE_INFO captured from the SB-LoRa XIAO (MeshCore v1.17.1) with firmware/tools/companion_query.py.
 * Generated from the capture file, not retyped (an earlier hand-typed copy had two bytes shifted). */
static void test_decode_real_device_info(void)
{
	static const UInt8 in[] = {
		0x3e, 0x52, 0x00, 0x0d, 0x0d, 0xaf, 0x28, 0x00, 0x00, 0x00, 0x00, 0x31,
		0x34, 0x20, 0x41, 0x75, 0x67, 0x20, 0x32, 0x30, 0x32, 0x36, 0x00, 0x53,
		0x65, 0x65, 0x65, 0x64, 0x20, 0x58, 0x69, 0x61, 0x6f, 0x2d, 0x6e, 0x72,
		0x66, 0x35, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x76, 0x31, 0x2e, 0x31, 0x37, 0x2e, 0x31, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00
	};
	VmFrameDecoder d;

	VmDecoderReset(&d);
	CHECK(feed(&d, in, sizeof(in)) == 1);
	CHECK(d.len == 0x52);
	CHECK(d.payload[0] == 0x0d);
	CHECK(memcmp(&d.payload[8], "14 Aug 2026", 11) == 0);
	CHECK(memcmp(&d.payload[20], "Seeed Xiao-nrf52", 16) == 0);
	CHECK(memcmp(&d.payload[60], "v1.17.1", 7) == 0);
	CHECK(sizeof(in) == VM_HEADER_LEN + 0x52);

	{
		VmDeviceInfo info;

		CHECK(VmParseDeviceInfo(d.payload, d.len, &info));
		CHECK(info.fwVerCode == 0x0d);
		CHECK(info.maxContacts == 350 && info.maxChannels == 40);
		CHECK(strcmp(info.buildDate, "14 Aug 2026") == 0);
		CHECK(strcmp(info.manufacturer, "Seeed Xiao-nrf52") == 0);
		CHECK(strcmp(info.version, "v1.17.1") == 0);
		CHECK(info.hasRepeat && !info.repeatEnabled);
		CHECK(info.hasPathHashMode && info.pathHashMode == 0);
		CHECK(!VmParseDeviceInfo(d.payload, 79, &info));
		CHECK(VmParseDeviceInfo(d.payload, 80, &info) && !info.hasRepeat && !info.hasPathHashMode);
	}
}

/* Builders must produce exactly the frames companion_query.py sent to the real radio. */
static void test_builders_match_bench_frames(void)
{
	UInt8 out[64];
	static const UInt8 query[] = { 0x3c, 0x02, 0x00, 0x16, 0x03 };
	static const UInt8 appStart[] = {
		0x3c, 0x11, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		'V', 'i', 's', 'o', 'r', 'M', 'e', 's', 'h'
	};
	static const UInt8 contacts[] = { 0x3c, 0x01, 0x00, 0x04 };
	static const UInt8 contactsSince[] = { 0x3c, 0x05, 0x00, 0x04, 0x78, 0x56, 0x34, 0x12 };
	static const UInt8 sync[] = { 0x3c, 0x01, 0x00, 0x0a };

	CHECK(VmBuildDeviceQuery(out, sizeof(out)) == sizeof(query));
	CHECK(memcmp(out, query, sizeof(query)) == 0);
	CHECK(VmBuildAppStart(out, sizeof(out), "VisorMesh") == sizeof(appStart));
	CHECK(memcmp(out, appStart, sizeof(appStart)) == 0);
	CHECK(VmBuildGetContacts(out, sizeof(out), 0) == sizeof(contacts));
	CHECK(memcmp(out, contacts, sizeof(contacts)) == 0);
	CHECK(VmBuildGetContacts(out, sizeof(out), 0x12345678UL) == sizeof(contactsSince));
	CHECK(memcmp(out, contactsSince, sizeof(contactsSince)) == 0);
	CHECK(VmBuildSyncNextMessage(out, sizeof(out)) == sizeof(sync));
	CHECK(memcmp(out, sync, sizeof(sync)) == 0);
	CHECK(VmBuildAppStart(out, 10, "VisorMesh") == 0);
}

/* Settings commands, laid out per the MyMesh.cpp handlers (multi-byte fields little-endian). */
static void test_settings_builders(void)
{
	UInt8 out[64];
	static const UInt8 name[] = { 0x3c, 0x09, 0x00, 0x08, 'V', 'i', 's', 'o', 'r', 'N', 'e', 'o' };
	/* USA preset: 910.525 MHz (0x000de4bd), BW 62.5 kHz (62500 = 0x0000f424), SF7, CR5, repeat off */
	static const UInt8 radio[] = {
		0x3c, 0x0c, 0x00, 0x0b, 0xbd, 0xe4, 0x0d, 0x00, 0x24, 0xf4, 0x00, 0x00, 0x07, 0x05, 0x00
	};
	static const UInt8 power[] = { 0x3c, 0x02, 0x00, 0x0c, 0xf7 };
	static const UInt8 hash[] = { 0x3c, 0x03, 0x00, 0x3d, 0x00, 0x02 };

	CHECK(VmBuildSetAdvertName(out, sizeof(out), "VisorNeo") == sizeof(name));
	CHECK(memcmp(out, name, sizeof(name)) == 0);
	CHECK(VmBuildSetAdvertName(out, sizeof(out), "") == 0);
	CHECK(VmBuildSetAdvertName(out, sizeof(out), "0123456789012345678901234567890123456789") == 3 + 1 + 31);

	CHECK(VmBuildSetRadioParams(out, sizeof(out), 910525UL, 62500UL, 7, 5, 1, 0) == sizeof(radio));
	CHECK(memcmp(out, radio, sizeof(radio)) == 0);
	CHECK(VmBuildSetRadioParams(out, sizeof(out), 910525UL, 62500UL, 7, 5, 0, 0) == sizeof(radio) - 1);
	CHECK(out[1] == 0x0b);
	CHECK(VmBuildSetRadioParams(out, sizeof(out), 910525UL, 62500UL, 7, 5, 1, 1) == sizeof(radio));
	CHECK(out[sizeof(radio) - 1] == 1);

	CHECK(VmBuildSetTxPower(out, sizeof(out), -9) == sizeof(power));
	CHECK(memcmp(out, power, sizeof(power)) == 0);
	CHECK(VmBuildSetPathHashMode(out, sizeof(out), 2) == sizeof(hash));
	CHECK(memcmp(out, hash, sizeof(hash)) == 0);
	{
		static const UInt8 reboot[] = { 0x3c, 0x07, 0x00, 0x13, 'r', 'e', 'b', 'o', 'o', 't' };

		CHECK(VmBuildReboot(out, sizeof(out)) == sizeof(reboot));
		CHECK(memcmp(out, reboot, sizeof(reboot)) == 0);
	}
}

static void test_path_len(void)
{
	CHECK(VM_PATH_HOPS(0x00) == 0 && VM_PATH_HASH_BYTES(0x00) == 1);
	CHECK(VM_PATH_HOPS(0x02) == 2 && VM_PATH_HASH_BYTES(0x02) == 1);
	CHECK(VM_PATH_HOPS(0x43) == 3 && VM_PATH_HASH_BYTES(0x43) == 2);   /* PhillyMesh: 2-byte hashes */
	CHECK(VM_PATH_HOPS(0x81) == 1 && VM_PATH_HASH_BYTES(0x81) == 3);
}

static void test_decimals(void)
{
	UInt32 v;
	Int32 i;
	Char buf[16];

	CHECK(VmParseDecimal("910.525", 3, &v) && v == 910525UL);
	CHECK(VmParseDecimal("921", 3, &v) && v == 921000UL);
	CHECK(VmParseDecimal("62.5", 3, &v) && v == 62500UL);
	CHECK(VmParseDecimal("250", 3, &v) && v == 250000UL);
	CHECK(VmParseDecimal("0.5", 3, &v) && v == 500UL);
	CHECK(VmParseDecimal(".5", 3, &v) && v == 500UL);
	CHECK(!VmParseDecimal("910.5255", 3, &v));
	CHECK(!VmParseDecimal("9.1.0", 3, &v));
	CHECK(!VmParseDecimal("", 3, &v));
	CHECK(!VmParseDecimal(".", 3, &v));
	CHECK(!VmParseDecimal("91a", 3, &v));
	CHECK(!VmParseDecimal("99999999999", 3, &v));

	VmFormatDecimal(buf, 910525UL, 3); CHECK(strcmp(buf, "910.525") == 0);
	VmFormatDecimal(buf, 921000UL, 3); CHECK(strcmp(buf, "921") == 0);
	VmFormatDecimal(buf, 62500UL, 3);  CHECK(strcmp(buf, "62.5") == 0);
	VmFormatDecimal(buf, 7800UL, 3);   CHECK(strcmp(buf, "7.8") == 0);
	VmFormatDecimal(buf, 500UL, 3);    CHECK(strcmp(buf, "0.5") == 0);
	VmFormatDecimal(buf, 0UL, 3);      CHECK(strcmp(buf, "0") == 0);
	VmFormatDecimal(buf, 869618UL, 3); CHECK(strcmp(buf, "869.618") == 0);

	CHECK(VmParseInt("22", &i) && i == 22);
	CHECK(VmParseInt("-9", &i) && i == -9);
	CHECK(!VmParseInt("-", &i) && !VmParseInt("", &i) && !VmParseInt("2x", &i));
}

static void test_parse_self_info(void)
{
	VmFrameDecoder d;
	VmSelfInfo s;

	decode_one(&d, kSelfInfo, sizeof(kSelfInfo));
	CHECK(VmParseSelfInfo(d.payload, d.len, &s));
	CHECK(s.advType == 1);
	CHECK(s.txPowerDbm == 22 && s.maxTxPowerDbm == 22);
	CHECK(s.pubKey[0] == 0x10 && s.pubKey[31] == 0x2f);
	CHECK(s.latE6 == 0 && s.lonE6 == 0);
	CHECK(s.freqKHz == 869618UL);
	CHECK(s.bwHz == 62500UL);
	CHECK(s.sf == 8 && s.cr == 5);
	CHECK(strcmp(s.name, "TestNode") == 0);
	CHECK(!VmParseSelfInfo(d.payload, 57, &s));
	CHECK(!VmParseContact(d.payload, d.len, (VmContact *)0));
}

static void test_parse_contacts(void)
{
	VmFrameDecoder d;
	VmContact c;
	UInt32 n;
	int frames = 0, i;

	decode_one(&d, kContactsStart, sizeof(kContactsStart));
	CHECK(VmParseContactsStart(d.payload, d.len, &n) && n == 1);

	decode_one(&d, kContact, sizeof(kContact));
	CHECK(d.len == 148);
	CHECK(VmParseContact(d.payload, d.len, &c));
	CHECK(c.pubKey[0] == 0xa0 && c.pubKey[31] == 0xbf);
	CHECK(c.type == 2 && c.flags == 0 && c.outPathLen == 2);
	CHECK(strcmp(c.name, "Hilltop Repeater") == 0);
	CHECK(c.lastAdvert == 1793000000UL);
	CHECK(c.latE6 == 40712345L && c.lonE6 == -74006789L);
	CHECK(c.lastMod == 1793000123UL);
	CHECK(!VmParseContact(d.payload, 147, &c));

	decode_one(&d, kEndOfContacts, sizeof(kEndOfContacts));
	CHECK(VmParseEndOfContacts(d.payload, d.len, &n) && n == 1793000123UL);

	/* Real empty-list reply: two back-to-back frames. */
	VmDecoderReset(&d);
	for (i = 0; i < (int)sizeof(kContactsEmpty); i++) {
		if (!VmDecoderFeed(&d, kContactsEmpty[i]))
			continue;
		frames++;
		if (frames == 1)
			CHECK(VmParseContactsStart(d.payload, d.len, &n) && n == 0);
		else
			CHECK(VmParseEndOfContacts(d.payload, d.len, &n) && n == 0);
	}
	CHECK(frames == 2);
}

static void test_parse_messages(void)
{
	VmFrameDecoder d;
	VmMessage m;
	static const UInt8 sender[6] = { 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5 };

	decode_one(&d, kContactMsg, sizeof(kContactMsg));
	CHECK(VmParseMessage(d.payload, d.len, &m));
	CHECK(!m.isChannel);
	CHECK(m.snrQuarterDb == -10);
	CHECK(memcmp(m.senderPrefix, sender, 6) == 0);
	CHECK(m.pathLen == 0xff && m.txtType == VM_TXT_TYPE_PLAIN);
	CHECK(m.timestamp == 1793001000UL);
	CHECK(m.textLen == 11 && memcmp(m.text, "hello visor", 11) == 0);

	decode_one(&d, kSignedMsg, sizeof(kSignedMsg));
	CHECK(VmParseMessage(d.payload, d.len, &m));
	CHECK(m.txtType == VM_TXT_TYPE_SIGNED_PLAIN && m.snrQuarterDb == 20);
	CHECK(m.textLen == 9 && memcmp(m.text, "signed hi", 9) == 0);

	decode_one(&d, kChannelMsg, sizeof(kChannelMsg));
	CHECK(VmParseMessage(d.payload, d.len, &m));
	CHECK(m.isChannel && m.channelIdx == 0 && m.pathLen == 3);
	CHECK(m.timestamp == 1793001002UL);
	CHECK(m.textLen == 13 && memcmp(m.text, "Alice: hi all", 13) == 0);

	decode_one(&d, kNoMoreMessages, sizeof(kNoMoreMessages));
	CHECK(d.len == 1 && d.payload[0] == VM_RESP_NO_MORE_MESSAGES);
	CHECK(!VmParseMessage(d.payload, d.len, &m));

	/* Truncated frames are rejected, not over-read. */
	decode_one(&d, kChannelMsg, sizeof(kChannelMsg));
	CHECK(!VmParseMessage(d.payload, 10, &m));
	decode_one(&d, kSignedMsg, sizeof(kSignedMsg));
	CHECK(!VmParseMessage(d.payload, 17, &m));
}

int main(void)
{
	test_encode_device_query();
	test_decode_with_leading_junk();
	test_decode_back_to_back();
	test_decode_oversize_resyncs();
	test_decode_max_frame();
	test_decode_real_device_info();
	test_builders_match_bench_frames();
	test_settings_builders();
	test_decimals();
	test_path_len();
	test_parse_self_info();
	test_parse_contacts();
	test_parse_messages();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all protocol tests passed\n");
	return 0;
}
