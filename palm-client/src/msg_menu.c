#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "session.h"
#include "messages.h"
#include "contacts.h"
#include "reactions.h"
#include "app.h"
#include "ui/text.h"
#include "ui/emoji.h"
#include "info_fmt.h"
#include "msg_menu.h"

static Int16   Popup(const RectangleType *b, UInt16 listId, UInt16 areaId, Char **items, UInt16 n) VM_SECT3;
static void    NameOf(const VmMsgHeader *h, Char *out) VM_SECT3;
static UInt16  React(UInt16 msg) VM_SECT3;
static UInt16  Reply(UInt16 msg, UInt16 fieldId) VM_SECT3;
static void    SendDm(const Char *name) VM_SECT3;
static void    PathDetails(UInt16 msg) VM_SECT3;
static Boolean Matches(const VmReact *r, const UInt8 *hash, const Char *name, Boolean channel) VM_SECT3;

#define ACT_REACT   0
#define ACT_REPLY   1
#define ACT_DM      2
#define ACT_PATH    3
#define ACT_RETRY   4
#define ACT_MAX     5
#define LIST_X      28
#define LIST_H(n)   ((n) * 11 + 2)   /* stdFont rows and the frame */
#define QUOTE_CHARS 10
#define REACT_MAX   60               /* reactions held for one conversation */
#define POOL_SIZE   400              /* their summaries for the messages shown */
#define KINDS_MAX   6                /* different emoji in one summary */

static VmReact *gReacts = NULL;
static UInt16   gReactCount;
static Char     gPool[POOL_SIZE];
static UInt16   gPoolUsed;

/* Beside the row (below it, or above near the bottom of the transcript), like Home's. */
static Int16 Popup(const RectangleType *b, UInt16 listId, UInt16 areaId, Char **items, UInt16 n)
{
	FormType *form = FrmGetActiveForm();
	ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, listId));
	RectangleType area;
	Coord y;

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, areaId), &area);
	y = b ? b->topLeft.y + b->extent.y : area.topLeft.y + area.extent.y;
	if (y + LIST_H(n) > area.topLeft.y + area.extent.y)
		y = (b ? b->topLeft.y : area.topLeft.y + area.extent.y) - LIST_H(n);
	LstSetListChoices(list, items, n);
	LstSetHeight(list, n);
	LstSetPosition(list, LIST_X, y);
	LstSetSelection(list, noListSelection);
	return LstPopupList(list);
}

/* The sender a channel reaction names: theirs, or ours for our own message. */
static void NameOf(const VmMsgHeader *h, Char *out)
{
	if (h->dir == VM_DIR_OUT)
		StrNCopy(out, SessionSelf()->name, VM_NAME_SIZE - 1);
	else
		StrNCopy(out, h->sender, VM_NAME_SIZE - 1);
	out[VM_NAME_SIZE - 1] = 0;
}

UInt16 MsgMenuOpen(UInt16 msg, const RectangleType *b, UInt16 listId, UInt16 areaId, UInt16 fieldId)
{
	const VmMsgHeader *h = MessagesLock(msg);
	Char *items[ACT_MAX];
	UInt8 acts[ACT_MAX];
	UInt16 n = 0;
	Int16 pick;
	Boolean mine, channel, room, failed;
	Char name[VM_NAME_SIZE];

	if (!h)
		return MSG_MENU_NONE;
	mine = h->dir == VM_DIR_OUT;
	channel = h->conv.type == VM_CONV_CHANNEL;
	room = !channel && !mine && h->sender[0];   /* a room post: rooms have no reactions */
	failed = mine && h->status == VM_ST_FAILED;
	NameOf(h, name);
	MessagesRelease(msg);

	if (!mine && !room) {
		items[n] = "React...";
		acts[n++] = ACT_REACT;
	}
	if (!mine) {
		items[n] = "Reply";
		acts[n++] = ACT_REPLY;
	}
	if (!mine && channel && name[0]) {
		items[n] = "Send DM";
		acts[n++] = ACT_DM;
	}
	items[n] = "Path Details";
	acts[n++] = ACT_PATH;
	if (failed) {
		items[n] = "Retry";
		acts[n++] = ACT_RETRY;
	}
	pick = Popup(b, listId, areaId, items, n);
	if (pick < 0 || pick >= (Int16)n)
		return MSG_MENU_NONE;
	switch (acts[pick]) {
	case ACT_REACT: return React(msg);
	case ACT_REPLY: return Reply(msg, fieldId);
	case ACT_DM:    SendDm(name); break;
	case ACT_PATH:  PathDetails(msg); break;
	case ACT_RETRY:
		SessionRetry(msg);
		return MSG_MENU_REFRESH;
	}
	return MSG_MENU_NONE;
}

Boolean MsgMenuNewest(UInt16 *msg)
{
	UInt16 i;
	const VmMsgHeader *h;
	Boolean found = false;

	for (i = MessagesCount(); i > 0 && !found; i--) {
		h = MessagesLock(i - 1);
		if (!h)
			continue;
		if (h->dir == VM_DIR_IN && ConvEqual(&h->conv, &gAppConv)) {
			*msg = i - 1;
			found = true;
		}
		MessagesRelease(i - 1);
	}
	return found;
}

static UInt16 React(UInt16 msg)
{
	const VmMsgHeader *h;
	UInt8 hash[5];
	Char name[VM_NAME_SIZE], glyph, text[48];
	VmConv conv;
	Err err;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first.", "", "");
		return MSG_MENU_NONE;
	}
	if (!MessageHash(msg, hash)) {
		FrmCustomAlert(SettingsErrorAlert, "Can't react to this message.", "", "");
		return MSG_MENU_NONE;
	}
	glyph = EmojiPick();
	if (!glyph)
		return MSG_MENU_NONE;
	h = MessagesLock(msg);
	if (!h)
		return MSG_MENU_NONE;
	conv = h->conv;
	NameOf(h, name);
	MessagesRelease(msg);
	err = SessionSendReaction(&conv, hash, conv.type == VM_CONV_CHANNEL ? name : "", glyph);
	if (err != errNone) {
		AppErrorText(err, text);
		FrmCustomAlert(SettingsErrorAlert, text, "", "");
	}
	return MSG_MENU_REFRESH;
}

/* '@[Name] "first ten..' at the cursor; no mention in a DM, where it's clear who it's for. */
static UInt16 Reply(UInt16 msg, UInt16 fieldId)
{
	FormType *form = FrmGetActiveForm();
	FieldType *field = FrmGetObjectPtr(form, FrmGetObjectIndex(form, fieldId));
	const VmMsgHeader *h = MessagesLock(msg);
	const Char *t;
	Char buf[VM_NAME_SIZE + QUOTE_CHARS + 12];
	UInt16 n = 0, i;

	if (!h)
		return MSG_MENU_NONE;
	t = (const Char *)(h + 1);
	if (h->conv.type == VM_CONV_CHANNEL && h->sender[0]) {
		StrPrintF(buf, "@[%s] ", h->sender);
		n = StrLen(buf);
	}
	buf[n++] = '"';
	for (i = 0; i < h->textLen && i < QUOTE_CHARS; i++)
		buf[n++] = (t[i] == '\n' || t[i] == '\t') ? ' ' : t[i];
	if (h->textLen > QUOTE_CHARS) {
		buf[n++] = '.';
		buf[n++] = '.';
	}
	buf[n++] = '"';
	buf[n++] = ' ';
	buf[n] = 0;
	MessagesRelease(msg);
	FrmSetFocus(form, FrmGetObjectIndex(form, fieldId));
	FldInsert(field, buf, n);
	return MSG_MENU_TEXT;
}

static void SendDm(const Char *name)
{
	VmContact c;
	VmConv conv;
	Char msg[VM_NAME_SIZE + 48];

	if (!ContactsFindByName(name, &c)) {
		StrPrintF(msg, "No contact named %s yet. It's added once their advert is heard.", name);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
		return;
	}
	MemSet(&conv, sizeof(conv), 0);
	conv.type = VM_CONV_DIRECT;
	MemMove(conv.key, c.pubKey, sizeof(conv.key));
	AppOpenConversation(&conv);
}

static void PathDetails(UInt16 msg)
{
	const VmMsgHeader *h = MessagesLock(msg);
	VmMsgHeader hdr;
	VmMsgExtra ex;
	Boolean hasEx;
	Char text[300], snr[8];
	UInt16 hops;

	if (!h)
		return;
	hdr = *h;
	MessagesRelease(msg);
	hasEx = MessagesExtra(msg, &ex);
	text[0] = 0;
	if (hdr.dir == VM_DIR_OUT) {
		InfoClock(text, sizeof(text), "Sent", hdr.timestamp);
		StrCat(text, "\n");
		switch (hdr.status) {
		case VM_ST_PENDING: StrCat(text, "Waiting for the radio"); break;
		case VM_ST_SENT:    StrCat(text, hdr.conv.type == VM_CONV_CHANNEL ? "Sent (channels report no delivery)" : "Sent, not confirmed yet"); break;
		case VM_ST_ACKED:   StrCat(text, "Delivered"); break;
		case VM_ST_FAILED:  StrCat(text, "Not delivered"); break;
		}
		FrmCustomAlert(PathAlert, text, "", "");
		return;
	}
	InfoClock(text, sizeof(text), "Sent", hdr.timestamp);     /* the sender's clock */
	StrCat(text, "\n");
	InfoClock(text, sizeof(text), "Got", hdr.received);
	StrCat(text, "\n");
	hops = VM_PATH_HOPS(hdr.pathLen);
	if (hdr.pathLen == VM_PATH_UNKNOWN)
		StrCat(text, "Direct route");
	else if (hops == 0)
		StrCat(text, "Heard directly (no repeaters)");
	else
		StrPrintF(text + StrLen(text), "Flood, %u hop%s", hops, hops == 1 ? "" : "s");
	TxtFormatSnr(snr, hdr.snrQuarterDb);
	StrPrintF(text + StrLen(text), ", SNR %s\n", snr);
	if (hdr.pathLen == VM_PATH_UNKNOWN || hops == 0) {
		/* nothing more to show */
	} else if (!hasEx || !ex.hasPath || ex.hashSize == 0 || ex.hashSize > 3) {
		StrCat(text, "Repeaters not recorded");
	} else {
		StrCat(text, "Via");
		InfoVia(text, sizeof(text), ex.path, ex.pathBytes, ex.hashSize, ex.hops);
	}
	FrmCustomAlert(PathAlert, text, "", "");
}

void MsgReactLoad(void)
{
	if (!gReacts)
		gReacts = MemPtrNew(sizeof(VmReact) * REACT_MAX);
	gReactCount = gReacts ? ReactionsFor(&gAppConv, gReacts, REACT_MAX) : 0;
	gPoolUsed = 0;
}

void MsgReactFree(void)
{
	if (gReacts) {
		MemPtrFree(gReacts);
		gReacts = NULL;
	}
	gReactCount = 0;
}

static Boolean Matches(const VmReact *r, const UInt8 *hash, const Char *name, Boolean channel)
{
	return MemCmp(r->hash, hash, sizeof(r->hash)) == 0 && (!channel || StrCompare(r->target, name) == 0);
}

Int16 MsgReactSummary(UInt16 msg, UInt16 *len)
{
	const VmMsgHeader *h;
	UInt8 hash[5];
	Char name[VM_NAME_SIZE], kinds[KINDS_MAX], buf[KINDS_MAX * 5];
	UInt16 counts[KINDS_MAX], nKinds = 0, i, k, n = 0;
	Boolean channel;

	*len = 0;
	if (gReactCount == 0 || !MessageHash(msg, hash))
		return -1;
	h = MessagesLock(msg);
	if (!h)
		return -1;
	channel = h->conv.type == VM_CONV_CHANNEL;
	NameOf(h, name);
	MessagesRelease(msg);
	for (i = 0; i < gReactCount; i++) {
		if (!Matches(&gReacts[i], hash, name, channel))
			continue;
		for (k = 0; k < nKinds && kinds[k] != gReacts[i].glyph; k++)
			;
		if (k == nKinds) {
			if (nKinds == KINDS_MAX)
				continue;
			kinds[nKinds] = gReacts[i].glyph;
			counts[nKinds++] = 0;
		}
		counts[k]++;
	}
	for (k = 0; k < nKinds; k++) {
		if (k)
			buf[n++] = ' ';
		buf[n++] = kinds[k];
		if (counts[k] > 1) {
			StrPrintF(buf + n, "%u", counts[k]);
			n += StrLen(buf + n);
		}
	}
	if (n == 0 || gPoolUsed + n > POOL_SIZE)
		return -1;
	MemMove(gPool + gPoolUsed, buf, n);
	gPoolUsed += n;
	*len = n;
	return (Int16)(gPoolUsed - n);
}

const Char *MsgReactText(UInt16 off)
{
	return gPool + off;
}

/* "Thumbs up: Bob, you" per emoji. */
void MsgReactWho(UInt16 msg)
{
	const VmMsgHeader *h;
	UInt8 hash[5];
	Char name[VM_NAME_SIZE], text[300], label[EMOJI_NAME_SIZE], done[KINDS_MAX];
	UInt16 i, j, nDone = 0, k;
	Boolean channel, first;
	Char glyph;

	if (!MessageHash(msg, hash))
		return;
	h = MessagesLock(msg);
	if (!h)
		return;
	channel = h->conv.type == VM_CONV_CHANNEL;
	NameOf(h, name);
	MessagesRelease(msg);
	text[0] = 0;
	for (i = 0; i < gReactCount && nDone < KINDS_MAX; i++) {
		if (!Matches(&gReacts[i], hash, name, channel))
			continue;
		glyph = gReacts[i].glyph;
		for (k = 0; k < nDone && done[k] != glyph; k++)
			;
		if (k < nDone)
			continue;
		done[nDone++] = glyph;
		if (!EmojiName(glyph, label))
			StrCopy(label, "Emoji");
		if (text[0])
			StrCat(text, "\n");
		StrCat(text, label);
		StrCat(text, ":");
		first = true;
		for (j = i; j < gReactCount; j++) {
			const VmReact *r = &gReacts[j];

			if (r->glyph != glyph || !Matches(r, hash, name, channel))
				continue;
			if (StrLen(text) + VM_NAME_SIZE + 3 >= sizeof(text))
				break;
			StrCat(text, first ? " " : ", ");
			StrCat(text, r->mine ? "you" : r->from[0] ? r->from : "them");
			first = false;
		}
	}
	if (text[0])
		FrmCustomAlert(ReactionsAlert, text, "", "");
}
