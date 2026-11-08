/*
 * Contacts, MeshTerm style: our node pinned first (star), then the stored
 * contacts by name, each with its type mark, name, when it was last heard and
 * its route. Refresh downloads the list, Add adds one by key; tap a contact
 * for its details.
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
#include "contact_form.h"

/* In the second code segment: see sections.h for the rules. */
static UInt16 SelfRows(void) VM_SECT2;
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;
static void Load(Boolean draw) VM_SECT2;
static void SetTitle(FormType *form) VM_SECT2;
static void Refresh(FormType *form) VM_SECT2;
static void ShowDetails(UInt16 row) VM_SECT2;

#define NAME_X   8
#define NAME_W   82

static RowList gList;

Char ContactTypeGlyph(UInt8 type)
{
	switch (type) {
	case VM_ADV_TYPE_CHAT:     return GLYPH_COMPANION;
	case VM_ADV_TYPE_REPEATER: return GLYPH_REPEATER;
	case VM_ADV_TYPE_ROOM:     return GLYPH_ROOM;
	case VM_ADV_TYPE_SENSOR:   return GLYPH_SENSOR;
	default:                   return GLYPH_UNKNOWN;
	}
}

const Char *ContactTypeName(UInt8 type)
{
	switch (type) {
	case VM_ADV_TYPE_CHAT:     return "Companion";
	case VM_ADV_TYPE_REPEATER: return "Repeater";
	case VM_ADV_TYPE_ROOM:     return "Room";
	case VM_ADV_TYPE_SENSOR:   return "Sensor";
	default:                   return "Unknown";
	}
}

/* "direct", "2 hops", or "flood" (no known path). */
void ContactFormatRoute(Char *out, UInt8 pathLen)
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
	Coord x = b->topLeft.x, y = b->topLeft.y, right = x + b->extent.x, end;
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
	mark = ContactTypeGlyph(c.type);
	WinDrawChars(&mark, 1, x + 1, y);
	/* age at the right; the route only when known (most are flood), so names get the room */
	if (c.lastAdvert) {
		UInt32 now = SessionNow();

		TxtFormatAge(buf, now > c.lastAdvert ? now - c.lastAdvert : 0);
		DrawRight(buf, right - 1, y);
	}
	end = right - 1 - FntCharsWidth("000d", 4) - 4;
	if (c.outPathLen != VM_PATH_UNKNOWN) {
		ContactFormatRoute(buf, c.outPathLen);
		DrawRight(buf, end, y);
		end -= FntCharsWidth(buf, StrLen(buf)) + 4;
	}
	DrawCut(c.name, x + NAME_X, y, end - (x + NAME_X));
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

	if (row >= SelfRows() && ContactsGet(row - SelfRows(), &c))
		ContactFormOpen(&c);
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
		case ContactsAddButton:
			if (SessionIsConnected())
				FrmGotoForm(ContactAddForm);
			else
				FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first (Node).", "", "");
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
