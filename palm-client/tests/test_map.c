/* Host-side tests for the basemap reader (src/map_geo.c). Run with `make test`. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/map_geo.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* Same projection as tools/make_map.py world_xy. */
static void world(double lat, double lon, int z, double *x, double *y)
{
	double n = 256.0 * (1 << z), s = sin(lat * M_PI / 180.0);

	*x = (lon + 180.0) / 360.0 * n;
	*y = (0.5 - log((1 + s) / (1 - s)) / (4 * M_PI)) * n;
}

static UInt8 *put16(UInt8 *p, unsigned v) { p[0] = v >> 8; p[1] = v; return p + 2; }
static UInt8 *put32(UInt8 *p, long v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; return p + 4; }

/* The Philadelphia map's header, zooms 12 and 13, as the tool writes it. */
static UInt32 build_header(UInt8 *buf)
{
	const double S = 39.70, W = -75.49, N = 40.20, E = -74.83;
	const long step = 10000;
	int latn = 51, lonn = 67, i, z;
	UInt8 *p = buf;
	double x, y;

	memcpy(p, "VMap", 4); p += 4;
	p = put16(p, 1); p = put16(p, VM_MAP_TILE);
	*p++ = 2; *p++ = 2;
	p = put32(p, 39700000); p = put32(p, -75490000); p = put32(p, 40200000); p = put32(p, -74830000);
	p = put32(p, step); p = put16(p, latn); p = put16(p, lonn);
	memset(p, 0, 32); memcpy(p, "(c) OpenStreetMap contributors", 30); p += 32;
	for (z = 12; z <= 13; z++) {
		long ox, oy;
		world(N, W, z, &x, &y);
		ox = (long)(x / 160) * 160;
		oy = (long)(y / 160) * 160;
		*p++ = z; *p++ = 0;
		p = put16(p, 13); p = put16(p, 13); p = put16(p, z == 12 ? 1 : 170);
		p = put32(p, ox); p = put32(p, oy);
	}
	for (i = 0; i < latn; i++) {
		world(S + i * 0.01, W, 13, &x, &y);
		p = put32(p, lround(y));
	}
	for (i = 0; i < lonn; i++) {
		world(S, W + i * 0.01, 13, &x, &y);
		p = put32(p, lround(x));
	}
	(void)E;
	return (UInt32)(p - buf);
}

static void test_header(void)
{
	static UInt8 buf[1024];
	UInt32 len = build_header(buf);
	VmMapHeader h;

	CHECK(VmMapParseHeader(buf, len, &h));
	CHECK(h.bpp == 2 && h.zoomCount == 2);
	CHECK(h.latS == 39700000 && h.lonE == -74830000);
	CHECK(strcmp(h.attribution, "(c) OpenStreetMap contributors") == 0);
	CHECK(h.zooms[1].z == 13 && h.zooms[1].first == 170);
	CHECK(!VmMapParseHeader(buf, len - 1, &h));      /* tables cut short */
	buf[0] = 'X';
	CHECK(!VmMapParseHeader(buf, len, &h));
}

static void test_projection(void)
{
	static UInt8 buf[1024];
	UInt32 len = build_header(buf);
	VmMapHeader h;
	/* City Hall, a point between table rows, and the map's corners */
	static const double pts[][2] = {
		{ 39.952583, -75.165222 }, { 40.0137, -75.1803 }, { 39.70, -75.49 }, { 40.20, -74.83 }, { 39.8765, -74.9012 }
	};
	unsigned i;
	Int32 wx, wy, x, y;
	double ex, ey;

	VmMapParseHeader(buf, len, &h);
	for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
		CHECK(VmMapWorld(&h, (Int32)lround(pts[i][0] * 1e6), (Int32)lround(pts[i][1] * 1e6), &wx, &wy));
		world(pts[i][0], pts[i][1], 13, &ex, &ey);
		CHECK(fabs(wx - ex) <= 1.0 && fabs(wy - ey) <= 1.0);
		/* zoom 12 is half, from that zoom's tile origin */
		VmMapToZoom(&h, 0, wx, wy, &x, &y);
		world(pts[i][0], pts[i][1], 12, &ex, &ey);
		CHECK(fabs((x + h.zooms[0].ox) - ex) <= 1.0 && fabs((y + h.zooms[0].oy) - ey) <= 1.0);
	}
	CHECK(!VmMapWorld(&h, 39690000, -75165222, &wx, &wy));   /* south of the map */
	CHECK(!VmMapWorld(&h, 39952583, -74800000, &wx, &wy));   /* east of it */
	CHECK(!VmMapWorld(&h, 0, 0, &wx, &wy));                  /* "no position" */
}

static void test_tiles(void)
{
	static UInt8 buf[1024];
	VmMapHeader h;

	VmMapParseHeader(buf, build_header(buf), &h);
	CHECK(VmMapTileRecord(&h, 0, 0, 0) == 1);
	CHECK(VmMapTileRecord(&h, 0, 12, 12) == 1 + 12 * 13 + 12);
	CHECK(VmMapTileRecord(&h, 1, 2, 1) == 170 + 13 + 2);
	CHECK(VmMapTileRecord(&h, 0, 13, 0) == 0);
	CHECK(VmMapTileRecord(&h, 0, -1, 0) == 0);
}

static void test_unpack(void)
{
	/* literal 3, run of 5 x 0xAA, literal 1, a no-op 0x80, run of 2 x 0x00 */
	static const UInt8 src[] = { 2, 1, 2, 3, 0xFC, 0xAA, 0, 9, 0x80, 0xFF, 0 };
	static const UInt8 want[] = { 1, 2, 3, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 9, 0, 0 };
	UInt8 out[11];

	CHECK(VmUnpackBits(src, sizeof(src), out, sizeof(out)));
	CHECK(memcmp(out, want, sizeof(want)) == 0);
	CHECK(!VmUnpackBits(src, sizeof(src), out, 7));          /* the run of 5 crosses the end */
	CHECK(!VmUnpackBits(src, 5, out, sizeof(out)));          /* input ends early */
}

int main(void)
{
	test_header();
	test_projection();
	test_tiles();
	test_unpack();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("map: all tests passed\n");
	return 0;
}
