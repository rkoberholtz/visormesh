#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "ui/draw.h"
#include "ui/title.h"

#define TITLE_PAD 8      /* the tab's width past its text */
#define ELLIPSIS  ((Char)0x85)   /* chrHorizontalEllipsis (3.1 and later ROMs) */

static Coord gLeft = 160;

void TitleStatusReset(void)
{
	gLeft = 160;
}

/*
 * Erases back to where the previous text started, so a shorter one doesn't
 * leave the start of a longer one behind ("Connecting..." is 65 px in the
 * large font). Rows 2-10: above the title line, so the large font draws a row
 * higher.
 */
void TitleStatus(const Char *text)
{
	RectangleType r;
	FontID old = FntSetFont(vmSmallFont);
	Coord left = 158 - FntCharsWidth(text, StrLen(text));

	if (left < gLeft)
		gLeft = left;
	RctSetRectangle(&r, gLeft - 1, 2, 160 - (gLeft - 1), 9);
	WinEraseRectangle(&r, 0);
	DrawRight(text, 158, gUiFontHeight > 6 ? 3 : 4);
	gLeft = left;
	FntSetFont(old);
}

void TitleConnState(const Char *prefix)
{
	Char buf[24];

	StrCopy(buf, prefix);
	StrCat(buf, SessionIsConnected() ? "online" : SessionConnecting() ? "connecting" : "offline");
	TitleStatus(buf);
}

Coord TitleStatusLeft(void)
{
	return gLeft;
}

Boolean TitleStatusTapped(const EventType *event)
{
	EventType e;

	if (event->eType != penDownEvent || SessionIsConnected() || SessionConnecting() ||
	    event->screenY > 12 || event->screenX < gLeft - 4)
		return false;
	MemSet(&e, sizeof(e), 0);
	e.eType = menuEvent;
	e.data.menu.itemID = MainMenuConnect;
	EvtAddEventToQueue(&e);
	return true;
}

Coord TitleFit(Char *title)
{
	FontID old = FntSetFont(vmSmallFont);
	Int16 maxW = 158 - FntCharsWidth("connecting", 10) - 4 - TITLE_PAD;
	Int16 w = maxW, len = StrLen(title);
	Boolean fits;

	FntSetFont(boldFont);
	if (FntCharsWidth(title, len) > maxW) {
		w = maxW - FntCharWidth(ELLIPSIS);
		FntCharsInWidth(title, &w, &len, &fits);
		title[len++] = ELLIPSIS;
		title[len] = 0;
	}
	w = FntCharsWidth(title, len) + TITLE_PAD;
	FntSetFont(old);
	return w;
}
