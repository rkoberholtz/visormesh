/* Host-side tests for the companion framing. Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include "../src/protocol.h"

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

/* DEVICE_INFO captured from the SB-LoRa XIAO (MeshCore v1.17.1) with firmware/tools/companion_query.py. */
static void test_decode_real_device_info(void)
{
	static const UInt8 in[] = {
		0x3e, 0x52, 0x00, 0x0d, 0x0d, 0xaf, 0x28, 0x00, 0x00, 0x00, 0x00, 0x31,
		0x34, 0x20, 0x41, 0x75, 0x67, 0x20, 0x32, 0x30, 0x32, 0x36, 0x00, 0x53,
		0x65, 0x65, 0x65, 0x64, 0x20, 0x58, 0x69, 0x61, 0x6f, 0x2d, 0x6e, 0x72,
		0x66, 0x35, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x31, 0x2e, 0x31, 0x37, 0x2e, 0x31,
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
	CHECK(memcmp(&d.payload[62], "v1.17.1", 7) == 0);
	CHECK(sizeof(in) == VM_HEADER_LEN + 0x52);
}

int main(void)
{
	test_encode_device_query();
	test_decode_with_leading_junk();
	test_decode_back_to_back();
	test_decode_oversize_resyncs();
	test_decode_max_frame();
	test_decode_real_device_info();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all protocol tests passed\n");
	return 0;
}
