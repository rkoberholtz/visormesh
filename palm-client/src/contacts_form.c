/*
 * Contacts screen: the stored list (works offline), Refresh to download it
 * from the radio, tap a contact for its details.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "session.h"
#include "contacts.h"
#include "contacts_form.h"

#define PALM_EPOCH_OFFSET 2082844800UL   /* seconds from 1904-01-01 (Palm) to 1970-01-01 (Unix) */

static const Char *TypeName(UInt8 type)
{
	switch (type) {
	case VM_ADV_TYPE_CHAT:     return "Chat";
	case VM_ADV_TYPE_REPEATER: return "Repeater";
	case VM_ADV_TYPE_ROOM:     return "Room";
	case VM_ADV_TYPE_SENSOR:   return "Sensor";
	default:                   return "?";
	}
}

static void FormatHops(Char *out, UInt8 pathLen)
{
	UInt8 hops = VM_PATH_HOPS(pathLen);

	if (pathLen == VM_PATH_UNKNOWN)
		StrCopy(out, "flood");
	else if (hops == 0)
		StrCopy(out, "direct");
	else
		StrPrintF(out, "%u hop%s", hops, hops == 1 ? "" : "s");
}

/* List draw callback: name on the left, type and route on the right. */
static void DrawContact(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	VmContact c;
	Char right[24], hops[12];
	Int16 rightW, nameW;
	UInt16 nameLen;
	Boolean fits;

	if (!ContactsGet((UInt16)itemNum, &c))
		return;
	FormatHops(hops, c.outPathLen);
	StrPrintF(right, "%s, %s", TypeName(c.type), hops);
	rightW = FntCharsWidth(right, StrLen(right));
	WinDrawChars(right, StrLen(right), bounds->topLeft.x + bounds->extent.x - rightW - 2, bounds->topLeft.y);

	nameLen = StrLen(c.name);
	nameW = bounds->extent.x - rightW - 8;
	FntCharsInWidth(c.name, &nameW, &nameLen, &fits);
	WinDrawChars(c.name, nameLen, bounds->topLeft.x + 1, bounds->topLeft.y);
}

static void SetStatus(FormType *form, const Char *text)
{
	UInt16 index = FrmGetObjectIndex(form, ContactsStatusLabel);

	FrmHideObject(form, index);
	FrmCopyLabel(form, ContactsStatusLabel, text);
	FrmShowObject(form, index);
}

static void LoadList(FormType *form, Boolean redraw)
{
	ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, ContactsList));
	Char status[32];
	UInt16 n = ContactsCount();

	LstSetListChoices(list, NULL, n);
	LstSetDrawFunction(list, DrawContact);
	LstSetSelection(list, noListSelection);
	if (n == 0)
		StrCopy(status, SessionIsConnected() ? "No contacts. Tap Refresh." : "No contacts stored.");
	else
		StrPrintF(status, "%u contact%s", n, n == 1 ? "" : "s");
	if (redraw) {
		LstDrawList(list);
		SetStatus(form, status);
	} else {
		FrmCopyLabel(form, ContactsStatusLabel, status);
	}
}

static void Refresh(FormType *form)
{
	UInt32 total = 0;
	Char msg[40];
	Err err;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first.", "", "");
		return;
	}
	SetStatus(form, "Downloading...");
	err = ContactsRefresh(&total);
	LoadList(form, true);
	if (err == errNone)
		return;
	if (err == vmErrNoReply)
		StrCopy(msg, "No reply from the radio.");
	else if (err == vmErrRadio)
		StrCopy(msg, "The radio returned an error.");
	else
		StrPrintF(msg, "Refresh failed (0x%x).", err);
	FrmCustomAlert(SettingsErrorAlert, msg, "", "");
}

static void ShowDetails(UInt16 index)
{
	VmContact c;
	DateTimeType dt;
	Char line[96], hops[12], key[16];

	if (!ContactsGet(index, &c))
		return;
	FormatHops(hops, c.outPathLen);
	StrPrintF(key, "%02x%02x%02x%02x%02x%02x", c.pubKey[0], c.pubKey[1], c.pubKey[2], c.pubKey[3],
	          c.pubKey[4], c.pubKey[5]);
	if (c.lastAdvert) {
		TimSecondsToDateTime(c.lastAdvert + PALM_EPOCH_OFFSET, &dt);
		StrPrintF(line, "%s, %s\nKey %s...\nLast advert %u-%02u-%02u %02u:%02u", TypeName(c.type), hops, key,
		          dt.year, dt.month, dt.day, dt.hour, dt.minute);
	} else {
		StrPrintF(line, "%s, %s\nKey %s...", TypeName(c.type), hops, key);
	}
	FrmCustomAlert(ContactInfoAlert, c.name, line, "");
}

Boolean ContactsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		ContactsOpen();
		LoadList(form, false);
		FrmDrawForm(form);
		return true;
	case lstSelectEvent:
		if (event->data.lstSelect.listID == ContactsList) {
			ShowDetails((UInt16)event->data.lstSelect.selection);
			return true;
		}
		break;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ContactsRefreshButton:
			Refresh(form);
			return true;
		case ContactsDoneButton:
			FrmGotoForm(MainForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
