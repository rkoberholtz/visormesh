/*
 * The basemap database (tools/make_map.py writes it; the layout is in its
 * docstring): reading the header, placing a position on the map, and
 * unpacking tiles. Positions are Web Mercator pixels; the tool stores world y
 * and x at the max zoom every 0.01 degree across the map, and lower zooms are
 * that shifted right, so no floating point is needed here.
 *
 * Plain C with no Palm OS calls, so it also builds on the host for tests.
 */
#ifndef VM_MAP_GEO_H
#define VM_MAP_GEO_H

#include "protocol.h"

#define VM_MAP_TILE      160
#define VM_MAP_MAX_ZOOMS 8
#define VM_MAP_ATTR_SIZE 32

typedef struct {
	UInt8  z;
	UInt16 cols, rows;
	UInt16 first;          /* record of tile (0, 0) */
	Int32  ox, oy;         /* world pixels at z of tile (0, 0)'s top-left */
} VmMapZoom;

typedef struct {
	UInt8     bpp;
	UInt8     zoomCount;
	Int32     latS, lonW, latN, lonE;     /* degrees * 1e6 */
	UInt32    step;                       /* table step, degrees * 1e6 */
	UInt16    latCount, lonCount;
	Char      attribution[VM_MAP_ATTR_SIZE + 1];
	VmMapZoom zooms[VM_MAP_MAX_ZOOMS];    /* lowest zoom first */
	const UInt8 *latTable, *lonTable;     /* big-endian Int32s, in the header record */
} VmMapHeader;

/* The header record; the tables point into p, which must stay valid. */
Boolean VmMapParseHeader(const UInt8 *p, UInt32 len, VmMapHeader *h) VM_SECT3;

/* World pixels at the map's max zoom; false outside the map's area. */
Boolean VmMapWorld(const VmMapHeader *h, Int32 latE6, Int32 lonE6, Int32 *wx, Int32 *wy) VM_SECT3;

/* World pixels at the max zoom to pixels at zoom index zi, from the zoom's tile (0, 0). */
void    VmMapToZoom(const VmMapHeader *h, UInt8 zi, Int32 wx, Int32 wy, Int32 *x, Int32 *y) VM_SECT3;

/* The record holding tile (col, row) at zoom index zi, or 0 if outside the grid. */
UInt16  VmMapTileRecord(const VmMapHeader *h, UInt8 zi, Int32 col, Int32 row) VM_SECT3;

/* PackBits: false if src runs out or overflows dst before dstLen bytes. */
Boolean VmUnpackBits(const UInt8 *src, UInt32 srcLen, UInt8 *dst, UInt32 dstLen) VM_SECT3;

#endif
