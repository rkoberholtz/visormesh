/*
 * A contact's details and signal history, and Add Contact (by key, for a node
 * the radio hasn't heard an advert from).
 *
 * Detail: type, route, last heard, the full key, then every time the radio
 * heard the node directly (its own zero-hop adverts, or relaying packets for a
 * repeater) with SNR and RSSI. Message opens the DM; for a room it logs in
 * first if needed. Login asks for the password and opens a room's posts or
 * a repeater's admin console. More has what suits the type: Admin Console,
 * Trace / Route, Ping, Reset Path, Block, Beam and Remove.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "session.h"
#include "contacts.h"
#include "contacts_form.h"
#include "heard.h"
#include "app.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "screen_fmt.h"
#include "contact_form.h"
#include "remote.h"
#include "trace_form.h"
#include "blocks.h"
#include "beam.h"

/* In an extra code segment (VM_SECT3): see sections.h for the rules. */
static void *GetObject(FormType *form, UInt16 id) VM_SECT3;
static void Error(const Char *msg) VM_SECT3;
static void ErrorFor(Err err) VM_SECT3;
static void FormatWhen(Char *out, UInt32 utc) VM_SECT3;
static void LocalTime(ScrTime *t, UInt32 utc) VM_SECT3;
static void DrawHex(const UInt8 *bytes, UInt16 n, Coord x, Coord y) VM_SECT3;
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT3;
static void Load(void) VM_SECT3;
static void Status(const Char *text) VM_SECT3;
static Boolean Login(FormType *form) VM_SECT3;
static void OpenConversation(void) VM_SECT3;
static Boolean ReloadContact(void) VM_SECT3;
static Boolean Connected(void) VM_SECT3;
static void More(FormType *form) VM_SECT3;
static void ShowButtons(FormType *form) VM_SECT3;
static Boolean Add(FormType *form) VM_SECT3;

#define INFO_ROWS 5      /* type/route, heard, key x2, the Signal header */
#define MAX_HIST  (VM_HEARD_MAX)

static VmContact gContact;
static VmHeard   gHist[MAX_HIST];
static UInt16    gHistCount;
static RowList   gList;
static Char      gTitle[VM_NAME_SIZE + 2];

void ContactFormOpen(const VmContact *c)
{
	gContact = *c;
	FrmGotoForm(ContactForm);
}

static void *GetObject(FormType *form, UInt16 id)
{
	return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void Error(const Char *msg)
{
	FrmCustomAlert(SettingsErrorAlert, msg, "", "");
}

static void ErrorFor(Err err)
{
	Char msg[48];

	AppErrorText(err, msg);
	Error(msg);
}

/* "11-01 17:32", local time. */
static void FormatWhen(Char *out, UInt32 utc)
{
	DateTimeType dt;

	TimSecondsToDateTime(SessionLocalPalm(utc), &dt);
	StrPrintF(out, "%02u-%02u %02u:%02u", dt.month, dt.day, dt.hour, dt.minute);
}

static void LocalTime(ScrTime *t, UInt32 utc)
{
	DateTimeType dt;

	TimSecondsToDateTime(SessionLocalPalm(utc), &dt);
	t->month = (UInt8)dt.month;
	t->day = (UInt8)dt.day;
	t->hour = (UInt8)dt.hour;
	t->minute = (UInt8)dt.minute;
}

static void DrawHex(const UInt8 *bytes, UInt16 n, Coord x, Coord y)
{
	Char hex[2 * 16 + 1];

	if (n > 16)
		n = 16;
	TxtHex(bytes, n, hex);
	WinDrawChars(hex, (Int16)(2 * n), x, y);
}

static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x + 1, y = b->topLeft.y, right = b->topLeft.x + b->extent.x - 1;
	Char line[SCR_LINE], route[12], when[16], snr[8];
	ScrTime t;
	UInt32 now = SessionNow();

	switch (row) {
	case 0:
		/* tests/test_screens.c checks these two rows fit in the large font */
		ContactFormatRoute(route, gContact.outPathLen);
		ScrContactSummary(line, ContactTypeGlyph(gContact.type), ContactTypeName(gContact.type), route,
		                  SessionIsLoggedIn(gContact.pubKey));
		DrawCut(line, x, y, right - x);
		break;
	case 1:
		if (gContact.lastAdvert) {
			LocalTime(&t, gContact.lastAdvert);
			TxtFormatAge(route, now > gContact.lastAdvert ? now - gContact.lastAdvert : 0);
			ScrContactAdvert(line, &t, route);
		} else {
			StrCopy(line, "No advert heard yet");
		}
		DrawCut(line, x, y, right - x);
		break;
	case 2:
		WinDrawChars("Key", 3, x, y);
		DrawHex(gContact.pubKey, 16, x + 18, y);
		break;
	case 3:
		DrawHex(gContact.pubKey + 16, 16, x + 18, y);
		break;
	case 4:
		DrawSection(gHistCount ? "heard directly" : "not heard directly yet", b->topLeft.x, y, b->extent.x);
		break;
	default: {
		const VmHeard *h = &gHist[row - INFO_ROWS];

		FormatWhen(when, h->time);
		WinDrawChars(when, StrLen(when), x, y);
		if (h->kind == VM_HEARD_ADVERT_FAR) {
			/* the signal is the last repeater's, not this node's */
			StrPrintF(line, "advert via %u hop%s", h->advertHops, h->advertHops == 1 ? "" : "s");
			DrawRight(line, right, y);
			break;
		}
		TxtFormatSnr(snr, h->snrQ);
		StrPrintF(line, "%s %ddBm", snr, (Int16)h->rssi);
		WinDrawChars(line, StrLen(line), x + 48, y);
		if (h->kind == VM_HEARD_RELAY && h->count > 1)
			StrPrintF(line, "relay x%u", h->count);
		else
			StrCopy(line, h->kind == VM_HEARD_RELAY ? "relay" : "advert");
		DrawRight(line, right, y);
		break;
	}
	}
	FntSetFont(old);
}

static void Load(void)
{
	/* a repeater is heard relaying others' packets; anything else only by its adverts */
	gHistCount = HeardFor(gContact.pubKey, gContact.type == VM_ADV_TYPE_REPEATER, gHist, MAX_HIST);
	RowListSetCount(&gList, INFO_ROWS + gHistCount);
}

/* A line of progress over the bottom of the list while the radio works. */
static void Status(const Char *text)
{
	FormType *form = FrmGetActiveForm();
	RectangleType r;
	FontID old;

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, ContactGadget), &r);
	r.topLeft.y += r.extent.y - VM_ROW_H - 1;
	r.extent.y = VM_ROW_H + 1;
	WinEraseRectangle(&r, 0);
	WinDrawLine(r.topLeft.x, r.topLeft.y, r.topLeft.x + r.extent.x - 1, r.topLeft.y);
	old = FntSetFont(vmSmallFont);
	WinDrawChars(text, StrLen(text), r.topLeft.x + 2, r.topLeft.y + 1);
	FntSetFont(old);
}

/* Asks for the password and logs in; true on success. */
static Boolean Login(FormType *form)
{
	Char pw[VM_NAME_SIZE], msg[48];
	Boolean admin = false;
	Err err;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first (Node).");
		return false;
	}
	pw[0] = 0;
	if (FrmCustomResponseAlert(PasswordAlert, gContact.name, "", "", pw, sizeof(pw), NULL) != 0)
		return false;
	Status("Logging in (waiting for an answer over the mesh)...");
	err = SessionLogin(gContact.pubKey, pw, &admin);
	MemSet(pw, sizeof(pw), 0);
	RowListDraw(&gList);
	if (err != errNone) {
		if (err == vmErrNoReply)
			Error("No answer: the node may be out of range. Try again, or Reset Path under More.");
		else
			ErrorFor(err);
		return false;
	}
	if (!admin && gContact.type == VM_ADV_TYPE_REPEATER) {
		StrPrintF(msg, "Logged in to %s as a guest: status and telemetry only, no CLI.", gContact.name);
		FrmCustomAlert(ContactInfoAlert, "Login", msg, "");
	}
	return true;
}

static void OpenConversation(void)
{
	VmConv conv;

	MemSet(&conv, sizeof(conv), 0);
	conv.type = VM_CONV_DIRECT;
	MemMove(conv.key, gContact.pubKey, 6);
	AppOpenConversation(&conv);
}

/* Re-reads the contact after a refresh; false if it's gone. */
static Boolean ReloadContact(void)
{
	return ContactsFindByPrefixN(gContact.pubKey, VM_PUB_KEY_SIZE, &gContact);
}

static Boolean Connected(void)
{
	if (SessionIsConnected())
		return true;
	Error("Connect to the radio first (Node).");
	return false;
}

void ContactResetPath(const VmContact *c)
{
	Err err;

	if (!Connected())
		return;
	err = SessionKeyCommand(VM_CMD_RESET_PATH, c->pubKey);
	if (err == errNone)
		err = ContactsRefresh(NULL);
	if (err != errNone)
		ErrorFor(err);
}

Boolean ContactRemove(const VmContact *c)
{
	Err err;

	if (!Connected() || FrmCustomAlert(RemoveContactAlert, c->name, "", "") != 0)
		return false;
	err = SessionKeyCommand(VM_CMD_REMOVE_CONTACT, c->pubKey);
	if (err == errNone)
		err = ContactsRefresh(NULL);
	if (err != errNone) {
		ErrorFor(err);
		return false;
	}
	return true;
}

/* More's actions: the ones that suit this contact's type. */
#define MORE_CONSOLE 0
#define MORE_TRACE   1
#define MORE_PING    2
#define MORE_RESET   3
#define MORE_BLOCK   4
#define MORE_BEAM    5
#define MORE_REMOVE  6

static void More(FormType *form)
{
	ListType *list = GetObject(form, ContactMoreList);
	Char *items[8];
	UInt8 acts[8];
	UInt16 n = 0;
	UInt8 t = gContact.type;
	Int16 pick;

	if (t == VM_ADV_TYPE_ROOM || t == VM_ADV_TYPE_REPEATER) {
		items[n] = "Admin Console";
		acts[n++] = MORE_CONSOLE;
	}
	items[n] = "Trace / Route";
	acts[n++] = MORE_TRACE;
	if (t == VM_ADV_TYPE_REPEATER) {
		items[n] = "Ping";
		acts[n++] = MORE_PING;
	}
	items[n] = "Reset Path";
	acts[n++] = MORE_RESET;
	if (t == VM_ADV_TYPE_CHAT) {
		items[n] = BlocksIsContact(&gContact) ? "Unblock" : "Block";
		acts[n++] = MORE_BLOCK;
	}
	items[n] = "Beam";
	acts[n++] = MORE_BEAM;
	items[n] = "Remove";
	acts[n++] = MORE_REMOVE;
	LstSetListChoices(list, items, n);
	LstSetHeight(list, n);
	pick = LstPopupList(list);
	LstSetListChoices(list, NULL, 0);
	if (pick < 0)
		return;
	switch (acts[pick]) {
	case MORE_CONSOLE:
		if (Connected() && (SessionIsLoggedIn(gContact.pubKey) || Login(form)))
			RemoteOpen(&gContact);
		break;
	case MORE_TRACE:
		TraceFormOpen(&gContact);
		break;
	case MORE_PING:
		TracePing(&gContact);
		break;
	case MORE_RESET:
		ContactResetPath(&gContact);
		ReloadContact();
		RowListDraw(&gList);
		break;
	case MORE_BLOCK:
		BlocksToggleContact(&gContact);
		break;
	case MORE_BEAM:
		BeamContact(&gContact);
		break;
	case MORE_REMOVE:
		if (ContactRemove(&gContact))
			FrmGotoForm(ContactsForm);
		break;
	}
}

/* Message for nodes that take text (companions, rooms), Login for rooms and repeaters. */
static void ShowButtons(FormType *form)
{
	UInt8 t = gContact.type;
	UInt16 msg = FrmGetObjectIndex(form, ContactMessageButton), login = FrmGetObjectIndex(form, ContactLoginButton);

	if (t == VM_ADV_TYPE_CHAT || t == VM_ADV_TYPE_ROOM)
		FrmShowObject(form, msg);
	else
		FrmHideObject(form, msg);
	if (t == VM_ADV_TYPE_ROOM || t == VM_ADV_TYPE_REPEATER)
		FrmShowObject(form, login);
	else
		FrmHideObject(form, login);
}

Boolean ContactFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		StrNCopy(gTitle, gContact.name, VM_NAME_SIZE - 1);
		gTitle[VM_NAME_SIZE - 1] = 0;
		FrmCopyTitle(form, gTitle);
		RowListInit(&gList, ContactGadget, ContactScroll, VM_ROW_H, DrawRow, NULL, NULL);
		Load();
		FrmDrawForm(form);
		ShowButtons(form);
		RowListDraw(&gList);
		return true;

	case frmUpdateEvent:
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			FrmDrawForm(form);
			ShowButtons(form);
		}
		Load();
		RowListDraw(&gList);
		return true;

	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ContactDoneButton:
			FrmGotoForm(ContactsForm);
			return true;
		case ContactMessageButton:
			/* a room only takes posts from members: log in first */
			if (gContact.type == VM_ADV_TYPE_ROOM && !SessionIsLoggedIn(gContact.pubKey) && !Login(form))
				return true;
			OpenConversation();
			return true;
		case ContactLoginButton:
			/* a room opens to its posts, a repeater to its admin console */
			if (!Login(form))
				RowListDraw(&gList);
			else if (gContact.type == VM_ADV_TYPE_ROOM)
				OpenConversation();
			else
				RemoteOpen(&gContact);
			return true;
		case ContactMoreButton:
			More(form);
			return true;
		}
		break;

	default:
		break;
	}
	return RowListHandleEvent(&gList, event, &tapped);
}

/* ---- Add Contact ---- */

static Boolean Add(FormType *form)
{
	const Char *name = FldGetTextPtr(GetObject(form, ContactAddNameField));
	const Char *hex = FldGetTextPtr(GetObject(form, ContactAddKeyField));
	UInt16 sel = FrmGetObjectId(form, FrmGetControlGroupSelection(form, ContactAddTypeGroup));
	UInt8 key[VM_PUB_KEY_SIZE], type;
	VmContact c;
	Err err;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first (Node).");
		return false;
	}
	if (!name || !*name) {
		Error("Enter a name for the contact.");
		return false;
	}
	if (!hex || !VmParseHex(hex, key, sizeof(key)) || StrLen(hex) < 2 * VM_PUB_KEY_SIZE) {
		Error("The key is 64 hex digits (0-9, a-f); spaces are fine.");
		return false;
	}
	type = sel == ContactAddRoomPush ? VM_ADV_TYPE_ROOM :
	       sel == ContactAddRepeaterPush ? VM_ADV_TYPE_REPEATER : VM_ADV_TYPE_CHAT;
	err = SessionAddContact(key, type, name);
	if (err == errNone)
		err = ContactsRefresh(NULL);
	if (err != errNone) {
		ErrorFor(err);
		return false;
	}
	if (ContactsFindByPrefixN(key, VM_PUB_KEY_SIZE, &c))
		ContactFormOpen(&c);
	else
		FrmGotoForm(ContactsForm);
	return true;
}

Boolean ContactAddFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		FrmSetControlGroupSelection(form, ContactAddTypeGroup, ContactAddChatPush);
		FrmDrawForm(form);
		FrmSetFocus(form, FrmGetObjectIndex(form, ContactAddNameField));
		return true;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ContactAddAddButton:
			Add(form);
			return true;
		case ContactAddCancelButton:
			FrmGotoForm(ContactsForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
