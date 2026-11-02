/* Host-side tests for the companion protocol. Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/protocol.h"
#include "../src/remote_fmt.h"
#include "fixtures.h"
#include "../src/sha256.h"

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

static void test_sha256_and_channels(void)
{
	UInt8 h[32], out[64];
	static const UInt8 abc[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
	};
	/* companion_protocol.md: hashtag channel #test has key 9cd8fcf22a47333b591d96a2b848b73f */
	static const UInt8 testKey[16] = {
		0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b, 0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f
	};
	UInt8 longMsg[200];
	static const UInt8 longDigest[4] = { 0xc2, 0xa9, 0x08, 0xd9 };   /* sha256(200 x 'a') starts c2a908d9 (Python hashlib) */

	VmSha256((const UInt8 *)"abc", 3, h);
	CHECK(memcmp(h, abc, 32) == 0);
	VmSha256((const UInt8 *)"#test", 5, h);
	CHECK(memcmp(h, testKey, 16) == 0);
	memset(longMsg, 'a', sizeof(longMsg));                          /* several blocks + a 2-block tail */
	VmSha256(longMsg, sizeof(longMsg), h);
	CHECK(memcmp(h, longDigest, 4) == 0);

	{
		/* padding boundaries: digest prefixes of n x 'x' from Python hashlib */
		static const struct { UInt16 n; UInt8 d[4]; } v[] = {
			{ 55, { 0xd5, 0xe2, 0x85, 0x68 } }, { 56, { 0x04, 0xc2, 0x62, 0x61 } },
			{ 63, { 0x75, 0x22, 0x0b, 0x47 } }, { 64, { 0x7c, 0xe1, 0x00, 0x97 } },
			{ 119, { 0x00, 0x0b, 0x48, 0xd4 } }, { 120, { 0x13, 0xf0, 0x5a, 0x0b } }
		};
		UInt16 k;

		memset(longMsg, 'x', sizeof(longMsg));
		for (k = 0; k < sizeof(v) / sizeof(v[0]); k++) {
			VmSha256(longMsg, v[k].n, h);
			CHECK(memcmp(h, v[k].d, 4) == 0);
		}
	}

	{
		UInt8 k[16];

		CHECK(VmParseHex("9cd8fcf22a47333b591d96a2b848b73f", k, 16) && memcmp(k, testKey, 16) == 0);
		CHECK(VmParseHex("9CD8 FCF2 2A47 333B 591D 96A2 B848 B73F", k, 16) && memcmp(k, testKey, 16) == 0);
		CHECK(!VmParseHex("9cd8fcf22a47333b591d96a2b848b73", k, 16));      /* one digit short */
		CHECK(!VmParseHex("9cd8fcf22a47333b591d96a2b848b73f00", k, 16));   /* too long */
		CHECK(!VmParseHex("9cd8fcf22a47333b591d96a2b848b73g", k, 16));     /* not hex */
	}

	CHECK(VmBuildSetChannel(out, sizeof(out), 2, (const UInt8 *)"#test", 5, testKey) == 3 + 2 + 32 + 16);
	CHECK(out[3] == 0x20 && out[4] == 2 && memcmp(out + 5, "#test", 5) == 0 && out[10] == 0);
	CHECK(memcmp(out + 5 + 32, testKey, 16) == 0);
	CHECK(VmBuildSetChannel(out, sizeof(out), 2, (const UInt8 *)"0123456789012345678901234567890123", 32,
	                        testKey) == 0);
}

static void test_contacts_and_rooms(void)
{
	UInt8 out[200], key[32], prefix[6];
	Boolean ok, admin;
	VmFrameDecoder d;
	VmMessage m;
	UInt16 i;
	static const UInt8 success[] = { 0x3e, 0x0e, 0x00, 0x85, 0x01, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 3, 1 };
	static const UInt8 fail[] = { 0x3e, 0x08, 0x00, 0x86, 0x00, 1, 2, 3, 4, 5, 6 };

	for (i = 0; i < 32; i++)
		key[i] = (UInt8)(0x40 + i);

	CHECK(VmBuildLogin(out, sizeof(out), key, (const UInt8 *)"hello", 5) == 3 + 1 + 32 + 5);
	CHECK(out[3] == 26 && out[4] == 0x40 && out[35] == 0x5f && memcmp(out + 36, "hello", 5) == 0);
	CHECK(VmBuildLogin(out, sizeof(out), key, (const UInt8 *)"", 0) == 3 + 1 + 32);      /* empty password */

	CHECK(VmBuildKeyCommand(out, sizeof(out), VM_CMD_REMOVE_CONTACT, key) == 3 + 33);
	CHECK(out[3] == 15 && out[4] == 0x40 && out[35] == 0x5f);
	CHECK(VmBuildKeyCommand(out, sizeof(out), VM_CMD_RESET_PATH, key) == 3 + 33 && out[3] == 13);
	CHECK(VmBuildKeyCommand(out, sizeof(out), VM_CMD_LOGOUT, key) == 3 + 33 && out[3] == 29);

	/* same layout as a CONTACT record (MyMesh::updateContactFromFrame), without the optional tail */
	CHECK(VmBuildAddContact(out, sizeof(out), key, VM_ADV_TYPE_ROOM, (const UInt8 *)"Room", 4) == 3 + 1 + 32 + 3 + 64 + 32 + 4);
	CHECK(out[3] == 9 && out[4 + 32] == 3 && out[4 + 33] == 0 && out[4 + 34] == 0xff);
	CHECK(memcmp(out + 4 + 35 + 64, "Room", 4) == 0 && out[4 + 35 + 64 + 4] == 0);
	CHECK(VmBuildAddContact(out, sizeof(out), key, 1, (const UInt8 *)"", 0) == 0);

	decode_one(&d, success, sizeof(success));
	CHECK(VmParseLoginResult(d.payload, d.len, &ok, &admin, prefix) && ok && admin && prefix[5] == 6);
	decode_one(&d, fail, sizeof(fail));
	CHECK(VmParseLoginResult(d.payload, d.len, &ok, &admin, prefix) && !ok && !admin && prefix[0] == 1);

	/* a signed message keeps its author's prefix (room posts) */
	decode_one(&d, kSignedMsg, sizeof(kSignedMsg));
	CHECK(VmParseMessage(d.payload, d.len, &m) && m.hasSigner && m.signer[0] == 0xa0 && m.signer[3] == 0xa3);
	decode_one(&d, kContactMsg, sizeof(kContactMsg));
	CHECK(VmParseMessage(d.payload, d.len, &m) && !m.hasSigner);
}

static void test_rx_log(void)
{
	VmRxLog rx;
	/* zero-hop flood advert: snr +6.25, rssi -92 */
	static const UInt8 advert0[] = { 0x88, 25, (UInt8)-92, (VM_PAYLOAD_ADVERT << 2) | 1, 0x40,
	                                 0x9b, 0x1e, 0x4f, 0x92, 0xf1, 0x37, 0, 0 };
	/* advert relayed by two repeaters, 2-byte hashes: aaaa then bbbb */
	static const UInt8 advert2[] = { 0x88, (UInt8)-8, (UInt8)-110, (VM_PAYLOAD_ADVERT << 2) | 1, 0x42,
	                                 0xaa, 0xaa, 0xbb, 0xbb, 0x9b, 0x1e, 0x4f, 0x92, 0xf1, 0x37 };
	/* text message with transport codes, flood, 1 hop of 1-byte hash cc */
	static const UInt8 text1[] = { 0x88, 4, (UInt8)-80, (2 << 2) | 0, 1, 2, 3, 4, 0x01, 0xcc, 9, 9, 9 };
	/* direct route: the path is the route ahead, so no 'from' */
	static const UInt8 direct[] = { 0x88, 4, (UInt8)-80, (2 << 2) | 2, 0x01, 0xdd, 9, 9 };

	CHECK(VmParseRxLog(advert0, sizeof(advert0), &rx) && rx.snrQ == 25 && rx.rssi == -92);
	CHECK(rx.isAdvert && rx.hops == 0 && rx.fromLen == 6 && rx.from[0] == 0x9b && rx.advertKey[5] == 0x37);
	CHECK(VmParseRxLog(advert2, sizeof(advert2), &rx) && rx.snrQ == -8 && rx.hops == 2);
	CHECK(rx.isAdvert && rx.advertKey[0] == 0x9b && rx.fromLen == 2 && rx.from[0] == 0xbb && rx.from[1] == 0xbb);
	CHECK(VmParseRxLog(text1, sizeof(text1), &rx) && !rx.isAdvert && rx.hops == 1 && rx.fromLen == 1 && rx.from[0] == 0xcc);
	CHECK(VmParseRxLog(direct, sizeof(direct), &rx) && rx.fromLen == 0);
	CHECK(!VmParseRxLog(advert2, 7, &rx));       /* path cut off */
}

static void test_remote(void)
{
	UInt8 out[200], key[32], prefix[6], st[8 + 56];
	VmRepeaterStats s;
	char line[64];
	const UInt8 *data;
	UInt16 dlen, pos, i;
	VmFrameDecoder d;
	/* ch1 voltage 4.12 V, ch2 temp -3.5 C, ch3 humidity 41.5%, ch4 GPS 39.9526,-75.1652 12.34m */
	static const UInt8 lpp[] = { 1, 116, 0x01, 0x9c, 2, 103, 0xff, 0xdd, 3, 104, 83,
	                             4, 136, 0x06, 0x18, 0xa6, 0xf4, 0x87, 0xdc, 0x00, 0x04, 0xd2, 5, 77, 1 };

	for (i = 0; i < 32; i++)
		key[i] = (UInt8)(0x40 + i);
	CHECK(VmBuildSendCli(out, sizeof(out), key, 0x12345678UL, (const UInt8 *)"get tx", 6) == 3 + 13 + 6);
	CHECK(out[3] == 2 && out[4] == 1 && out[5] == 0 && out[10] == 0x40 && memcmp(out + 16, "get tx", 6) == 0);
	CHECK(VmBuildStatusReq(out, sizeof(out), key) == 3 + 33 && out[3] == 27 && out[4] == 0x40);
	CHECK(VmBuildTelemetryReq(out, sizeof(out), key) == 3 + 36 && out[3] == 39 && out[4] == 0 && out[7] == 0x40);

	memset(st, 0, sizeof(st));
	st[0] = 0x87; st[2] = 0xaa; st[7] = 0xff;
	st[8] = 0x1c; st[9] = 0x10;                   /* 4124 mV */
	st[12] = 0x92; st[13] = 0xff;                 /* noise -110 */
	st[28] = 0x10; st[29] = 0x0e; st[30] = 0x04;  /* up 265744 s = 3d 1h */
	st[50] = 0xe8; st[51] = 0xff;                 /* snr -24/4 = -6.00 */
	st[16] = 0xd2; st[17] = 0x04;                 /* recv 1234 */
	CHECK(VmParseStatus(st, sizeof(st), prefix, &s) && prefix[0] == 0xaa && prefix[5] == 0xff);
	CHECK(s.battMv == 4124 && s.noiseFloor == -110 && s.upSecs == 265744UL && s.lastSnrQ == -24 && s.recv == 1234);
	VmFormatStatusLine(&s, 0, line);
	CHECK(strcmp(line, "Battery 4.124V, up 3d 1h, queue 0") == 0);
	VmFormatStatusLine(&s, 1, line);
	CHECK(strcmp(line, "Noise -110dBm, last 0dBm -6.00dB") == 0);
	VmFormatStatusLine(&s, 2, line);
	CHECK(strcmp(line, "Recv 1234 (flood 0, direct 0)") == 0);
	CHECK(!VmParseStatus(st, 40, prefix, &s));

	{
		UInt8 t[8 + sizeof(lpp)];

		t[0] = 0x8b; t[1] = 0;
		memcpy(t + 2, key, 6);
		memcpy(t + 8, lpp, sizeof(lpp));
		CHECK(VmParseTelemetry(t, sizeof(t), prefix, &data, &dlen) && dlen == sizeof(lpp) && prefix[0] == 0x40);
	}
	pos = 0;
	CHECK(VmLppNext(lpp, sizeof(lpp), &pos, line) && strcmp(line, "Ch1 Voltage 4.12V") == 0);
	CHECK(VmLppNext(lpp, sizeof(lpp), &pos, line) && strcmp(line, "Ch2 Temperature -3.5\xb0" "C") == 0);
	CHECK(VmLppNext(lpp, sizeof(lpp), &pos, line) && strcmp(line, "Ch3 Humidity 41.5%") == 0);
	CHECK(VmLppNext(lpp, sizeof(lpp), &pos, line) && strcmp(line, "Ch4 GPS 39.9526, -75.1652 12.34m") == 0);
	CHECK(VmLppNext(lpp, sizeof(lpp), &pos, line) && strcmp(line, "Ch5 type 77: not understood") == 0);
	CHECK(!VmLppNext(lpp, sizeof(lpp), &pos, line));
	(void)d;
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

/* Messaging, laid out per the MyMesh.cpp handlers (CMD_SEND_TXT_MSG etc.). */
static void test_messaging(void)
{
	UInt8 out[200];
	static const UInt8 prefix[6] = { 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5 };
	static const UInt8 dm[] = {
		0x3c, 0x0f, 0x00, 0x02, 0x00, 0x01, 0x28, 0x5a, 0xd6, 0x6a,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 'h', 'i'
	};
	static const UInt8 chan[] = { 0x3c, 0x09, 0x00, 0x03, 0x00, 0x01, 0x28, 0x5a, 0xd6, 0x6a, 'h', 'i' };
	static const UInt8 setTime[] = { 0x3c, 0x05, 0x00, 0x06, 0x28, 0x5a, 0xd6, 0x6a };
	static const UInt8 advert[] = { 0x3c, 0x02, 0x00, 0x07, 0x01 };
	static const UInt8 getChan[] = { 0x3c, 0x02, 0x00, 0x1f, 0x02 };
	UInt8 longText[VM_MAX_CHANNEL_TEXT + 1];
	VmFrameDecoder d;
	VmChannel ch;
	VmSent sent;
	UInt32 ack, trip, t;

	/* timestamp 1792432680 = 0x6ad65a28 */
	CHECK(VmBuildSendTxt(out, sizeof(out), prefix, 1792432680UL, 1, (const UInt8 *)"hi", 2) == sizeof(dm));
	CHECK(memcmp(out, dm, sizeof(dm)) == 0);
	CHECK(VmBuildSendChannelTxt(out, sizeof(out), 1, 1792432680UL, (const UInt8 *)"hi", 2) == sizeof(chan));
	CHECK(memcmp(out, chan, sizeof(chan)) == 0);
	memset(longText, 'x', sizeof(longText));
	CHECK(VmBuildSendChannelTxt(out, sizeof(out), 0, 0, longText, VM_MAX_CHANNEL_TEXT) != 0);
	CHECK(VmBuildSendChannelTxt(out, sizeof(out), 0, 0, longText, VM_MAX_CHANNEL_TEXT + 1) == 0);
	CHECK(VmBuildSendTxt(out, sizeof(out), prefix, 0, 0, longText, 0) == 0);
	CHECK(VmBuildSetTime(out, sizeof(out), 1792432680UL) == sizeof(setTime));
	CHECK(memcmp(out, setTime, sizeof(setTime)) == 0);
	CHECK(VmBuildSendAdvert(out, sizeof(out), 1) == sizeof(advert) && memcmp(out, advert, sizeof(advert)) == 0);
	CHECK(VmBuildGetChannel(out, sizeof(out), 2) == sizeof(getChan) && memcmp(out, getChan, sizeof(getChan)) == 0);

	decode_one(&d, kChannelInfo, sizeof(kChannelInfo));
	CHECK(VmParseChannelInfo(d.payload, d.len, &ch));
	CHECK(ch.idx == 1 && strcmp(ch.name, "#phillymesh") == 0 && ch.secret[0] == 0x11 && ch.secret[15] == 0x20);
	CHECK(!VmParseChannelInfo(d.payload, 49, &ch));

	decode_one(&d, kSent, sizeof(kSent));
	CHECK(VmParseSent(d.payload, d.len, &sent));
	CHECK(sent.flood && sent.expectedAck == 0xdeadbeefUL && sent.timeoutMs == 12000UL);

	decode_one(&d, kSendConfirmed, sizeof(kSendConfirmed));
	CHECK(VmParseSendConfirmed(d.payload, d.len, &ack, &trip) && ack == 0xdeadbeefUL && trip == 850UL);

	decode_one(&d, kCurrTime, sizeof(kCurrTime));
	CHECK(VmParseCurrTime(d.payload, d.len, &t) && t == 1792432680UL);
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
	test_sha256_and_channels();
	test_contacts_and_rooms();
	test_rx_log();
	test_remote();
	test_messaging();
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
