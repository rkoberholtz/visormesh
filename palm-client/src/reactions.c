#include <PalmOS.h>
#include "db.h"
#include "channels.h"
#include "reaction_fmt.h"
#include "rxpath.h"
#include "ui/text.h"
#include "ui/glyphs.h"
#include "reactions.h"

static Err     Open(void) VM_SECT4;
static void    ChannelKey(VmConv *conv) VM_SECT4;
static Boolean SameConv(const VmConv *a, const VmConv *b) VM_SECT4;
static void    Store(const VmReact *r) VM_SECT4;
static Char    GlyphOf(const UInt8 *emoji, UInt16 len) VM_SECT4;

static Char DB_NAME[] = "VisorMeshReactions";
#define DB_TYPE 'reac'

static DmOpenRef gDb = 0;

static Err Open(void)
{
	return gDb ? errNone : DbOpen(DB_NAME, DB_TYPE, &gDb);
}

void ReactionsClose(void)
{
	if (gDb) {
		DmCloseDatabase(gDb);
		gDb = 0;
	}
}

/* A channel conversation's secret prefix, so reactions follow the channel when its slot changes. */
static void ChannelKey(VmConv *conv)
{
	VmChannel c;

	MemSet(conv->key, sizeof(conv->key), 0);
	if (ChannelsFindSlot(conv->channel, &c))
		MemMove(conv->key, c.secret, sizeof(conv->key));
}

static Boolean SameConv(const VmConv *a, const VmConv *b)
{
	return a->type == b->type && MemCmp(a->key, b->key, sizeof(a->key)) == 0;
}

/* Adds r unless it's already there (a reaction can be heard more than once). */
static void Store(const VmReact *r)
{
	UInt16 i, n, at = dmMaxRecordIndex;
	MemHandle h;
	const VmReact *p;
	Boolean dup = false;

	if (Open() != errNone)
		return;
	n = DmNumRecords(gDb);
	for (i = 0; i < n && !dup; i++) {
		h = DmQueryRecord(gDb, i);
		if (!h)
			continue;
		p = MemHandleLock(h);
		dup = SameConv(&p->conv, &r->conv) && MemCmp(p->hash, r->hash, sizeof(r->hash)) == 0 &&
		      p->glyph == r->glyph && p->mine == r->mine && StrCompare(p->from, r->from) == 0;
		MemHandleUnlock(h);
	}
	if (dup)
		return;
	while (DmNumRecords(gDb) >= VM_MAX_REACTIONS)
		DmRemoveRecord(gDb, 0);
	h = DmNewRecord(gDb, &at, sizeof(VmReact));
	if (!h)
		return;
	DmWrite(MemHandleLock(h), 0, r, sizeof(VmReact));
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, at, true);
}

/* The emoji as the Visor shows it: one glyph, the generic smiley for ones the font lacks. */
static Char GlyphOf(const UInt8 *emoji, UInt16 len)
{
	Char buf[8];
	UInt16 n = TxtUtf8ToPalm(emoji, len, buf, sizeof(buf));

	return n ? buf[0] : GLYPH_EMOJI;
}

Boolean ReactionsTake(const VmMessage *m, UInt32 now)
{
	VmReaction rx;
	VmReact r;
	const UInt8 *body = m->text;
	UInt16 bodyLen = m->textLen, n;

	if (m->txtType != VM_TXT_TYPE_PLAIN || m->hasSigner)    /* rooms have no reactions */
		return false;
	if (m->isChannel)
		body = VmReactChannelBody(m->text, m->textLen, &bodyLen);
	if (!VmReactParse(m->isChannel, body, bodyLen, &rx))
		return false;
	MemSet(&r, sizeof(r), 0);
	if (m->isChannel) {
		r.conv.type = VM_CONV_CHANNEL;
		r.conv.channel = m->channelIdx;
		ChannelKey(&r.conv);
		n = TxtUtf8ToPalm(rx.target, rx.targetLen, r.target, sizeof(r.target) - 1);
		r.target[n] = 0;
		/* the reactor: the "Name" before the ':' */
		for (n = 0; n < m->textLen && m->text[n] != ':'; n++)
			;
		n = TxtUtf8ToPalm(m->text, n, r.from, sizeof(r.from) - 1);
		r.from[n] = 0;
	} else {
		r.conv.type = VM_CONV_DIRECT;
		MemMove(r.conv.key, m->senderPrefix, sizeof(r.conv.key));
	}
	MemMove(r.hash, rx.hash, sizeof(r.hash));
	r.glyph = GlyphOf(rx.emoji, rx.emojiLen);
	r.time = now;
	Store(&r);
	return true;
}

void ReactionsAddMine(const VmConv *conv, const UInt8 *hash, const Char *target, Char glyph, UInt32 now)
{
	VmReact r;

	MemSet(&r, sizeof(r), 0);
	r.conv = *conv;
	if (conv->type == VM_CONV_CHANNEL)
		ChannelKey(&r.conv);
	MemMove(r.hash, hash, sizeof(r.hash));
	r.mine = 1;
	r.glyph = glyph;
	StrNCopy(r.target, target, sizeof(r.target) - 1);
	r.time = now;
	Store(&r);
}

UInt16 ReactionsFor(const VmConv *conv, VmReact *out, UInt16 max)
{
	VmConv want = *conv;
	UInt16 i, n, found = 0;
	MemHandle h;
	const VmReact *p;

	if (Open() != errNone)
		return 0;
	if (want.type == VM_CONV_CHANNEL)
		ChannelKey(&want);
	n = DmNumRecords(gDb);
	/* the newest max, oldest first */
	for (i = n; i > 0 && found < max; i--) {
		h = DmQueryRecord(gDb, i - 1);
		if (!h)
			continue;
		p = MemHandleLock(h);
		if (SameConv(&p->conv, &want))
			out[found++] = *p;
		MemHandleUnlock(h);
	}
	for (i = 0; i < found / 2; i++) {
		VmReact t = out[i];

		out[i] = out[found - 1 - i];
		out[found - 1 - i] = t;
	}
	return found;
}

Boolean MessageHash(UInt16 index, UInt8 *hash)
{
	VmMsgExtra ex;
	const VmMsgHeader *h;
	UInt8 utf8[VM_MAX_FRAME];
	UInt16 n, len, start = 0;
	Boolean channel;
	UInt32 ts;

	if (MessagesExtra(index, &ex)) {
		if (!ex.hasHash)
			return false;
		MemMove(hash, ex.hash, sizeof(ex.hash));
		return true;
	}
	h = MessagesLock(index);
	if (!h)
		return false;
	len = h->textLen;
	n = TxtPalmToUtf8((const Char *)(h + 1), len, utf8, sizeof(utf8));
	channel = h->conv.type == VM_CONV_CHANNEL;
	ts = h->timestamp;
	MessagesRelease(index);
	if (n == 0 && len)
		return false;
	/* a channel message is stored without its "Name: ", so the hashed body is the text, trimmed */
	if (channel) {
		while (start < n && (utf8[start] == ' ' || utf8[start] == '\t'))
			start++;
		while (n > start && (utf8[n - 1] == ' ' || utf8[n - 1] == '\t'))
			n--;
	}
	VmReactHash(utf8 + start, n - start, ts, hash);
	return true;
}

void MessageExtraFor(const VmMessage *m, UInt32 now, VmMsgExtra *ex)
{
	const UInt8 *body = m->text;
	UInt16 bodyLen = m->textLen;
	VmRxLog rx;

	MemSet(ex, sizeof(*ex), 0);
	if (m->isChannel)
		body = VmReactChannelBody(m->text, m->textLen, &bodyLen);
	VmReactHash(body, bodyLen, m->timestamp, ex->hash);
	ex->hasHash = 1;
	if (RxPathTake(m->isChannel, m->pathLen, m->isChannel ? NULL : m->senderPrefix, now, &rx)) {
		ex->hasPath = 1;
		ex->hashSize = rx.hashSize;
		ex->hops = rx.hops;
		ex->pathBytes = rx.pathBytes;
		MemMove(ex->path, rx.path, rx.pathBytes);
	}
}
