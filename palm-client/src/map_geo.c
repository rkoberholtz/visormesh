#include "map_geo.h"

#define HDR_FIXED  66      /* '>4sHHBBiiiiIHH32s' */
#define HDR_ZOOM   16      /* '>BBHHHii' */

static UInt16 Be16(const UInt8 *p) VM_SECT3;
static Int32 Be32(const UInt8 *p) VM_SECT3;
static Int32 Lookup(const UInt8 *table, UInt16 count, Int32 v0, UInt32 step, Int32 v, Boolean *ok) VM_SECT3;

static UInt16 Be16(const UInt8 *p)
{
	return (UInt16)((p[0] << 8) | p[1]);
}

static Int32 Be32(const UInt8 *p)
{
	return (Int32)(((UInt32)p[0] << 24) | ((UInt32)p[1] << 16) | ((UInt32)p[2] << 8) | p[3]);
}

Boolean VmMapParseHeader(const UInt8 *p, UInt32 len, VmMapHeader *h)
{
	UInt32 need;
	UInt16 i;
	const UInt8 *z;

	if (len < HDR_FIXED || p[0] != 'V' || p[1] != 'M' || p[2] != 'a' || p[3] != 'p' || Be16(p + 4) != 1)
		return 0;
	if (Be16(p + 6) != VM_MAP_TILE)
		return 0;
	h->bpp = p[8];
	h->zoomCount = p[9];
	if (h->zoomCount == 0 || h->zoomCount > VM_MAP_MAX_ZOOMS || (h->bpp != 1 && h->bpp != 2 && h->bpp != 4))
		return 0;
	h->latS = Be32(p + 10);
	h->lonW = Be32(p + 14);
	h->latN = Be32(p + 18);
	h->lonE = Be32(p + 22);
	h->step = (UInt32)Be32(p + 26);
	h->latCount = Be16(p + 30);
	h->lonCount = Be16(p + 32);
	for (i = 0; i < VM_MAP_ATTR_SIZE; i++)
		h->attribution[i] = (Char)p[34 + i];
	h->attribution[VM_MAP_ATTR_SIZE] = 0;
	need = HDR_FIXED + (UInt32)h->zoomCount * HDR_ZOOM + 4UL * (h->latCount + h->lonCount);
	if (len < need || h->step == 0 || h->latCount < 2 || h->lonCount < 2)
		return 0;
	for (i = 0; i < h->zoomCount; i++) {
		z = p + HDR_FIXED + i * HDR_ZOOM;
		h->zooms[i].z = z[0];
		h->zooms[i].cols = Be16(z + 2);
		h->zooms[i].rows = Be16(z + 4);
		h->zooms[i].first = Be16(z + 6);
		h->zooms[i].ox = Be32(z + 8);
		h->zooms[i].oy = Be32(z + 12);
	}
	h->latTable = p + HDR_FIXED + h->zoomCount * HDR_ZOOM;
	h->lonTable = h->latTable + 4 * h->latCount;
	return 1;
}

/* Linear interpolation in a table of values at v0, v0 + step, ... */
static Int32 Lookup(const UInt8 *table, UInt16 count, Int32 v0, UInt32 step, Int32 v, Boolean *ok)
{
	UInt32 d, i, frac;
	Int32 a, b;

	if (v < v0) {
		*ok = 0;
		return 0;
	}
	d = (UInt32)(v - v0);
	i = d / step;
	frac = d % step;
	if (i >= (UInt32)count - 1) {
		if (i == (UInt32)count - 1 && frac == 0)
			return Be32(table + 4 * i);
		*ok = 0;
		return 0;
	}
	a = Be32(table + 4 * i);
	b = Be32(table + 4 * (i + 1));
	/* (b - a) is a few hundred pixels at most and frac < step = 10000: no overflow */
	return a + (Int32)(((b - a) * (Int32)frac) / (Int32)step);
}

Boolean VmMapWorld(const VmMapHeader *h, Int32 latE6, Int32 lonE6, Int32 *wx, Int32 *wy)
{
	Boolean ok = 1;

	if (latE6 < h->latS || latE6 > h->latN || lonE6 < h->lonW || lonE6 > h->lonE)
		return 0;
	*wy = Lookup(h->latTable, h->latCount, h->latS, h->step, latE6, &ok);
	*wx = Lookup(h->lonTable, h->lonCount, h->lonW, h->step, lonE6, &ok);
	return ok;
}

void VmMapToZoom(const VmMapHeader *h, UInt8 zi, Int32 wx, Int32 wy, Int32 *x, Int32 *y)
{
	UInt8 shift = h->zooms[h->zoomCount - 1].z - h->zooms[zi].z;

	*x = (wx >> shift) - h->zooms[zi].ox;
	*y = (wy >> shift) - h->zooms[zi].oy;
}

UInt16 VmMapTileRecord(const VmMapHeader *h, UInt8 zi, Int32 col, Int32 row)
{
	const VmMapZoom *z = &h->zooms[zi];

	if (col < 0 || row < 0 || col >= (Int32)z->cols || row >= (Int32)z->rows)
		return 0;
	return (UInt16)(z->first + row * z->cols + col);
}

Boolean VmUnpackBits(const UInt8 *src, UInt32 srcLen, UInt8 *dst, UInt32 dstLen)
{
	UInt32 i = 0, o = 0, n;
	UInt8 c, v;

	while (o < dstLen) {
		if (i >= srcLen)
			return 0;
		c = src[i++];
		if (c < 128) {
			n = (UInt32)c + 1;
			if (i + n > srcLen || o + n > dstLen)
				return 0;
			while (n--)
				dst[o++] = src[i++];
		} else if (c > 128) {
			n = 257 - (UInt32)c;
			if (i >= srcLen || o + n > dstLen)
				return 0;
			v = src[i++];
			while (n--)
				dst[o++] = v;
		}
	}
	return 1;
}
