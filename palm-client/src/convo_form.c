/*
 * One conversation, MeshTerm's chat screen: day dividers, a name chip when
 * the sender changes (ours is "* Name"), then the time and wrapped text, with
 * SNR after received messages and the delivery mark after ours. Compose field
 * with a byte counter at the bottom; tap a failed message to retry it.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "session.h"
#include "messages.h"
#include "convos.h"
#include "app.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "convo_form.h"

/* In the second code segment: see sections.h for the rules. */
static UInt16 TextWidth(void) VM_SECT2;
static void SenderOf(const VmMsgHeader *h, Char *out, UInt16 size) VM_SECT2;
static void SuffixOf(const VmMsgHeader *h, Char *out) VM_SECT2;
static UInt32 LocalDay(UInt32 utc) VM_SECT2;
static void AddLine(UInt8 kind, UInt16 msg, UInt16 off, UInt16 len, Boolean first, Boolean last, UInt32 day) VM_SECT2;
static void Build(void) VM_SECT2;
static void DrawTextWithMentions(const Char *text, UInt16 len, Coord x, Coord y) VM_SECT2;
static void DrawLine(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;
static void DrawCounter(void) VM_SECT2;
static UInt16 FirstUnread(void) VM_SECT2;
static void ScrollToNew(void) VM_SECT2;
static void Refresh(Boolean toEnd) VM_SECT2;
static void Send(FormType *form) VM_SECT2;
static void InsertMention(const Char *sender) VM_SECT2;
static void Tapped(UInt16 row) VM_SECT2;

#define MAX_MSGS    40        /* newest messages shown */
#define MAX_LINES   200
#define LINE_H      VM_CHIP_H /* chips need the extra 2 px */
#define TIME_X      2
#define TEXT_X      (gTxtLarge ? 30 : 26)   /* clear of "HH:MM" in either font */

#define L_DAY   0
#define L_CHIP  1
#define L_TEXT  2
#define L_NEW   3      /* "── new ──" before the first message unread when the screen opened */
#define NO_NEW  0xFFFF

typedef struct {
	UInt8  kind;
	UInt8  first;      /* first text line of a message: draw its time */
	UInt8  last;       /* last line of a message: draw its SNR / delivery mark */
	UInt16 msg;        /* message record */
	UInt16 off, len;   /* the text on this line */
	UInt32 day;        /* L_DAY: local day number */
} Line;

static Line   *gLines = NULL;
static UInt16  gCount = 0;
static RowList gList;
static Char    gTitle[VM_NAME_SIZE + 2];
static UInt16  gNewFrom = NO_NEW;   /* message record of the first unread one at open */

static const Char *kDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const Char *kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static UInt16 TextWidth(void)
{
	RectangleType r;
	FormType *form = FrmGetActiveForm();

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, ConvoGadget), &r);
	return (UInt16)(r.extent.x - TEXT_X - 1);
}

/* The sender name a message is filed under (for chips and grouping). */
static void SenderOf(const VmMsgHeader *h, Char *out, UInt16 size)
{
	if (h->dir == VM_DIR_OUT)
		StrPrintF(out, "%c %s", GLYPH_SELF, SessionSelf()->name[0] ? SessionSelf()->name : "you");
	else if (h->conv.type == VM_CONV_CHANNEL)
		StrNCopy(out, h->sender[0] ? h->sender : "?", size - 1);
	else if (h->sender[0])               /* a room post: its author */
		StrNCopy(out, h->sender, size - 1);
	else
		ContactNameForPrefix(h->conv.key, out, size);
	out[size - 1] = 0;
}

/* SNR after a received message, the delivery mark after ours (none for a sent channel message). */
static void SuffixOf(const VmMsgHeader *h, Char *out)
{
	out[0] = 0;
	if (h->dir == VM_DIR_IN) {
		TxtFormatSnr(out, h->snrQuarterDb);
		return;
	}
	switch (h->status) {
	case VM_ST_PENDING: out[0] = GLYPH_ELLIPSIS; out[1] = 0; break;
	case VM_ST_SENT:    if (h->conv.type == VM_CONV_DIRECT) { out[0] = GLYPH_PENDING; out[1] = 0; } break;
	case VM_ST_ACKED:   out[0] = GLYPH_ACKED; out[1] = 0; break;
	case VM_ST_FAILED:  out[0] = GLYPH_FAILED; out[1] = 0; break;
	}
}

static UInt32 LocalDay(UInt32 utc)
{
	return SessionLocalPalm(utc) / 86400UL;
}

static void AddLine(UInt8 kind, UInt16 msg, UInt16 off, UInt16 len, Boolean first, Boolean last, UInt32 day)
{
	Line *l;

	if (gCount >= MAX_LINES)
		return;
	l = &gLines[gCount++];
	l->kind = kind;
	l->msg = msg;
	l->off = off;
	l->len = len;
	l->first = first;
	l->last = last;
	l->day = day;
}

static void Build(void)
{
	UInt16 idx[MAX_MSGS];
	UInt16 nIdx = 0, i, n = MessagesCount(), width = TextWidth();
	UInt32 day, lastDay = 0;
	Char sender[VM_NAME_SIZE + 2], lastSender[VM_NAME_SIZE + 2], suffix[8];
	FontID old = FntSetFont(vmSmallFont);

	gCount = 0;
	lastSender[0] = 0;
	/* the newest MAX_MSGS of this conversation, oldest first */
	for (i = n; i > 0 && nIdx < MAX_MSGS; i--) {
		const VmMsgHeader *h = MessagesLock(i - 1);

		if (h) {
			if (ConvEqual(&h->conv, &gAppConv))
				idx[nIdx++] = i - 1;
			MessagesRelease(i - 1);
		}
	}
	while (nIdx > 0) {
		UInt16 m = idx[--nIdx], off = 0, len, used;
		Boolean firstText = true;
		const VmMsgHeader *h = MessagesLock(m);
		const Char *text;

		if (!h)
			continue;
		text = (const Char *)(h + 1);
		day = LocalDay(h->received);
		if (day != lastDay) {
			AddLine(L_DAY, m, 0, 0, false, false, day);
			lastDay = day;
			lastSender[0] = 0;
		}
		if (m == gNewFrom) {
			AddLine(L_NEW, m, 0, 0, false, false, 0);
			lastSender[0] = 0;      /* start the new part with a sender chip */
		}
		SenderOf(h, sender, sizeof(sender));
		if (StrCompare(sender, lastSender) != 0) {
			AddLine(L_CHIP, m, 0, 0, false, false, 0);
			StrCopy(lastSender, sender);
		}
		do {
			len = TxtWrap(text + off, h->textLen - off, width, TxtSmallWidth, &used);
			AddLine(L_TEXT, m, off, len, firstText, false, 0);
			firstText = false;
			off += used;
		} while (off < h->textLen && used > 0);
		/* the SNR / mark goes after the last line if it fits, else on a line of its own */
		SuffixOf(h, suffix);
		if (suffix[0] && gCount > 0) {
			Line *l = &gLines[gCount - 1];
			Int16 lineW = TxtWidth(text + l->off, l->len, TxtSmallWidth);

			if (lineW + 4 + TxtWidth(suffix, StrLen(suffix), TxtSmallWidth) > (Int16)width)
				AddLine(L_TEXT, m, h->textLen, 0, false, false, 0);
		}
		if (gCount > 0)
			gLines[gCount - 1].last = true;
		MessagesRelease(m);
	}
	FntSetFont(old);
}

/*
 * Draws a line of message text, with each complete @[Name] mention as a chip
 * (MeshTerm's mention chip, @ dropped). A chip is never wider than the raw
 * "@[Name]" it replaces (2 px pads vs three 4 px characters), so the line still
 * fits the width it was wrapped to.
 */
static void DrawTextWithMentions(const Char *text, UInt16 len, Coord x, Coord y)
{
	Char name[VM_NAME_SIZE];
	UInt16 i = 0, start = 0, end, n;

	while (i + 2 < len) {
		if (text[i] != '@' || text[i + 1] != '[') {
			i++;
			continue;
		}
		for (end = i + 2; end < len && text[end] != ']'; end++)
			;
		n = end - (i + 2);
		if (end >= len || n == 0 || n >= sizeof(name)) {
			i++;
			continue;
		}
		WinDrawChars(text + start, i - start, x, y);
		x += FntCharsWidth(text + start, i - start);
		MemMove(name, text + i + 2, n);
		name[n] = 0;
		x += DrawChip(name, x, y - 1);
		i = start = end + 1;
	}
	WinDrawChars(text + start, len - start, x, y);
}

static void DrawLine(UInt16 row, const RectangleType *b, void *ctx)
{
	const Line *l = &gLines[row];
	const VmMsgHeader *h = MessagesLock(l->msg);
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x, y = b->topLeft.y + 1;
	Char buf[VM_NAME_SIZE + 4];
	DateTimeType dt;

	if (!h) {
		FntSetFont(old);
		return;
	}
	if (l->kind == L_NEW) {
		DrawSection("new", x, y, b->extent.x);
	} else if (l->kind == L_DAY) {
		TimSecondsToDateTime(l->day * 86400UL, &dt);
		StrPrintF(buf, "%s %s %u", kDays[DayOfWeek(dt.month, dt.day, dt.year)], kMonths[dt.month - 1], dt.day);
		DrawSection(buf, x, y, b->extent.x);
	} else if (l->kind == L_CHIP) {
		SenderOf(h, buf, sizeof(buf));
		DrawChip(buf, x + 1, b->topLeft.y);
	} else {
		const Char *text = (const Char *)(h + 1) + l->off;

		if (l->first) {
			TimSecondsToDateTime(SessionLocalPalm(h->received), &dt);
			StrPrintF(buf, "%02u:%02u", dt.hour, dt.minute);
			WinDrawChars(buf, StrLen(buf), x + TIME_X, y);
		}
		DrawTextWithMentions(text, l->len, x + TEXT_X, y);
		if (l->last) {
			SuffixOf(h, buf);
			if (buf[0])
				WinDrawChars(buf, StrLen(buf), x + TEXT_X + FntCharsWidth(text, l->len) + (l->len ? 4 : 0), y);
		}
	}
	MessagesRelease(l->msg);
	FntSetFont(old);
}

/* "12/130" bytes used, bottom right. */
static void DrawCounter(void)
{
	FormType *form = FrmGetActiveForm();
	FieldType *field = FrmGetObjectPtr(form, FrmGetObjectIndex(form, ConvoField));
	const Char *t = FldGetTextPtr(field);
	UInt16 limit = gAppConv.type == VM_CONV_CHANNEL ? VM_MAX_CHANNEL_TEXT : VM_MAX_DM_TEXT;
	UInt8 utf8[VM_MAX_DM_TEXT + 4];
	UInt16 used = t ? TxtPalmToUtf8(t, StrLen(t), utf8, sizeof(utf8)) : 0;
	RectangleType r;
	Char buf[12];
	FontID old = FntSetFont(vmSmallFont);

	if (t && *t && used == 0)
		used = sizeof(utf8);       /* over even our buffer */
	StrPrintF(buf, "%u/%u", used, limit);
	RctSetRectangle(&r, 100, 148, 46, 8);
	WinEraseRectangle(&r, 0);
	DrawRight(buf, 146, 149);
	FntSetFont(old);
}

/* The oldest unread message of this conversation, or NO_NEW. */
static UInt16 FirstUnread(void)
{
	UInt16 i, n = MessagesCount(), found = NO_NEW;

	for (i = n; i > 0; i--) {
		const VmMsgHeader *h = MessagesLock(i - 1);

		if (h) {
			if (h->unread && ConvEqual(&h->conv, &gAppConv))
				found = i - 1;
			MessagesRelease(i - 1);
		}
	}
	return found;
}

/* Opens at the "new" divider (one line of context above it), or at the end. */
static void ScrollToNew(void)
{
	UInt16 i;

	for (i = 0; i < gCount; i++) {
		if (gLines[i].kind == L_NEW) {
			RowListScrollTo(&gList, i > 0 ? i - 1 : 0);
			return;
		}
	}
	RowListScrollToEnd(&gList);
}

static void Refresh(Boolean toEnd)
{
	MessagesMarkRead(&gAppConv);
	Build();
	RowListSetCount(&gList, gCount);
	if (toEnd)
		RowListScrollToEnd(&gList);
	RowListDraw(&gList);
}

static void Send(FormType *form)
{
	FieldType *field = FrmGetObjectPtr(form, FrmGetObjectIndex(form, ConvoField));
	const Char *t = FldGetTextPtr(field);
	UInt16 limit = gAppConv.type == VM_CONV_CHANNEL ? VM_MAX_CHANNEL_TEXT : VM_MAX_DM_TEXT;
	UInt8 utf8[VM_MAX_DM_TEXT];
	UInt16 index;
	Char msg[48];
	Err err;

	if (!t || !*t)
		return;
	if (TxtPalmToUtf8(t, StrLen(t), utf8, limit) == 0) {
		StrPrintF(msg, "That's over the %u-byte limit.", limit);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
		return;
	}
	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first (Node).", "", "");
		return;
	}
	err = SessionSend(&gAppConv, t, &index);
	FldDelete(field, 0, FldGetTextLength(field));
	DrawCounter();
	Refresh(true);
	if (err == vmErrNoReply || (err != errNone && err != vmErrRadio)) {
		AppErrorText(err, msg);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
	}
}

/* Inserts "@[Name] " at the cursor, to reply to someone in a channel (MeshTerm's Enter on a message). */
static void InsertMention(const Char *sender)
{
	FormType *form = FrmGetActiveForm();
	FieldType *field = FrmGetObjectPtr(form, FrmGetObjectIndex(form, ConvoField));
	Char buf[VM_NAME_SIZE + 5];

	StrPrintF(buf, "@[%s] ", sender);
	FrmSetFocus(form, FrmGetObjectIndex(form, ConvoField));
	FldInsert(field, buf, StrLen(buf));
	DrawCounter();
}

/* Tap a failed message of ours to retry it, or someone's channel message to reply to them. */
static void Tapped(UInt16 row)
{
	const VmMsgHeader *h = MessagesLock(gLines[row].msg);
	Boolean failed, reply;
	Char sender[VM_NAME_SIZE];

	if (!h)
		return;
	failed = h->dir == VM_DIR_OUT && h->status == VM_ST_FAILED;
	reply = h->dir == VM_DIR_IN && h->conv.type == VM_CONV_CHANNEL && h->sender[0];
	if (reply)
		StrCopy(sender, h->sender);
	MessagesRelease(gLines[row].msg);
	if (failed && FrmAlert(RetryAlert) == 0) {
		SessionRetry(gLines[row].msg);
		Refresh(false);
	} else if (reply) {
		InsertMention(sender);
	}
}

Boolean ConvoFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		if (!gLines)
			gLines = MemPtrNew(sizeof(Line) * MAX_LINES);
		if (!gLines)
			return false;
		ConvName(&gAppConv, gTitle, VM_NAME_SIZE);
		FrmCopyTitle(form, gTitle);
		RowListInit(&gList, ConvoGadget, ConvoScroll, LINE_H, DrawLine, NULL, NULL);
		gNewFrom = FirstUnread();
		Build();
		MessagesMarkRead(&gAppConv);
		RowListSetCount(&gList, gCount);
		ScrollToNew();
		FrmDrawForm(form);
		RowListDraw(&gList);
		DrawCounter();
		FrmSetFocus(form, FrmGetObjectIndex(form, ConvoField));
		return true;

	case frmCloseEvent:
		if (gLines) {
			MemPtrFree(gLines);
			gLines = NULL;
		}
		return false;

	case frmUpdateEvent:
		if (!gLines)
			return false;
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			/* e.g. after the on-screen keyboard: redraw everything, not just the transcript */
			FrmDrawForm(form);
			RowListDraw(&gList);
			DrawCounter();
			return true;
		}
		Refresh(gList.top + RowListVisibleRows(&gList) >= gCount);   /* follow new messages if at the end */
		return true;

	case fldChangedEvent:
		DrawCounter();
		return false;

	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ConvoSendButton:
			Send(form);
			return true;
		case ConvoBackButton:
			FrmGotoForm(HomeForm);
			return true;
		}
		break;

	case keyDownEvent:
		if (event->data.keyDown.chr == linefeedChr) {    /* Graffiti return sends */
			Send(form);
			return true;
		}
		break;

	default:
		break;
	}

	if (gLines && RowListHandleEvent(&gList, event, &tapped)) {
		if (tapped >= 0)
			Tapped((UInt16)tapped);
		return true;
	}
	return false;
}

void ConvoFormIdle(void)
{
	if (gLines)
		DrawCounter();
}
