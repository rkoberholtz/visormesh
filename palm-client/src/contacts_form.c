/*
 * Contacts, MeshTerm style: our node pinned first (star), then the stored
 * contacts by name, each with its type mark, name, when it was last heard and
 * its route. Refresh downloads the list; tap a contact for its details and
 * Message to open a direct conversation.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "session.h"
#include "contacts.h"
#include "convos.h"
#include "app.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "contacts_form.h"

#define NAME_X   8
#define NAME_W   82
#define HEARD_R  110

static RowList gList;

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

static const Char *TypeName(UInt8 type)
{
	switch (type) {
	case VM_ADV_TYPE_CHAT:     return "Companion";
	case VM_ADV_TYPE_REPEATER: return "Repeater";
	case VM_ADV_TYPE_ROOM:     return "Room server";
	case VM_ADV_TYPE_SENSOR:   return "Sensor";
	default:                   return "Unknown";
	}
}

/* "direct", "2 hops", or "flood" (no known path). */
static void FormatRoute(Char *out, UInt8 pathLen)
{
	UInt8 hops = VM_PATH_HOPS(pathLen);

	if (pathLen == VM_PATH_UNKNOWN)
		StrCopy(out, "flood");
	else if (hops == 0)
		StrCopy(out, "direct");
	else
		StrPrintF(out, "%u hop%s", hops, hops == 1 ? "" : "s");
}

/* Row 0 is our own node when connected; the stored contacts follow. */
static UInt16 SelfRows(void)
{
	return SessionIsConnected() ? 1 : 0;
}

static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x, y = b->topLeft.y, right = x + b->extent.x;
	Char mark, buf[16];
	VmContact c;

	if (row < SelfRows()) {
		mark = GLYPH_SELF;
		WinDrawChars(&mark, 1, x + 1, y);
		DrawCut(SessionSelf()->name, x + NAME_X, y, NAME_W);
		DrawRight("you", right - 1, y);
		FntSetFont(old);
		return;
	}
	if (!ContactsGet(row - SelfRows(), &c)) {
		FntSetFont(old);
		return;
	}
	mark = TypeGlyph(c.type);
	WinDrawChars(&mark, 1, x + 1, y);
	DrawCut(c.name, x + NAME_X, y, NAME_W);
	if (c.lastAdvert) {
		UInt32 now = SessionNow();

		TxtFormatAge(buf, now > c.lastAdvert ? now - c.lastAdvert : 0);
		DrawRight(buf, x + HEARD_R, y);
	}
	FormatRoute(buf, c.outPathLen);
	DrawRight(buf, right - 1, y);
	FntSetFont(old);
}

static void Load(Boolean draw)
{
	ContactsOpen();
	RowListSetCount(&gList, (UInt16)(ContactsCount() + SelfRows()));
	if (draw)
		RowListDraw(&gList);
}

/* Title: "Contacts (n)". */
static void SetTitle(FormType *form)
{
	static Char title[20];

	StrPrintF(title, "Contacts (%u)", ContactsCount());
	FrmCopyTitle(form, title);
}

static void Refresh(FormType *form)
{
	Char msg[48];
	Err err;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first (Node).", "", "");
		return;
	}
	err = ContactsRefresh(NULL);
	SetTitle(form);
	Load(true);
	if (err != errNone) {
		AppErrorText(err, msg);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
	}
}

static void ShowDetails(UInt16 row)
{
	VmContact c;
	DateTimeType dt;
	VmConv conv;
	Char line[96], route[12];

	if (row < SelfRows() || !ContactsGet(row - SelfRows(), &c))
		return;
	FormatRoute(route, c.outPathLen);
	if (c.lastAdvert) {
		TimSecondsToDateTime(SessionLocalPalm(c.lastAdvert), &dt);
		StrPrintF(line, "%s, %s\nKey %02x%02x%02x%02x%02x%02x...\nHeard %u-%02u-%02u %02u:%02u",
		          TypeName(c.type), route, c.pubKey[0], c.pubKey[1], c.pubKey[2], c.pubKey[3], c.pubKey[4],
		          c.pubKey[5], dt.year, dt.month, dt.day, dt.hour, dt.minute);
	} else {
		StrPrintF(line, "%s, %s\nKey %02x%02x%02x%02x%02x%02x...", TypeName(c.type), route,
		          c.pubKey[0], c.pubKey[1], c.pubKey[2], c.pubKey[3], c.pubKey[4], c.pubKey[5]);
	}
	if (FrmCustomAlert(ContactInfoAlert, c.name, line, "") == 1) {     /* Message */
		MemSet(&conv, sizeof(conv), 0);
		conv.type = VM_CONV_DIRECT;
		MemMove(conv.key, c.pubKey, 6);
		AppOpenConversation(&conv);
	}
}

Boolean ContactsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		RowListInit(&gList, ContactsGadget, ContactsScroll, VM_ROW_H, DrawRow, NULL, NULL);
		Load(false);
		SetTitle(form);
		FrmDrawForm(form);
		RowListDraw(&gList);
		return true;

	case frmUpdateEvent:
		SetTitle(form);
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode)
			FrmDrawForm(form);
		Load(true);
		return true;

	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ContactsRefreshButton:
			Refresh(form);
			return true;
		case ContactsDoneButton:
			FrmGotoForm(HomeForm);
			return true;
		}
		break;

	default:
		break;
	}

	if (RowListHandleEvent(&gList, event, &tapped)) {
		if (tapped >= 0)
			ShowDetails((UInt16)tapped);
		return true;
	}
	return false;
}
