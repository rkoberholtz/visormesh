/*
 * The admin console for a repeater or room we're logged into: a transcript of
 * CLI commands and replies, a command line, Status and Telemetry requests,
 * and Cmds, a list of common commands (those ending in "..." go into the
 * command line to finish; the others are sent at once).
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "contact_form.h"
#include "remote.h"
#include "ui/draw.h"
#include "ui/rowlist.h"
#include "remote_form.h"

/* In an extra code segment (VM_SECT3): see sections.h for the rules. */
static void *GetObject(FormType *form, UInt16 id) VM_SECT3;
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT3;
static void Reload(Boolean draw) VM_SECT3;
static void SendTyped(FormType *form) VM_SECT3;
static void PickCommand(FormType *form) VM_SECT3;

/* the settings "Read settings" asks for */
#define READ_SETTINGS "get name|get radio|get tx|get repeat|get advert.interval|get flood.advert.interval|get flood.max|get path.hash.mode|ver|clock"

static RowList gList;
static Char    gTitle[VM_NAME_SIZE + 2];

static void *GetObject(FormType *form, UInt16 id)
{
	return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	const VmRLine *l = RemoteLine(row);
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x + 1, y = b->topLeft.y;
	Char mark;

	switch (l->kind) {
	case VM_RL_CMD:
		mark = GLYPH_FORWARD;
		WinDrawChars(&mark, 1, x, y);
		WinDrawChars(l->text, StrLen(l->text), x + 6, y);
		/* underline our commands so they stand apart from the answers */
		WinDrawGrayLine(x + 6, y + VM_ROW_H - 1, x + 6 + FntCharsWidth(l->text, StrLen(l->text)), y + VM_ROW_H - 1);
		break;
	case VM_RL_NOTE:
		WinDrawGrayLine(x, y + VM_ROW_H / 2, x + 3, y + VM_ROW_H / 2);
		WinDrawChars(l->text, StrLen(l->text), x + 6, y);
		break;
	default:
		WinDrawChars(l->text, StrLen(l->text), x + 6, y);
		break;
	}
	FntSetFont(old);
}

static void Reload(Boolean draw)
{
	RowListSetCount(&gList, RemoteLineCount());
	RowListScrollToEnd(&gList);
	if (draw)
		RowListDraw(&gList);
}

static void SendTyped(FormType *form)
{
	FieldType *f = GetObject(form, RemoteField);
	const Char *t = FldGetTextPtr(f);

	if (!t || !*t)
		return;
	RemoteCommand(t);
	FldDelete(f, 0, FldGetTextLength(f));
	Reload(true);
}

static void PickCommand(FormType *form)
{
	ListType *list = GetObject(form, RemoteCmdList);
	Int16 pick = LstPopupList(list);
	const Char *item;
	UInt16 len;

	if (pick < 0)
		return;
	item = LstGetSelectionText(list, pick);
	len = StrLen(item);
	if (pick == 0) {
		RemoteBatch(READ_SETTINGS);
	} else if (len > 3 && StrCompare(item + len - 3, "...") == 0) {
		/* to finish typing: "set tx..." -> "set tx " */
		FieldType *f = GetObject(form, RemoteField);

		FldDelete(f, 0, FldGetTextLength(f));
		FldInsert(f, item, len - 3);
		if (item[len - 4] != ' ')
			FldInsert(f, " ", 1);
		FrmSetFocus(form, FrmGetObjectIndex(form, RemoteField));
		return;
	} else {
		RemoteCommand(item);
	}
	Reload(true);
}

Boolean RemoteFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		StrNCopy(gTitle, RemoteTarget()->name, VM_NAME_SIZE - 1);
		gTitle[VM_NAME_SIZE - 1] = 0;
		FrmCopyTitle(form, gTitle);
		RowListInit(&gList, RemoteGadget, RemoteScroll, VM_ROW_H, DrawRow, NULL, NULL);
		Reload(false);
		FrmDrawForm(form);
		RowListDraw(&gList);
		FrmSetFocus(form, FrmGetObjectIndex(form, RemoteField));
		if (RemoteLineCount() == 0) {
			if (!SessionIsAdmin(RemoteTarget()->pubKey))
				RemoteBatch("ver");       /* a guest gets no answer: the note says so */
			else
				RemoteBatch(READ_SETTINGS);
			Reload(true);
		}
		return true;

	case frmUpdateEvent:
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode)
			FrmDrawForm(form);
		Reload(true);
		return true;

	case keyDownEvent:
		if (event->data.keyDown.chr == linefeedChr || event->data.keyDown.chr == crChr) {
			SendTyped(form);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case RemoteSendButton:
			SendTyped(form);
			return true;
		case RemoteStatusButton:
			RemoteStatus();
			Reload(true);
			return true;
		case RemoteTelemButton:
			RemoteTelemetry();
			Reload(true);
			return true;
		case RemoteCmdsButton:
			PickCommand(form);
			return true;
		case RemoteDoneButton:
			ContactFormOpen(RemoteTarget());
			return true;
		}
		break;

	default:
		break;
	}
	return RowListHandleEvent(&gList, event, &tapped);
}
