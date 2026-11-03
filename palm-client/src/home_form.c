/*
 * Home: the conversation list, MeshTerm's chat picker. Channels in slot
 * order, then direct conversations (most recent first). Each row: marker,
 * name, unread badge or failed mark, age, preview. Tap to open; tap and hold
 * for a channel's actions (mute, scope, leave) or a contact's (info, reset
 * path, remove).
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "convos.h"
#include "app.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "home_form.h"
#include "mutes.h"
#include "scopes.h"
#include "channel_form.h"
#include "contacts.h"
#include "contact_form.h"

/* In the second code segment: see sections.h for the rules. */
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;
static Boolean Selectable(UInt16 row, void *ctx) VM_SECT2;
static void DrawTitleStatus(void) VM_SECT2;
static void ShowStatus(const Char *text) VM_SECT2;
static void TitleRight(const Char *text) VM_SECT2;
static void Rebuild(Boolean draw) VM_SECT2;
static Int16 PopupActions(const RectangleType *b, Char **items) VM_SECT2;
static Boolean HoldChannel(const VmConvRow *r, const RectangleType *b) VM_SECT2;
static Boolean HoldDirect(const VmConvRow *r, const RectangleType *b) VM_SECT2;
static Boolean Hold(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;

#define HOME_MAX_ROWS 64
#define NAME_X     8
#define NAME_W     56
#define BADGE_R    84
#define AGE_R      100
#define PREVIEW_X  103

static VmConvRow *gRows = NULL;
static UInt16     gCount = 0;
static RowList    gList;

static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	const VmConvRow *r = &gRows[row];
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x, y = b->topLeft.y, right = x + b->extent.x;
	Char buf[12];

	if (r->kind == VM_ROW_SECTION) {
		FntSetFont(old);
		DrawSection(r->name, x, y, b->extent.x);
		return;
	}
	WinDrawChars(&r->marker, 1, x + 1, y);
	DrawCut(r->name, x + NAME_X, y, NAME_W);
	if (r->failed) {
		buf[0] = GLYPH_FAILED;
		buf[1] = 0;
		DrawRight(buf, x + BADGE_R, y);
	} else if (r->unread) {
		/* open circle: a muted channel's unread */
		StrPrintF(buf, "%c%u", r->muted ? GLYPH_UNKNOWN : GLYPH_COMPANION, r->unread);
		DrawRight(buf, x + BADGE_R, y);
	}
	if (r->last) {
		UInt32 now = SessionNow();

		TxtFormatAge(buf, now > r->last ? now - r->last : 0);
		DrawRight(buf, x + AGE_R, y);
		DrawCut(r->preview, x + PREVIEW_X, y, right - (x + PREVIEW_X));
	}
	FntSetFont(old);
}

#define HOLD_ITEMS   3
#define HOLD_LIST_X  28
#define HOLD_LIST_H  (HOLD_ITEMS * 11 + 2)   /* stdFont rows and the frame */

/* Shows the popup beside the row (below it, or above near the bottom of the list); returns the pick. */
static Int16 PopupActions(const RectangleType *b, Char **items)
{
	FormType *form = FrmGetActiveForm();
	ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, HomeActionList));
	RectangleType area;
	Coord y = b->topLeft.y + b->extent.y;

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, HomeGadget), &area);
	if (y + HOLD_LIST_H > area.topLeft.y + area.extent.y)
		y = b->topLeft.y - HOLD_LIST_H;
	LstSetListChoices(list, items, HOLD_ITEMS);
	LstSetHeight(list, HOLD_ITEMS);
	LstSetPosition(list, HOLD_LIST_X, y);
	LstSetSelection(list, noListSelection);
	return LstPopupList(list);
}

static Boolean HoldChannel(const VmConvRow *r, const RectangleType *b)
{
	Char *items[HOLD_ITEMS];
	UInt8 slot = r->conv.channel;

	items[0] = r->muted ? "Unmute" : "Mute";
	items[1] = "Channel Scope...";
	items[2] = "Leave Channel";
	switch (PopupActions(b, items)) {
	case 0:
		ChannelMuteToggle(slot);
		Rebuild(true);
		break;
	case 1:
		ChannelScopeChoose(slot);
		break;
	case 2:
		ChannelLeave(slot);
		break;
	}
	return true;
}

/* A sender we have no contact for has nothing to act on: that hold is a tap. */
static Boolean HoldDirect(const VmConvRow *r, const RectangleType *b)
{
	Char *items[HOLD_ITEMS];
	VmContact c;

	if (!ContactsFindByPrefix(r->conv.key, &c))
		return false;
	items[0] = "Contact Info";
	items[1] = "Reset Path";
	items[2] = "Remove Contact";
	switch (PopupActions(b, items)) {
	case 0:
		ContactFormOpen(&c);
		break;
	case 1:
		ContactResetPath(&c);
		break;
	case 2:
		if (ContactRemove(&c))
			Rebuild(true);
		break;
	}
	return true;
}

static Boolean Hold(UInt16 row, const RectangleType *b, void *ctx)
{
	const VmConvRow *r = &gRows[row];

	if (r->kind != VM_ROW_CONV)
		return false;
	return r->conv.type == VM_CONV_CHANNEL ? HoldChannel(r, b) : HoldDirect(r, b);
}

static Boolean Selectable(UInt16 row, void *ctx)
{
	return gRows[row].kind == VM_ROW_CONV;
}

/* Right end of the title bar: unread total and connection state. */
/*
 * Text at the right end of the title bar. Erases back to where the previous
 * text started, so a shorter one doesn't leave the start of a longer one
 * behind ("Connecting..." is 65 px in the large font). Rows 2-10: above the
 * title line, so the large font draws a row higher.
 */
static Coord gTitleLeft = 160;

static void TitleRight(const Char *text)
{
	RectangleType r;
	FontID old = FntSetFont(vmSmallFont);
	Coord left = 158 - FntCharsWidth(text, StrLen(text));

	if (left < gTitleLeft)
		gTitleLeft = left;
	RctSetRectangle(&r, gTitleLeft - 1, 2, 160 - (gTitleLeft - 1), 9);
	WinEraseRectangle(&r, 0);
	DrawRight(text, 158, gUiFontHeight > 6 ? 3 : 4);
	gTitleLeft = left;
	FntSetFont(old);
}

static void DrawTitleStatus(void)
{
	Char buf[24];
	UInt16 unread = ConvosUnread(gRows, gCount);

	if (unread)
		StrPrintF(buf, "%c%u %s", GLYPH_COMPANION, unread, SessionIsConnected() ? "online" : "offline");
	else
		StrCopy(buf, SessionIsConnected() ? "online" : "offline");
	TitleRight(buf);
}

static void ShowStatus(const Char *text)
{
	TitleRight(text);
}

static void Rebuild(Boolean draw)
{
	gCount = ConvosBuild(gRows, HOME_MAX_ROWS);
	RowListSetCount(&gList, gCount);
	if (draw) {
		RowListDraw(&gList);
		DrawTitleStatus();
	}
}

Boolean HomeFormHandleEvent(EventType *event)
{
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		if (!gRows)
			gRows = MemPtrNew(sizeof(VmConvRow) * HOME_MAX_ROWS);
		if (!gRows)
			return false;
		RowListInit(&gList, HomeGadget, HomeScroll, VM_ROW_H, DrawRow, Selectable, NULL);
		gList.hold = Hold;
		Rebuild(false);
		FrmDrawForm(FrmGetActiveForm());
		RowListDraw(&gList);
		DrawTitleStatus();
		if (!gAppAutoConnectTried && !SessionIsConnected()) {
			gAppAutoConnectTried = true;
			AppConnect(ShowStatus, true);
			Rebuild(true);
		}
		return true;

	case frmCloseEvent:
		if (gRows) {
			MemPtrFree(gRows);
			gRows = NULL;
		}
		return false;

	case frmUpdateEvent:
		if (!gRows)
			return false;
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			FrmDrawForm(FrmGetActiveForm());
			RowListDraw(&gList);
			DrawTitleStatus();
			return true;
		}
		Rebuild(true);
		return true;

	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case HomeContactsButton:
			FrmGotoForm(ContactsForm);
			return true;
		case HomeNodeButton:
			FrmGotoForm(MainForm);
			return true;
		}
		break;

	default:
		break;
	}

	if (gRows && RowListHandleEvent(&gList, event, &tapped)) {
		if (tapped >= 0)
			AppOpenConversation(&gRows[tapped].conv);
		return true;
	}
	return false;
}
