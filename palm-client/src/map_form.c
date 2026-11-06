#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "contacts.h"
#include "contact_form.h"
#include "session.h"
#include "map_geo.h"
#include "neigh.h"
#include "map_form.h"
#include "ui/draw.h"
#include "ui/text.h"

static Boolean OpenMap(void) VM_SECT4;
static void CloseMap(void) VM_SECT4;
static void LoadNodes(void) VM_SECT4;
static BitmapType *Tile(UInt16 rec) VM_SECT4;
static BitmapType *Enlarged(Int32 col, Int32 row) VM_SECT4;
static void ClampCenter(void) VM_SECT4;
static void Draw(void) VM_SECT4;
static void DrawNodes(const RectangleType *r, Int32 vx, Int32 vy) VM_SECT4;
static Int16 NodeByKey(const UInt8 *key) VM_SECT4;
static void DrawNeighbours(const RectangleType *r, Int32 vx, Int32 vy) VM_SECT4;
static void DrawInfo(const RectangleType *r) VM_SECT4;
static void DrawFooter(void) VM_SECT4;
static void Halo(const Char *s, UInt16 len, Coord x, Coord y) VM_SECT4;
static Char TypeGlyph(UInt8 type) VM_SECT4;
static Int16 NodeAt(Coord x, Coord y) VM_SECT4;
static void Zoom(Int16 by) VM_SECT4;
static void SaveView(void) VM_SECT4;
static Int32 FloorDiv(Int32 a, Int32 b) VM_SECT4;

#define MAP_DB_NAME     "VisorMeshMap"
#define MAP_PREFS_ID    5
#define MAP_PREFS_VER   1

#define HIDE_BASEMAP    0x01   /* plain white under the nodes, to see them better */
#define HIDE_LINES      0x02   /* the repeater neighbour lines */
#define MAX_NODES       150
#define CACHE_TILES     4
#define TAP_SLOP        4      /* pixels a pen can move and still be a tap */
#define HIT_RADIUS      8
#define NAME_LEN        16

typedef struct {
	Int32  wx, wy;             /* world pixels at the max zoom */
	UInt16 index;              /* contact record */
	UInt8  type;
	Boolean direct;            /* a 0-hop neighbour */
	UInt32 lastAdvert;
	UInt8  key[4];             /* for the neighbour lines (neigh.h) */
	Char   name[NAME_LEN + 1];
} MapNode;

typedef struct {
	UInt16 version;
	UInt8  zi;
	UInt8  hide;               /* HIDE_*; 0 in prefs saved before there were layers */
	Int32  cx, cy;
} MapPrefs;

static DmOpenRef   gDb = NULL;
static MemHandle   gHdrH = NULL;
static VmMapHeader gH;
static UInt32      gOldDepth = 0;
static Boolean     gDepthSet = false;

static struct { UInt16 rec; BitmapType *bmp; } gCache[CACHE_TILES];
static UInt16 gCacheNext;
static BitmapType *gBig = NULL;   /* Enlarged()'s tile */

static MapNode *gNodes = NULL;
static UInt16   gNodeCount;
static Int16    gSel = -1;

static UInt8  gZi;
static UInt8  gHide = 0;
static Int32  gCx, gCy;        /* view centre, world pixels at the max zoom */
static Coord  gDownX, gDownY;
static Boolean gPenDown;

static Int32 FloorDiv(Int32 a, Int32 b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static Char TypeGlyph(UInt8 type)
{
	switch (type) {
	case VM_ADV_TYPE_CHAT:     return GLYPH_COMPANION;
	case VM_ADV_TYPE_REPEATER: return GLYPH_REPEATER;
	case VM_ADV_TYPE_ROOM:     return GLYPH_ROOM;
	case VM_ADV_TYPE_SENSOR:   return GLYPH_SENSOR;
	default:                   return GLYPH_UNKNOWN;
	}
}

static Boolean OpenMap(void)
{
	LocalID id = DmFindDatabase(0, MAP_DB_NAME);
	MapPrefs prefs;
	UInt16 size = sizeof(prefs), i;
	Int32 x0, x1, y0, y1;

	if (!id || (gDb = DmOpenDatabase(0, id, dmModeReadOnly)) == NULL)
		return false;
	gHdrH = DmQueryRecord(gDb, 0);
	if (!gHdrH || !VmMapParseHeader((const UInt8 *)MemHandleLock(gHdrH), MemHandleSize(gHdrH), &gH)) {
		if (gHdrH)
			MemHandleUnlock(gHdrH);
		gHdrH = NULL;
		DmCloseDatabase(gDb);
		gDb = NULL;
		return false;
	}
	for (i = 0; i < CACHE_TILES; i++) {
		gCache[i].rec = 0;
		gCache[i].bmp = NULL;
	}
	gCacheNext = 0;

	/* the last view, or the whole map at its widest zoom */
	if (PrefGetAppPreferences(VM_CREATOR, MAP_PREFS_ID, &prefs, &size, true) != noPreferenceFound &&
	    size == sizeof(prefs) && prefs.version == MAP_PREFS_VER && prefs.zi < gH.zoomCount) {
		gZi = prefs.zi;
		gHide = prefs.hide;
		gCx = prefs.cx;
		gCy = prefs.cy;
	} else {
		VmMapWorld(&gH, gH.latS, gH.lonW, &x0, &y1);
		VmMapWorld(&gH, gH.latN, gH.lonE, &x1, &y0);
		gZi = 0;
		gCx = (x0 + x1) / 2;
		gCy = (y0 + y1) / 2;
	}
	ClampCenter();
	return true;
}

static void CloseMap(void)
{
	UInt16 i;

	for (i = 0; i < CACHE_TILES; i++)
		if (gCache[i].bmp)
			BmpDelete(gCache[i].bmp);
	if (gBig)
		BmpDelete(gBig);
	gBig = NULL;
	if (gHdrH)
		MemHandleUnlock(gHdrH);
	gHdrH = NULL;
	if (gDb)
		DmCloseDatabase(gDb);
	gDb = NULL;
	if (gNodes)
		MemPtrFree(gNodes);
	gNodes = NULL;
}

static void SaveView(void)
{
	MapPrefs prefs;

	prefs.version = MAP_PREFS_VER;
	prefs.zi = gZi;
	prefs.hide = gHide;
	prefs.cx = gCx;
	prefs.cy = gCy;
	PrefSetAppPreferences(VM_CREATOR, MAP_PREFS_ID, MAP_PREFS_VER, &prefs, sizeof(prefs), true);
}

/* Contacts with a position inside the map; the rest aren't shown at all. */
static void LoadNodes(void)
{
	UInt16 count, i;
	VmContact c;
	MapNode *n;

	gNodeCount = 0;
	gSel = -1;
	gNodes = (MapNode *)MemPtrNew(MAX_NODES * sizeof(MapNode));
	if (!gNodes)
		return;
	ContactsOpen();
	count = ContactsCount();
	for (i = 0; i < count && gNodeCount < MAX_NODES; i++) {
		if (!ContactsGet(i, &c) || (c.latE6 == 0 && c.lonE6 == 0))
			continue;
		n = &gNodes[gNodeCount];
		if (!VmMapWorld(&gH, c.latE6, c.lonE6, &n->wx, &n->wy))
			continue;
		n->index = i;
		n->type = c.type;
		n->direct = c.outPathLen != 0xFF && (c.outPathLen & 0x3F) == 0;
		n->lastAdvert = c.lastAdvert;
		MemMove(n->key, c.pubKey, 4);
		StrNCopy(n->name, c.name, NAME_LEN);
		n->name[NAME_LEN] = 0;
		gNodeCount++;
	}
}

/* A decoded tile, from a small cache; NULL for a blank one or on error. */
static BitmapType *Tile(UInt16 rec)
{
	MemHandle h;
	const UInt8 *src;
	BitmapType *bmp;
	UInt16 i, err;
	UInt32 bytes = (UInt32)VM_MAP_TILE * VM_MAP_TILE * gH.bpp / 8;
	Boolean ok;

	if (rec == 0)
		return NULL;                       /* outside the zoom's area (record 0 is the header) */
	for (i = 0; i < CACHE_TILES; i++)
		if (gCache[i].rec == rec)
			return gCache[i].bmp;
	h = DmQueryRecord(gDb, rec);
	if (!h || MemHandleSize(h) <= 1)
		return NULL;                       /* an all-white tile */
	i = gCacheNext;
	gCacheNext = (gCacheNext + 1) % CACHE_TILES;
	if (!gCache[i].bmp) {
		gCache[i].bmp = BmpCreate(VM_MAP_TILE, VM_MAP_TILE, gH.bpp, NULL, &err);
		if (!gCache[i].bmp)
			return NULL;
	}
	bmp = gCache[i].bmp;
	src = (const UInt8 *)MemHandleLock(h);
	ok = VmUnpackBits(src, MemHandleSize(h), (UInt8 *)BmpGetBits(bmp), bytes);
	MemHandleUnlock(h);
	gCache[i].rec = ok ? rec : 0;
	return ok ? bmp : NULL;
}

/*
 * Where this zoom has no tile (outside its area), the zoom below's pixels
 * doubled: blockier, but not blank. Rows are built from the tiles they
 * cross, and a row with the same source row as the one above is copied.
 */
static BitmapType *Enlarged(Int32 col, Int32 row)
{
	const VmMapZoom *z = &gH.zooms[gZi], *p;
	UInt8 b = gH.bpp, mask = (UInt8)((1 << b) - 1), *dst, *line, *from = NULL;
	UInt16 rowBytes = VM_MAP_TILE * b / 8, x, y, px, py, err;
	Int32 tx, ty, sx, sy, c, pc = 0, pr;
	BitmapType *src;

	if (gZi == 0 || z->z != gH.zooms[gZi - 1].z + 1)
		return NULL;
	p = &gH.zooms[gZi - 1];
	if (!gBig && (gBig = BmpCreate(VM_MAP_TILE, VM_MAP_TILE, b, NULL, &err)) == NULL)
		return NULL;
	dst = (UInt8 *)BmpGetBits(gBig);
	tx = z->ox + col * VM_MAP_TILE;
	ty = z->oy + row * VM_MAP_TILE;
	for (y = 0; y < VM_MAP_TILE; y++) {
		line = dst + y * rowBytes;
		if (y > 0 && ((ty + y) >> 1) == ((ty + y - 1) >> 1)) {
			MemMove(line, line - rowBytes, rowBytes);
			continue;
		}
		MemSet(line, rowBytes, 0);
		sy = ((ty + y) >> 1) - p->oy;
		pr = FloorDiv(sy, VM_MAP_TILE);
		py = (UInt16)(sy - pr * VM_MAP_TILE);
		for (x = 0; x < VM_MAP_TILE; x++) {
			sx = ((tx + x) >> 1) - p->ox;
			c = FloorDiv(sx, VM_MAP_TILE);
			if (x == 0 || c != pc) {
				pc = c;
				src = Tile(VmMapTileRecord(&gH, gZi - 1, c, pr));
				from = src ? (UInt8 *)BmpGetBits(src) + py * rowBytes : NULL;
			}
			if (!from)
				continue;
			px = (UInt16)(sx - c * VM_MAP_TILE);
			line[(x * b) >> 3] |= (UInt8)(((from[(px * b) >> 3] >> (8 - b - ((px * b) & 7))) & mask)
			                                << (8 - b - ((x * b) & 7)));
		}
	}
	return gBig;
}

static void ClampCenter(void)
{
	Int32 x0, x1, y0, y1;

	VmMapWorld(&gH, gH.latS, gH.lonW, &x0, &y1);
	VmMapWorld(&gH, gH.latN, gH.lonE, &x1, &y0);
	if (gCx < x0) gCx = x0;
	if (gCx > x1) gCx = x1;
	if (gCy < y0) gCy = y0;
	if (gCy > y1) gCy = y1;
}

/* Text with a one-pixel white outline, so it reads over roads. */
static void Halo(const Char *s, UInt16 len, Coord x, Coord y)
{
	RGBColorType white = { 0, 255, 255, 255 }, black = { 0, 0, 0, 0 };

	WinSetDrawMode(winOverlay);
	WinSetTextColor(WinRGBToIndex(&white));
	WinPaintChars(s, len, x - 1, y);
	WinPaintChars(s, len, x + 1, y);
	WinPaintChars(s, len, x, y - 1);
	WinPaintChars(s, len, x, y + 1);
	WinSetTextColor(WinRGBToIndex(&black));
	WinPaintChars(s, len, x, y);
}

static Int16 NodeByKey(const UInt8 *key)
{
	UInt16 i;

	for (i = 0; i < gNodeCount; i++)
		if (MemCmp(gNodes[i].key, key, 4) == 0)
			return (Int16)i;
	return -1;
}

/*
 * The last neighbour list a repeater's console showed: a line to each
 * neighbour with a position, solid if the repeater hears it well (SNR 0 dB
 * or better), grey if not. Draw has set the clip to the map.
 */
static void DrawNeighbours(const RectangleType *r, Int32 vx, Int32 vy)
{
	VmNeighbour list[VM_NEIGH_MAX];
	UInt8 rep[6];
	UInt16 n = NeighGet(rep, list), i;
	Int16 from, to;
	Int32 x0, y0, x1, y1;

	if (n == 0 || (from = NodeByKey(rep)) < 0)
		return;
	VmMapToZoom(&gH, gZi, gNodes[from].wx, gNodes[from].wy, &x0, &y0);
	x0 += r->topLeft.x - vx;
	y0 += r->topLeft.y - vy;
	for (i = 0; i < n; i++) {
		if ((to = NodeByKey(list[i].prefix)) < 0)
			continue;
		VmMapToZoom(&gH, gZi, gNodes[to].wx, gNodes[to].wy, &x1, &y1);
		x1 += r->topLeft.x - vx;
		y1 += r->topLeft.y - vy;
		/* far off-screen ends would overflow Coord */
		if (x1 < -2000 || x1 > 2000 || y1 < -2000 || y1 > 2000 || x0 < -2000 || x0 > 2000 || y0 < -2000 || y0 > 2000)
			continue;
		if (list[i].snrQ >= 0)
			WinDrawLine((Coord)x0, (Coord)y0, (Coord)x1, (Coord)y1);
		else
			WinDrawGrayLine((Coord)x0, (Coord)y0, (Coord)x1, (Coord)y1);
	}
}

static void DrawNodes(const RectangleType *r, Int32 vx, Int32 vy)
{
	RectangleType labels[MAX_NODES / 4];
	UInt16 nLabels = 0, i, j, len;
	Int32 x, y;
	Coord sx, sy, w;
	Char g;
	Boolean clear, named = gZi + 2 >= gH.zoomCount;   /* names at the two closest zooms */
	MapNode *n;

	FntSetFont(vmSmallFont);
	for (i = 0; i < gNodeCount; i++) {
		n = &gNodes[i];
		VmMapToZoom(&gH, gZi, n->wx, n->wy, &x, &y);
		x -= vx;
		y -= vy;
		if (x < -4 || y < -4 || x >= r->extent.x + 4 || y >= r->extent.y + 4)
			continue;
		sx = r->topLeft.x + (Coord)x;
		sy = r->topLeft.y + (Coord)y;
		g = TypeGlyph(n->type);
		w = FntCharWidth(g);
		Halo(&g, 1, sx - w / 2, sy - gUiFontHeight / 2);
		if (n->direct)                                  /* bold: heard directly */
			WinPaintChars(&g, 1, sx - w / 2 + 1, sy - gUiFontHeight / 2);
		if (!named && (Int16)i != gSel)
			continue;
		len = StrLen(n->name);
		labels[nLabels].topLeft.x = sx + w / 2 + 2;
		labels[nLabels].topLeft.y = sy - gUiFontHeight / 2;
		labels[nLabels].extent.x = FntCharsWidth(n->name, len);
		labels[nLabels].extent.y = gUiFontHeight;
		clear = true;
		for (j = 0; j < nLabels && clear; j++) {
			RectangleType o;

			RctGetIntersection(&labels[j], &labels[nLabels], &o);
			clear = o.extent.x <= 0 || o.extent.y <= 0;
		}
		if (!clear && (Int16)i != gSel)
			continue;
		Halo(n->name, len, labels[nLabels].topLeft.x, labels[nLabels].topLeft.y);
		if (nLabels < sizeof(labels) / sizeof(labels[0]) - 1)
			nLabels++;
	}
}

/* The selected node: a bar along the bottom of the map. */
static void DrawInfo(const RectangleType *r)
{
	RectangleType bar;
	MapNode *n;
	/* glyph, name, age (up to 4) and the hint: 46 at most */
	Char line[NAME_LEN + 48], age[12];
	UInt32 now;

	if (gSel < 0)
		return;
	n = &gNodes[gSel];
	bar.topLeft.x = r->topLeft.x;
	bar.topLeft.y = r->topLeft.y + r->extent.y - gUiFontHeight - 2;
	bar.extent.x = r->extent.x;
	bar.extent.y = gUiFontHeight + 2;
	if (n->lastAdvert) {
		now = SessionNow();
		TxtFormatAge(age, now > n->lastAdvert ? now - n->lastAdvert : 0);
	} else {
		StrCopy(age, "-");
	}
	StrPrintF(line, "%c %s  %s  (tap again to open)", TypeGlyph(n->type), n->name, age);
	WinEraseRectangle(&bar, 0);
	FntSetFont(vmSmallFont);
	WinSetDrawMode(winPaint);
	DrawCut(line, bar.topLeft.x + 2, bar.topLeft.y + 1, bar.extent.x - 4);
	WinInvertRectangle(&bar, 0);
}

/* The attribution, right of the buttons (which end at x 63): cut if it doesn't fit. */
#define FOOTER_X 66
static void DrawFooter(void)
{
	FontID old = FntSetFont(vmSmallFont);
	Coord room = 159 - FOOTER_X;

	if (FntCharsWidth(gH.attribution, StrLen(gH.attribution)) > room)
		DrawCut(gH.attribution, FOOTER_X, 150, room);
	else
		DrawRight(gH.attribution, 159, 150);
	FntSetFont(old);
}

static void Draw(void)
{
	FormType *form = FrmGetActiveForm();
	RectangleType r;
	Int32 cx, cy, vx, vy, col, row, col0, col1, row0, row1;
	UInt16 rec;
	BitmapType *bmp;
	RectangleType t;

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, MapGadget), &r);
	WinPushDrawState();
	WinSetClip(&r);
	VmMapToZoom(&gH, gZi, gCx, gCy, &cx, &cy);
	vx = cx - r.extent.x / 2;
	vy = cy - r.extent.y / 2;
	col0 = FloorDiv(vx, VM_MAP_TILE);
	col1 = FloorDiv(vx + r.extent.x - 1, VM_MAP_TILE);
	row0 = FloorDiv(vy, VM_MAP_TILE);
	row1 = FloorDiv(vy + r.extent.y - 1, VM_MAP_TILE);
	for (row = row0; row <= row1; row++) {
		for (col = col0; col <= col1; col++) {
			t.topLeft.x = r.topLeft.x + (Coord)(col * VM_MAP_TILE - vx);
			t.topLeft.y = r.topLeft.y + (Coord)(row * VM_MAP_TILE - vy);
			t.extent.x = t.extent.y = VM_MAP_TILE;
			rec = VmMapTileRecord(&gH, gZi, col, row);
			if (gHide & HIDE_BASEMAP)
				bmp = NULL;
			else
				bmp = rec ? Tile(rec) : Enlarged(col, row);
			if (bmp)
				WinDrawBitmap(bmp, t.topLeft.x, t.topLeft.y);
			else
				WinEraseRectangle(&t, 0);
		}
	}
	if (!(gHide & HIDE_LINES))
		DrawNeighbours(&r, vx, vy);
	DrawNodes(&r, vx, vy);
	DrawInfo(&r);
	WinResetClip();
	WinPopDrawState();
}

static Int16 NodeAt(Coord px, Coord py)
{
	FormType *form = FrmGetActiveForm();
	RectangleType r;
	Int32 cx, cy, x, y, dx, dy, best = (Int32)HIT_RADIUS * HIT_RADIUS + 1;
	Int16 hit = -1;
	UInt16 i;

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, MapGadget), &r);
	VmMapToZoom(&gH, gZi, gCx, gCy, &cx, &cy);
	for (i = 0; i < gNodeCount; i++) {
		VmMapToZoom(&gH, gZi, gNodes[i].wx, gNodes[i].wy, &x, &y);
		dx = r.topLeft.x + (x - (cx - r.extent.x / 2)) - px;
		dy = r.topLeft.y + (y - (cy - r.extent.y / 2)) - py;
		if (dx * dx + dy * dy < best) {
			best = dx * dx + dy * dy;
			hit = (Int16)i;
		}
	}
	return hit;
}

static void Zoom(Int16 by)
{
	Int16 zi = (Int16)gZi + by;

	if (zi < 0 || zi >= gH.zoomCount)
		return;
	gZi = (UInt8)zi;
	Draw();
}

Boolean MapFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	RectangleType r;
	UInt32 depths, want;
	Coord dx, dy;
	Int16 hit;
	VmContact c;

	switch (event->eType) {
	case frmOpenEvent:
		if (OpenMap()) {
			/* the tiles' grey depth, if the screen can do it */
			WinScreenMode(winScreenModeGet, NULL, NULL, &gOldDepth, NULL);
			want = gH.bpp;
			if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL, &depths, NULL) == errNone &&
			    (depths & (1UL << (want - 1))) && want != gOldDepth &&
			    WinScreenMode(winScreenModeSet, NULL, NULL, &want, NULL) == errNone)
				gDepthSet = true;
			LoadNodes();
			FrmDrawForm(form);
			DrawFooter();
			Draw();
		} else {
			FrmDrawForm(form);
			FrmCustomAlert(MapAlert, "No map installed. Build one with palm-client/tools/make_map.py "
			               "and HotSync build/VisorMeshMap.pdb.", "", "");
		}
		return true;
	case frmCloseEvent:
		if (gDb)
			SaveView();
		CloseMap();
		if (gDepthSet)
			WinScreenMode(winScreenModeSet, NULL, NULL, &gOldDepth, NULL);
		gDepthSet = false;
		break;                           /* let the form close normally */
	case frmUpdateEvent:
		FrmDrawForm(form);
		if (gDb) {
			DrawFooter();
			Draw();
		}
		return true;
	case penDownEvent:
		if (!gDb)
			break;
		FrmGetObjectBounds(form, FrmGetObjectIndex(form, MapGadget), &r);
		if (!RctPtInRectangle(event->screenX, event->screenY, &r))
			break;
		gPenDown = true;
		gDownX = event->screenX;
		gDownY = event->screenY;
		return true;
	case penUpEvent:
		if (!gPenDown)
			break;
		gPenDown = false;
		dx = event->screenX - gDownX;
		dy = event->screenY - gDownY;
		if (dx > TAP_SLOP || dx < -TAP_SLOP || dy > TAP_SLOP || dy < -TAP_SLOP) {
			UInt8 shift = gH.zooms[gH.zoomCount - 1].z - gH.zooms[gZi].z;

			gCx -= (Int32)dx << shift;   /* drag the map along with the pen */
			gCy -= (Int32)dy << shift;
			ClampCenter();
		} else {
			hit = NodeAt(gDownX, gDownY);
			if (hit >= 0 && hit == gSel && ContactsGet(gNodes[hit].index, &c)) {
				ContactFormOpen(&c);
				return true;
			}
			gSel = hit;
		}
		Draw();
		return true;
	case keyDownEvent:
		if (!gDb)
			break;
		if (event->data.keyDown.chr == pageUpChr) {
			Zoom(1);
			return true;
		}
		if (event->data.keyDown.chr == pageDownChr) {
			Zoom(-1);
			return true;
		}
		break;
	case menuEvent:
		if (event->data.menu.itemID == MainMenuBasemap || event->data.menu.itemID == MainMenuNeighLines) {
			gHide ^= event->data.menu.itemID == MainMenuBasemap ? HIDE_BASEMAP : HIDE_LINES;
			if (gDb) {
				SaveView();
				Draw();
			}
			return true;
		}
		break;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case MapInButton:   if (gDb) Zoom(1);  return true;
		case MapOutButton:  if (gDb) Zoom(-1); return true;
		case MapDoneButton: FrmGotoForm(HomeForm); return true;
		}
		break;
	default:
		break;
	}
	return false;
}
