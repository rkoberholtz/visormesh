#include <PalmOS.h>
#include "../resources.h"
#include "draw.h"
#include "emoji.h"

static void WaitForTap(void) VM_SECT3;

#define ENTRY      56        /* code, name[23], 16 rows of 2 bytes */
#define NAME_SIZE  23
#define SCALE      2
#define BOX_W      104
#define BOX_H      70

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

Boolean EmojiShow(Char c)
{
	MemHandle h = DmGetResource('EMJI', EmojiArtRes);
	const UInt8 *p, *e = NULL;
	UInt8 bits[32];
	Char name[NAME_SIZE + 1];
	RectangleType box, save, px;
	WinHandle under;
	UInt16 i, err, row, col, rowBits;
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
	for (row = 0; row < 16; row++) {
		rowBits = (UInt16)((bits[2 * row] << 8) | bits[2 * row + 1]);
		for (col = 0; col < 16; col++) {
			if (!(rowBits & (0x8000 >> col)))
				continue;
			RctSetRectangle(&px, x0 + col * SCALE, y0 + row * SCALE, SCALE, SCALE);
			WinDrawRectangle(&px, 0);
		}
	}
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
