/*
 * Trace Path: sends a trace through the repeaters typed as hop codes (or
 * picked with Add), out and, with Round trip, back the same way, and lists
 * the SNR each hop heard it at and the time it took. Opened for a contact
 * (More > Trace / Route), it starts from the contact's route, and Set Route
 * makes the hops (out only) the contact's route. Ping is a trace to a
 * repeater one hop away and back.
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "contacts.h"
#include "contact_form.h"
#include "protocol2.h"
#include "info_fmt.h"
#include "app.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "ui/glyphs.h"
#include "ui/title.h"
#include "trace_form.h"

static void   *GetObject(FormType *form, UInt16 id) VM_SECT5;
static void    Note(const Char *msg) VM_SECT5;
static UInt8   TraceSize(void) VM_SECT5;
static void    FillFromRoute(FormType *form) VM_SECT5;
static void    DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT5;
static Int16   ReadHops(FormType *form, UInt8 *hops, UInt8 *size) VM_SECT5;
static void    Run(FormType *form) VM_SECT5;
static void    AddHop(FormType *form) VM_SECT5;
static void    SetRoute(FormType *form) VM_SECT5;
static Boolean IsSelf(const UInt8 *hash, UInt8 size) VM_SECT5;

#define MAX_REPEATERS 40
#define LABEL_MAX     32

static VmContact gContact;
static Boolean   gForContact;
static Char      gLabel[64];      /* cut to LABEL_MAX for the form's label */
static VmTrace   gTrace;
static UInt32    gTripMs;
static Err       gErr;
static UInt8     gSent[VM_TRACE_MAX];   /* the path traced, for naming the hops */
static RowList   gList;

void TraceFormOpen(const VmContact *c)
{
	gForContact = c != NULL;
	if (c)
		gContact = *c;
	FrmGotoForm(TraceForm);
}

static void *GetObject(FormType *form, UInt16 id)
{
	return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void Note(const Char *msg)
{
	FrmCustomAlert(SettingsErrorAlert, msg, "", "");
}

/* Hop codes as long as the radio's path hashes (a 3-byte one is traced as 4: keys match either way). */
static UInt8 TraceSize(void)
{
	UInt8 mode = SessionDevice()->hasPathHashMode ? SessionDevice()->pathHashMode : 0;

	return mode == 0 ? 1 : mode == 1 ? 2 : 4;
}

/* The contact's route from the radio as hop codes; a repeater's own code goes last, as the trace's far end. */
static void FillFromRoute(FormType *form)
{
	UInt8 frame[VM_HEADER_LEN + 1 + VM_PUB_KEY_SIZE], reply[VM_CONTACT_FRAME_LEN], pathLen, path[VM_MAX_PATH_SIZE];
	FieldType *f = GetObject(form, TraceField);
	Char text[64], hex[10];
	UInt16 len, got, i, hops = 0, size = 0;

	text[0] = 0;
	StrPrintF(gLabel, "%s: ", gContact.name);
	len = VmBuildGetContactByKey(frame, sizeof(frame), gContact.pubKey);
	if (SessionQuery(frame, len, VM_RESP_CONTACT, reply, sizeof(reply), &got) == errNone &&
	    VmContactPath(reply, got, &pathLen, path)) {
		if (pathLen == VM_PATH_UNKNOWN) {
			StrCat(gLabel, "no route (flood)");
		} else {
			hops = VM_PATH_HOPS(pathLen);
			size = VM_PATH_HASH_BYTES(pathLen);
			if (hops == 0)
				StrCat(gLabel, "direct, no repeaters");
			else
				StrPrintF(gLabel + StrLen(gLabel), "route of %u hop%s", hops, hops == 1 ? "" : "s");
			for (i = 0; i < hops && (i + 1) * size <= VM_MAX_PATH_SIZE && StrLen(text) + 10 < sizeof(text); i++) {
				TxtHex(path + i * size, size, hex);
				if (i)
					StrCat(text, " ");
				StrCat(text, hex);
			}
		}
	} else {
		StrCat(gLabel, "route unknown");
	}
	if (gContact.type == VM_ADV_TYPE_REPEATER && StrLen(text) + 10 < sizeof(text)) {
		TxtHex(gContact.pubKey, size ? size : TraceSize(), hex);
		if (text[0])
			StrCat(text, " ");
		StrCat(text, hex);
	}
	FldDelete(f, 0, FldGetTextLength(f));
	FldInsert(f, text, StrLen(text));
}

/*
 * Row 0: the outcome; then one per hop: who heard it and at what SNR, the
 * last being us.
 */
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x + 1, y = b->topLeft.y, right = b->topLeft.x + b->extent.x - 1;
	Char line[48], snr[8], msg[48];

	if (row == 0) {
		if (gErr == vmErrNoReply)
			StrCopy(line, "No answer: a hop didn't hear it.");
		else if (gErr != errNone) {
			AppErrorText(gErr, msg);
			StrCopy(line, msg);
		} else
			StrPrintF(line, "Back in %lu.%lu s, %u hop%s", gTripMs / 1000, gTripMs % 1000 / 100,
			          gTrace.hops, gTrace.hops == 1 ? "" : "s");
		WinDrawChars(line, StrLen(line), x, y);
	} else {
		line[0] = GLYPH_FORWARD;
		line[1] = ' ';
		line[2] = 0;
		if (row - 1 < gTrace.hops)
			InfoHop(line, sizeof(line), gSent + (row - 1) * gTrace.hashSize, gTrace.hashSize);
		else
			StrCat(line, "you");
		DrawCut(line, x, y, right - 30 - x);
		TxtFormatSnr(snr, gTrace.snrQ[row - 1]);
		DrawRight(snr, right, y);
	}
	FntSetFont(old);
}

/* The typed hops; -1 (after saying why) if they don't parse. */
static Int16 ReadHops(FormType *form, UInt8 *hops, UInt8 *size)
{
	const Char *t = FldGetTextPtr(GetObject(form, TraceField));
	Int16 n = VmParseHopCodes(t ? t : "", hops, VM_TRACE_MAX, size);

	if (n < 0)
		Note("Hops are hex codes of 2, 4 or 8 digits, all the same length, separated by spaces.");
	return n;
}

static void Run(FormType *form)
{
	UInt8 hops[VM_TRACE_MAX], size;
	Int16 n = ReadHops(form, hops, &size), i, total;

	if (n < 0)
		return;
	if (n == 0) {
		Note("Add a hop first: the repeaters to trace through.");
		return;
	}
	if (!SessionIsConnected()) {
		Note("Connect to the radio first (Node).");
		return;
	}
	total = n;
	if (CtlGetValue(GetObject(form, TraceRoundCheck))) {
		/* back the same way: A B C -> A B C B A */
		for (i = n - 2; i >= 0 && (total + 1) * size <= VM_TRACE_MAX; i--, total++)
			MemMove(hops + total * size, hops + i * size, size);
		if (i >= 0) {
			Note("That's too many hops for a round trip.");
			return;
		}
	}
	MemMove(gSent, hops, total * size);
	TitleStatus("tracing...");
	gErr = SessionTrace(hops, total * size, size, &gTrace, &gTripMs);
	TitleStatus("");
	if (gErr != errNone)
		gTrace.hops = 0;
	RowListSetCount(&gList, gErr == errNone ? 2 + gTrace.hops : 1);
	RowListScrollTo(&gList, 0);
	RowListDraw(&gList);
}

/* Appends a repeater's code, picked from the contacts by name. */
static void AddHop(FormType *form)
{
	ListType *list = GetObject(form, TraceHopList);
	FieldType *f = GetObject(form, TraceField);
	Char **names;
	UInt8 (*keys)[4];
	UInt16 i, n = 0, count = ContactsCount();
	UInt8 size = TraceSize();
	VmContact c;
	Char hex[10];
	Int16 pick;

	names = MemPtrNew(MAX_REPEATERS * (sizeof(Char *) + VM_NAME_SIZE));
	keys = MemPtrNew(MAX_REPEATERS * 4);
	if (!names || !keys) {
		if (names)
			MemPtrFree(names);
		if (keys)
			MemPtrFree(keys);
		return;
	}
	for (i = 0; i < count && n < MAX_REPEATERS; i++) {
		if (!ContactsGet(i, &c) || c.type != VM_ADV_TYPE_REPEATER)
			continue;
		names[n] = (Char *)(names + MAX_REPEATERS) + n * VM_NAME_SIZE;
		StrCopy(names[n], c.name);
		MemMove(keys[n], c.pubKey, 4);
		n++;
	}
	if (n == 0) {
		Note("No repeaters in the contacts yet. Type a hop's code instead.");
	} else {
		LstSetListChoices(list, names, n);
		LstSetHeight(list, n < 10 ? n : 10);
		pick = LstPopupList(list);
		if (pick >= 0) {
			/* the size of the codes already typed, if any */
			const Char *t = FldGetTextPtr(f);
			UInt8 typed[VM_TRACE_MAX], tsize;

			if (t && *t && VmParseHopCodes(t, typed, sizeof(typed), &tsize) > 0)
				size = tsize;
			TxtHex(keys[pick], size, hex);
			FldSetInsertionPoint(f, FldGetTextLength(f));
			if (FldGetTextLength(f) && t[FldGetTextLength(f) - 1] != ' ')
				FldInsert(f, " ", 1);
			FldInsert(f, hex, StrLen(hex));
		}
	}
	LstSetListChoices(list, NULL, 0);
	MemPtrFree(keys);
	MemPtrFree(names);
}

static Boolean IsSelf(const UInt8 *hash, UInt8 size)
{
	return MemCmp(hash, gContact.pubKey, size) == 0;
}

/* The typed hops (out only; a repeater's own code at the end dropped) become the contact's route. */
static void SetRoute(FormType *form)
{
	UInt8 hops[VM_TRACE_MAX], frame[VM_HEADER_LEN + VM_CONTACT_FRAME_LEN], reply[VM_CONTACT_FRAME_LEN];
	UInt8 size, pathSize = SessionDevice()->hasPathHashMode ? SessionDevice()->pathHashMode + 1 : 1, i;
	Int16 n = ReadHops(form, hops, &size);
	Char how[80], hex[10];
	UInt16 len, got;
	Err err;

	if (n < 0)
		return;
	if (!SessionIsConnected()) {
		Note("Connect to the radio first (Node).");
		return;
	}
	if (n > 0 && IsSelf(hops + (n - 1) * size, size))
		n--;
	if (n > 0 && size < pathSize) {
		StrPrintF(how, "Routes use %u-byte codes (%u hex digits) on this radio.", pathSize, pathSize * 2);
		Note(how);
		return;
	}
	/* stored at the radio's path hash size: longer codes are cut */
	for (i = 1; i < n && size > pathSize; i++)
		MemMove(hops + i * pathSize, hops + i * size, pathSize);
	if (n == 0) {
		StrCopy(how, "directly, with no repeaters");
	} else {
		StrCopy(how, "through");
		for (i = 0; i < n && StrLen(how) + 10 < sizeof(how); i++) {
			TxtHex(hops + i * pathSize, pathSize, hex);
			StrCat(how, " ");
			StrCat(how, hex);
		}
	}
	if (FrmCustomAlert(TraceRouteAlert, gContact.name, how, "") != 0)
		return;
	len = VmBuildGetContactByKey(frame, sizeof(frame), gContact.pubKey);
	err = SessionQuery(frame, len, VM_RESP_CONTACT, reply, sizeof(reply), &got);
	if (err == errNone) {
		len = VmBuildContactRoute(frame, sizeof(frame), reply, got, pathSize, hops, (UInt8)n);
		err = len ? SessionCommand(frame, len) : vmErrBadReply;
	}
	if (err == errNone)
		err = ContactsUpdate();
	if (err != errNone) {
		AppErrorText(err, how);
		Note(how);
		return;
	}
	ContactsFindByPrefixN(gContact.pubKey, VM_PUB_KEY_SIZE, &gContact);
	FillFromRoute(form);
	gLabel[LABEL_MAX] = 0;
	FrmCopyLabel(form, TraceForLabel, gLabel);
}

void TracePing(const VmContact *c)
{
	UInt8 size = TraceSize();
	VmTrace t;
	UInt32 ms;
	Char text[120], there[8], back[8], msg[48];
	Err err;

	if (!SessionIsConnected()) {
		Note("Connect to the radio first (Node).");
		return;
	}
	if (c->type != VM_ADV_TYPE_REPEATER) {
		Note("Only repeaters answer a ping (they pass traces on).");
		return;
	}
	err = SessionTrace(c->pubKey, size, size, &t, &ms);
	if (err == errNone && t.hops >= 1) {
		TxtFormatSnr(there, t.snrQ[0]);
		TxtFormatSnr(back, t.snrQ[t.hops]);
		StrPrintF(text, "%s answered in %lu.%lu s.\nIt heard us at %s, we heard it at %s.", c->name,
		          ms / 1000, ms % 1000 / 100, there, back);
	} else if (err == vmErrNoReply) {
		StrPrintF(text, "No answer from %s: it may be out of direct range. Trace Path reaches it through other repeaters.", c->name);
	} else {
		AppErrorText(err, msg);
		StrCopy(text, msg);
	}
	FrmCustomAlert(PingAlert, text, "", "");
}

Boolean TraceFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		gErr = errNone;
		TitleStatusReset();
		RowListInit(&gList, TraceGadget, TraceScroll, VM_ROW_H, DrawRow, NULL, NULL);
		CtlSetValue(GetObject(form, TraceRoundCheck), 1);
		gLabel[0] = 0;
		if (gForContact && SessionIsConnected())
			FillFromRoute(form);
		else if (gForContact)
			StrPrintF(gLabel, "%s: connect to read its route", gContact.name);
		else
			StrCopy(gLabel, "Repeaters to trace through:");
		gLabel[LABEL_MAX] = 0;
		FrmCopyLabel(form, TraceForLabel, gLabel);
		if (!gForContact)
			FrmHideObject(form, FrmGetObjectIndex(form, TraceRouteButton));
		FrmDrawForm(form);
		RowListDraw(&gList);
		FrmSetFocus(form, FrmGetObjectIndex(form, TraceField));
		return true;
	case frmUpdateEvent:
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			FrmDrawForm(form);
			RowListDraw(&gList);
		}
		return true;
	case keyDownEvent:
		if (event->data.keyDown.chr == linefeedChr) {
			Run(form);
			return true;
		}
		break;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case TraceGoButton:
			Run(form);
			return true;
		case TraceAddButton:
			AddHop(form);
			return true;
		case TraceRouteButton:
			SetRoute(form);
			return true;
		case TraceDoneButton:
			if (gForContact)
				ContactFormOpen(&gContact);
			else
				FrmGotoForm(HomeForm);
			return true;
		}
		break;
	default:
		break;
	}
	return RowListHandleEvent(&gList, event, &tapped);
}
