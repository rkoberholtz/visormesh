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
#include "ui/title.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "ui/emoji.h"
#include "msg_menu.h"
#include "convo_extra.h"
#include "protocol2.h"
#include "convo_form.h"

/* In the second code segment: see sections.h for the rules. */
static UInt16 TextWidth(void) VM_SECT2;
static void SenderOf(const VmMsgHeader *h, Char *out, UInt16 size) VM_SECT2;
static void SuffixOf(const VmMsgHeader *h, Char *out) VM_SECT2;
static UInt32 LocalDay(UInt32 utc) VM_SECT2;
static void AddLine(UInt8 kind, UInt16 msg, UInt16 off, UInt16 len, Boolean first, Boolean last, UInt32 day) VM_SECT2;
static void Build(void) VM_SECT2;
static Int16 TextWithMentions(const Char *text, UInt16 len, Coord x, Coord y, Coord hitX) VM_SECT2;
static void DrawLine(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;
static void DrawCounter(void) VM_SECT2;
static void DrawStatus(void) VM_SECT2;
static UInt8 ConnState(void) VM_SECT2;
static UInt32 FirstUnread(void) VM_SECT2;
static void ScrollToNew(void) VM_SECT2;
static void Refresh(Boolean toEnd) VM_SECT2;
static void Send(FormType *form) VM_SECT2;
static void InsertMention(const Char *sender) VM_SECT2;
static void Tapped(UInt16 row, Coord penX) VM_SECT2;
static Boolean AllocLines(void) VM_SECT2;
static void ShowEarlier(void) VM_SECT2;
static Boolean TappedEmoji(UInt16 row, Coord penX) VM_SECT2;
static void MessageMenu(UInt16 msg, const RectangleType *b) VM_SECT2;
static Boolean Hold(UInt16 row, const RectangleType *b, void *ctx) VM_SECT2;

#define MAX_MSGS    40        /* newest messages shown; "Earlier messages" adds as many again */
#define LINES_PER   5         /* lines allowed per message shown, on average */
#define LINE_H      VM_CHIP_H /* chips need the extra 2 px */
#define TIME_X      2
#define TEXT_X      (gTxtLarge ? 30 : 26)   /* clear of "HH:MM" in either font */

#define L_DAY   0
#define L_CHIP  1
#define L_TEXT  2
#define L_NEW   3      /* "── new ──" before the first message unread when the screen opened */
#define L_MORE  4      /* at the top when older messages aren't shown: tap for more */
#define L_REACT 5      /* under a message: its reactions (off, len: in msg_menu's summaries) */
#define NO_NEW  0

typedef struct {
	UInt8  kind;
	UInt8  first;      /* first text line of a message: draw its time */
	UInt8  last;       /* last line of a message: draw its SNR / delivery mark */
	UInt8  suffixLeft; /* ...in the time column, which is free after a message's first line */
	UInt16 msg;        /* message record */
	UInt16 off, len;   /* the text on this line */
	UInt32 day;        /* L_DAY: local day number */
} Line;

static Line   *gLines = NULL;
static UInt16  gCount = 0;
static UInt16  gShow = MAX_MSGS;    /* messages shown (the newest) */
static RowList gList;
static Char    gTitle[VM_NAME_SIZE + 2];
static Coord   gTitleEnd;       /* where the title's tab ends */
static UInt8   gShownState;     /* what DrawStatus showed: ConnState() */
static UInt32  gNewFrom = NO_NEW;   /* unique ID of the first message unread at open */

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

	if (gCount >= gShow * LINES_PER)
		return;
	l = &gLines[gCount++];
	l->kind = kind;
	l->msg = msg;
	l->off = off;
	l->len = len;
	l->first = first;
	l->last = last;
	l->suffixLeft = false;
	l->day = day;
}

static void Build(void)
{
	UInt16 *idx = MemPtrNew(gShow * sizeof(UInt16));
	UInt16 nIdx = 0, i, n = MessagesCount(), width = TextWidth();
	Boolean more = false;
	Int16 react;
	UInt32 day, lastDay = 0;
	Char sender[VM_NAME_SIZE + 2], lastSender[VM_NAME_SIZE + 2], suffix[8];
	FontID old = FntSetFont(vmSmallFont);

	gCount = 0;
	lastSender[0] = 0;
	MsgReactLoad();
	if (!idx) {
		FntSetFont(old);
		return;
	}
	/* the newest gShow of this conversation, oldest first, and whether there are older ones */
	for (i = n; i > 0 && !more; i--) {
		const VmMsgHeader *h = MessagesLock(i - 1);

		if (h) {
			if (ConvEqual(&h->conv, &gAppConv)) {
				if (nIdx < gShow)
					idx[nIdx++] = i - 1;
				else
					more = true;
			}
			MessagesRelease(i - 1);
		}
	}
	if (more && nIdx)
		AddLine(L_MORE, idx[nIdx - 1], 0, 0, false, false, 0);
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
		if (gNewFrom != NO_NEW && MessagesUniqueId(m) == gNewFrom) {
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
		/* the SNR / mark goes after the last line if it fits, else in its time column, else on a line of its own */
		SuffixOf(h, suffix);
		if (suffix[0] && gCount > 0) {
			Line *l = &gLines[gCount - 1];
			Int16 lineW = TxtWidth(text + l->off, l->len, TxtSmallWidth);

			if (lineW + 4 + TxtWidth(suffix, StrLen(suffix), TxtSmallWidth) > (Int16)width) {
				if (!l->first && l->kind == L_TEXT)
					l->suffixLeft = true;
				else
					AddLine(L_TEXT, m, h->textLen, 0, false, false, 0);
			}
		}
		if (gCount > 0)
			gLines[gCount - 1].last = true;
		MessagesRelease(m);
		react = MsgReactSummary(m, &len);
		if (react >= 0)
			AddLine(L_REACT, m, (UInt16)react, len, false, false, 0);
	}
	MemPtrFree(idx);
	FntSetFont(old);
}

/*
 * Draws a line of message text, with each complete @[Name] mention as a chip
 * (MeshTerm's mention chip, @ dropped). A chip is never wider than the raw
 * "@[Name]" it replaces (2 px pads vs three 4 px characters), so the line still
 * fits the width it was wrapped to.
 */
/*
 * A line's text with "@[name]" mentions drawn as chips. With hitX < 0 it
 * draws; otherwise it only lays the line out and returns the index of the
 * plain character under hitX (-1 if none, or a chip), so taps line up with
 * what's drawn.
 */
static Int16 TextWithMentions(const Char *text, UInt16 len, Coord x, Coord y, Coord hitX)
{
	Char name[VM_NAME_SIZE];
	UInt16 i = 0, start = 0, end = 0, n = 0, k;
	Coord w;

	for (;;) {
		/* find the next mention (or the end) */
		for (; i + 2 < len; i++) {
			if (text[i] != '@' || text[i + 1] != '[')
				continue;
			for (end = i + 2; end < len && text[end] != ']'; end++)
				;
			n = end - (i + 2);
			if (end < len && n > 0 && n < sizeof(name))
				break;
		}
		if (i + 2 >= len)
			i = len;
		/* the plain text before it */
		if (hitX < 0) {
			WinDrawChars(text + start, i - start, x, y);
			x += FntCharsWidth(text + start, i - start);
		} else {
			for (k = start; k < i; k++, x += w) {
				w = FntCharWidth(text[k]);
				if (hitX < x + w)
					return hitX >= x ? (Int16)k : -1;
			}
		}
		if (i >= len)
			return -1;
		MemMove(name, text + i + 2, n);
		name[n] = 0;
		w = hitX < 0 ? DrawChip(name, x, y - 1) : ChipWidth(name);
		if (hitX >= 0 && hitX < x + w)
			return -1;
		x += w;
		i = start = end + 1;
	}
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
	} else if (l->kind == L_MORE) {
		DrawSection("tap for earlier messages", x, y, b->extent.x);
	} else if (l->kind == L_DAY) {
		TimSecondsToDateTime(l->day * 86400UL, &dt);
		StrPrintF(buf, "%s %s %u", kDays[DayOfWeek(dt.month, dt.day, dt.year)], kMonths[dt.month - 1], dt.day);
		DrawSection(buf, x, y, b->extent.x);
	} else if (l->kind == L_CHIP) {
		SenderOf(h, buf, sizeof(buf));
		DrawChip(buf, x + 1, b->topLeft.y);
	} else if (l->kind == L_REACT) {
		WinDrawChars(MsgReactText(l->off), l->len, x + TEXT_X, y);
	} else {
		const Char *text = (const Char *)(h + 1) + l->off;

		if (l->first) {
			TimSecondsToDateTime(SessionLocalPalm(h->received), &dt);
			StrPrintF(buf, "%02u:%02u", dt.hour, dt.minute);
			WinDrawChars(buf, StrLen(buf), x + TIME_X, y);
		}
		TextWithMentions(text, l->len, x + TEXT_X, y, -1);
		if (l->last) {
			SuffixOf(h, buf);
			if (buf[0])
				WinDrawChars(buf, StrLen(buf), l->suffixLeft ? x + TIME_X :
				             x + TEXT_X + FntCharsWidth(text, l->len) + (l->len ? 4 : 0), y);
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

static UInt8 ConnState(void)
{
	return SessionIsConnected() ? 1 : SessionConnecting() ? 2 : 0;
}

/* Online or offline at the right of the title; redraws the title if "Connecting..." covered its end. */
static void DrawStatus(void)
{
	Boolean over = TitleStatusLeft() < gTitleEnd;

	gShownState = ConnState();
	TitleConnState("");
	if (over)
		FrmCopyTitle(FrmGetActiveForm(), gTitle);
}

/* The oldest unread message of this conversation, or NO_NEW. */
static UInt32 FirstUnread(void)
{
	UInt16 i, n = MessagesCount();
	UInt32 found = NO_NEW;

	for (i = n; i > 0; i--) {
		const VmMsgHeader *h = MessagesLock(i - 1);

		if (h) {
			if (h->unread && ConvEqual(&h->conv, &gAppConv))
				found = MessagesUniqueId(i - 1);
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
	DrawStatus();       /* offline now if the radio didn't answer */
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

/* An emoji (or its neighbour, they're narrow) under the pen: show it enlarged; a #hashtag: join or open it. */
static Boolean TappedEmoji(UInt16 row, Coord penX)
{
	FormType *form = FrmGetActiveForm();
	const Line *l = &gLines[row];
	const VmMsgHeader *h;
	const Char *text;
	RectangleType b;
	FontID old;
	Int16 k;
	Char c[3], tag[VM_NAME_SIZE];
	UInt16 n = 0, i;
	Boolean isTag = false;

	if (l->kind != L_TEXT)
		return false;
	h = MessagesLock(l->msg);
	if (!h)
		return false;
	FrmGetObjectBounds(form, FrmGetObjectIndex(form, ConvoGadget), &b);
	text = (const Char *)(h + 1) + l->off;
	old = FntSetFont(vmSmallFont);
	k = TextWithMentions(text, l->len, b.topLeft.x + TEXT_X, 0, penX);
	FntSetFont(old);
	if (k >= 0) {
		c[n++] = text[k];
		if (k > 0)
			c[n++] = text[k - 1];
		if (k + 1 < (Int16)l->len)
			c[n++] = text[k + 1];
		isTag = VmHashtagAt(text, l->len, (UInt16)k, tag, sizeof(tag));
	}
	MessagesRelease(l->msg);
	if (isTag) {
		ConvoOpenHashtag(tag);
		return true;
	}
	for (i = 0; i < n; i++)
		if (EmojiShow(c[i]))
			return true;
	return false;
}

/*
 * Tap an emoji to see it enlarged, a failed message of ours to retry it, or
 * someone's channel message to reply to them.
 */
/* Room for the lines of gShow messages. */
static Boolean AllocLines(void)
{
	Line *p = MemPtrNew(sizeof(Line) * gShow * LINES_PER);

	if (!p)
		return false;
	if (gLines)
		MemPtrFree(gLines);
	gLines = p;
	return true;
}

/* Shows MAX_MSGS more older messages, staying at the top. */
static void ShowEarlier(void)
{
	gShow += MAX_MSGS;
	if (gShow > VM_MAX_MESSAGES || !AllocLines()) {
		gShow -= MAX_MSGS;
		return;
	}
	Build();
	RowListSetCount(&gList, gCount);
	RowListScrollTo(&gList, 0);
	RowListDraw(&gList);
}

static void Tapped(UInt16 row, Coord penX)
{
	const VmMsgHeader *h;
	Boolean failed, reply;
	Char sender[VM_NAME_SIZE];

	if (gLines[row].kind == L_MORE) {
		ShowEarlier();
		return;
	}
	if (gLines[row].kind == L_REACT) {
		MsgReactWho(gLines[row].msg);
		return;
	}
	if (TappedEmoji(row, penX))
		return;
	h = MessagesLock(gLines[row].msg);
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

/* The actions popup for a message (b: its row; NULL from the menu), then what it changed. */
static void MessageMenu(UInt16 msg, const RectangleType *b)
{
	UInt16 r = MsgMenuOpen(msg, b, ConvoActionList, ConvoGadget, ConvoField);

	if (r == MSG_MENU_REFRESH) {
		Refresh(false);
		DrawStatus();
	} else if (r == MSG_MENU_TEXT) {
		DrawCounter();
	}
}

static Boolean Hold(UInt16 row, const RectangleType *b, void *ctx)
{
	UInt8 kind = gLines[row].kind;

	if (kind != L_TEXT && kind != L_CHIP && kind != L_REACT)
		return false;
	MessageMenu(gLines[row].msg, b);
	return true;
}

Boolean ConvoFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;

	switch (event->eType) {
	case frmOpenEvent:
		gShow = MAX_MSGS;
		if (!AllocLines())
			return false;
		ConvName(&gAppConv, gTitle, VM_NAME_SIZE);
		gTitleEnd = TitleFit(gTitle);
		FrmCopyTitle(form, gTitle);
		TitleStatusReset();
		RowListInit(&gList, ConvoGadget, ConvoScroll, LINE_H, DrawLine, NULL, NULL);
		gList.hold = Hold;
		gNewFrom = FirstUnread();
		Build();
		MessagesMarkRead(&gAppConv);
		RowListSetCount(&gList, gCount);
		ScrollToNew();
		FrmDrawForm(form);
		RowListDraw(&gList);
		DrawCounter();
		DrawStatus();
		FrmSetFocus(form, FrmGetObjectIndex(form, ConvoField));
		return true;

	case frmCloseEvent:
		if (gLines) {
			MemPtrFree(gLines);
			gLines = NULL;
		}
		MsgReactFree();
		return false;

	case frmUpdateEvent:
		if (!gLines)
			return false;
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			/* e.g. after the on-screen keyboard: redraw everything, not just the transcript */
			FrmDrawForm(form);
			RowListDraw(&gList);
			DrawCounter();
			TitleStatusReset();
			DrawStatus();
			return true;
		}
		Refresh(gList.top + RowListVisibleRows(&gList) >= gCount);   /* follow new messages if at the end */
		DrawStatus();
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

	case penDownEvent:
		if (TitleStatusTapped(event))
			return true;
		break;

	case menuEvent:
		if (event->data.menu.itemID == MainMenuMention) {
			if (ConvoMentionPick(ConvoMentionList, ConvoField, false))
				DrawCounter();
			return true;
		}
		if (event->data.menu.itemID == MainMenuMessage) {
			UInt16 msg;

			if (gLines && MsgMenuNewest(&msg))
				MessageMenu(msg, NULL);
			return true;
		}
		break;

	case keyDownEvent:
		if (event->data.keyDown.chr == linefeedChr) {    /* Graffiti return sends */
			Send(form);
			return true;
		}
		if (event->data.keyDown.chr == '@' && FrmGetFocus(form) == FrmGetObjectIndex(form, ConvoField)) {
			if (ConvoMentionPick(ConvoMentionList, ConvoField, true))
				DrawCounter();
			return true;
		}
		break;

	default:
		break;
	}

	if (gLines && RowListHandleEvent(&gList, event, &tapped)) {
		if (tapped >= 0)
			Tapped((UInt16)tapped, event->screenX);
		return true;
	}
	return false;
}

void ConvoFormIdle(void)
{
	if (gLines) {
		DrawCounter();
		if (ConnState() != gShownState)
			DrawStatus();
	}
}
