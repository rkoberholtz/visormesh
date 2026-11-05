#include <PalmOS.h>
#include "../resources.h"
#include "draw.h"
#include "glyphs.h"
#include "emoji.h"

static void WaitForTap(void) VM_SECT3;
static void DrawArt(const UInt8 *bits, Coord x0, Coord y0, Coord scale) VM_SECT3;
static void InvertCell(Coord x0, Coord y0, Int16 i) VM_SECT3;

#define ENTRY      56        /* code, name[23], 16 rows of 2 bytes */
#define NAME_SIZE  23
#define SCALE      2
#define BOX_W      104
#define BOX_H      70
#define PICK_COLS  4
#define PICK_CELL  22        /* a 16x16 picture and its margin */

/* Until a tap (down and up) or a key; the tap that opened the box is still queued. */
static void WaitForTap(void)
{
	EventType e;
	Boolean down = false;

	for (;;) {
		EvtGetEvent(&e, evtWaitForever);
		if (e.eType == appStopEvent) {
			EvtAddEventToQueue(&e);
			return;
		}
		if (e.eType == keyDownEvent)
			return;
		if (e.eType == penDownEvent)
			down = true;
		else if (e.eType == penUpEvent && down)
			return;
	}
}

Boolean EmojiName(Char c, Char *out)
{
	MemHandle h = DmGetResource('EMJI', EmojiArtRes);
	const UInt8 *p;
	UInt16 i;
	Boolean found = false;

	if (!h)
		return false;
	p = (const UInt8 *)MemHandleLock(h);
	for (i = 0; i < p[0] && !found; i++) {
		if (p[1 + i * ENTRY] == (UInt8)c) {
			MemMove(out, p + 2 + i * ENTRY, NAME_SIZE);
			out[NAME_SIZE] = 0;
			found = true;
		}
	}
	MemHandleUnlock(h);
	DmReleaseResource(h);
	return found;
}

/* A 16x16 picture (16 rows of 2 bytes, top bit leftmost), each pixel scale x scale. */
static void DrawArt(const UInt8 *bits, Coord x0, Coord y0, Coord scale)
{
	RectangleType px;
	UInt16 row, col, rowBits;

	for (row = 0; row < 16; row++) {
		rowBits = (UInt16)((bits[2 * row] << 8) | bits[2 * row + 1]);
		for (col = 0; col < 16; col++) {
			if (!(rowBits & (0x8000 >> col)))
				continue;
			RctSetRectangle(&px, x0 + col * scale, y0 + row * scale, scale, scale);
			WinDrawRectangle(&px, 0);
		}
	}
}

Boolean EmojiShow(Char c)
{
	MemHandle h = DmGetResource('EMJI', EmojiArtRes);
	const UInt8 *p, *e = NULL;
	UInt8 bits[32];
	Char name[NAME_SIZE + 1];
	RectangleType box, save;
	WinHandle under;
	UInt16 i, err;
	Coord x0, y0;
	FontID old;

	if (!h)
		return false;
	p = (const UInt8 *)MemHandleLock(h);
	for (i = 0; i < p[0]; i++)
		if (p[1 + i * ENTRY] == (UInt8)c)
			e = p + 1 + i * ENTRY;
	if (e) {
		MemMove(name, e + 1, NAME_SIZE);
		name[NAME_SIZE] = 0;
		MemMove(bits, e + 1 + NAME_SIZE, sizeof(bits));
	}
	MemHandleUnlock(h);
	DmReleaseResource(h);
	if (!e)
		return false;

	RctSetRectangle(&box, (160 - BOX_W) / 2, 40, BOX_W, BOX_H);
	RctSetRectangle(&save, box.topLeft.x - 3, box.topLeft.y - 3, BOX_W + 6, BOX_H + 6);
	under = WinSaveBits(&save, &err);
	WinEraseRectangle(&box, 0);
	WinDrawRectangleFrame(dialogFrame, &box);

	x0 = box.topLeft.x + (BOX_W - 16 * SCALE) / 2;
	y0 = box.topLeft.y + 4;
	DrawArt(bits, x0, y0, SCALE);
	old = FntSetFont(stdFont);
	WinDrawChars(name, StrLen(name), box.topLeft.x + (BOX_W - FntCharsWidth(name, StrLen(name))) / 2,
	             y0 + 16 * SCALE + 3);
	FntSetFont(vmSmallFont);
	WinDrawChars("tap to close", 12, box.topLeft.x + (BOX_W - FntCharsWidth("tap to close", 12)) / 2,
	             box.topLeft.y + BOX_H - gUiFontHeight - 3);
	FntSetFont(old);

	WaitForTap();
	if (under)
		WinRestoreBits(under, save.topLeft.x, save.topLeft.y);
	else
		FrmUpdateForm(FrmGetActiveFormID(), frmRedrawUpdateCode);
	return true;
}

static void InvertCell(Coord x0, Coord y0, Int16 i)
{
	RectangleType r;

	RctSetRectangle(&r, x0 + (i % PICK_COLS) * PICK_CELL, y0 + (i / PICK_COLS) * PICK_CELL, PICK_CELL, PICK_CELL);
	WinInvertRectangle(&r, 0);
}

/*
 * A box of the font's emoji over the screen (the generic smiley left out):
 * tap one to pick it, or outside the box or a key to cancel (0). The
 * picture under the pen is inverted while it's down.
 */
Char EmojiPick(void)
{
	MemHandle h = DmGetResource('EMJI', EmojiArtRes);
	const UInt8 *p, *e;
	RectangleType box, save;
	WinHandle under;
	EventType ev;
	UInt16 i, n = 0, count, rows, err;
	Int16 lit = -1, at;
	UInt16 k;
	Boolean active = false;
	Coord x0, y0, w, hgt;
	Char picked = 0, codes[16];
	FontID old;

	if (!h)
		return 0;
	p = (const UInt8 *)MemHandleLock(h);
	count = p[0];
	for (i = 0; i < count && n < sizeof(codes); i++)
		if (p[1 + i * ENTRY] != (UInt8)GLYPH_EMOJI)
			codes[n++] = (Char)p[1 + i * ENTRY];
	rows = (n + PICK_COLS - 1) / PICK_COLS;
	w = PICK_COLS * PICK_CELL + 8;
	hgt = rows * PICK_CELL + gUiFontHeight + 10;
	RctSetRectangle(&box, (160 - w) / 2, 30, w, hgt);
	RctSetRectangle(&save, box.topLeft.x - 3, box.topLeft.y - 3, w + 6, hgt + 6);
	under = WinSaveBits(&save, &err);
	WinEraseRectangle(&box, 0);
	WinDrawRectangleFrame(dialogFrame, &box);
	old = FntSetFont(vmSmallFont);
	WinDrawChars("React with", 10, box.topLeft.x + (w - FntCharsWidth("React with", 10)) / 2, box.topLeft.y + 3);
	FntSetFont(old);
	x0 = box.topLeft.x + 4;
	y0 = box.topLeft.y + gUiFontHeight + 6;
	for (i = 0; i < n; i++) {
		for (e = NULL, k = 0; k < count; k++)
			if (p[1 + k * ENTRY] == (UInt8)codes[i])
				e = p + 1 + k * ENTRY;
		if (e)
			DrawArt(e + 1 + NAME_SIZE, x0 + (i % PICK_COLS) * PICK_CELL + 3, y0 + (i / PICK_COLS) * PICK_CELL + 3, 1);
	}
	MemHandleUnlock(h);
	DmReleaseResource(h);

	for (;;) {
		EvtGetEvent(&ev, evtWaitForever);
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			break;
		}
		if (ev.eType == keyDownEvent)
			break;
		if (ev.eType == penDownEvent)
			active = true;
		/* the pen lifting from the hold that opened the box comes first: skip it */
		if (!active || (ev.eType != penDownEvent && ev.eType != penMoveEvent && ev.eType != penUpEvent))
			continue;
		at = -1;
		if (ev.screenX >= x0 && ev.screenX < x0 + PICK_COLS * PICK_CELL &&
		    ev.screenY >= y0 && ev.screenY < y0 + (Coord)rows * PICK_CELL) {
			at = (Int16)((ev.screenY - y0) / PICK_CELL * PICK_COLS + (ev.screenX - x0) / PICK_CELL);
			if (at >= (Int16)n)
				at = -1;
		}
		if (ev.eType == penUpEvent) {
			if (at >= 0)
				picked = codes[at];
			break;
		}
		if (at != lit) {
			if (lit >= 0)
				InvertCell(x0, y0, lit);
			if (at >= 0)
				InvertCell(x0, y0, at);
			lit = at;
		}
	}
	if (under)
		WinRestoreBits(under, save.topLeft.x, save.topLeft.y);
	else
		FrmUpdateForm(FrmGetActiveFormID(), frmRedrawUpdateCode);
	return picked;
}
