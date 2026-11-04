#include <PalmOS.h>
#include "db.h"
#include "protocol.h"
#include "ui/text.h"
#include "messages.h"
#include "contacts.h"
#include "channels.h"

/* In the second code segment: see sections.h for the rules. */
static Err Append(const VmMsgHeader *hdr, const Char *text, UInt16 *index) VM_SECT2;
static void WriteHeader(UInt16 i, const VmMsgHeader *hdr) VM_SECT2;
static Boolean ReadHeader(UInt16 i, VmMsgHeader *hdr) VM_SECT2;
static void TagChannel(VmConv *conv) VM_SECT2;

/* a data array, not a literal: this module is in the second code segment (sections.h) */
static Char DB_NAME[] = "VisorMeshMessages";
#define DB_TYPE     'msgs'
#define TEXT_MAX    (VM_MAX_FRAME + 1)

static DmOpenRef gDb = 0;
/*
 * The earliest ack deadline of a DM still waiting, so the check each poll
 * only scans the store when one can have passed. 1 until the first scan
 * (DMs from before this run may be waiting); 0: none waiting.
 */
static UInt32 gNextAckDeadline = 1;

Err MessagesOpen(void)
{
	return gDb ? errNone : DbOpen(DB_NAME, DB_TYPE, &gDb);
}

void MessagesClose(void)
{
	if (gDb) {
		DmCloseDatabase(gDb);
		gDb = 0;
	}
}

UInt16 MessagesCount(void)
{
	return gDb ? DmNumRecords(gDb) : 0;
}

const VmMsgHeader *MessagesLock(UInt16 i)
{
	MemHandle h = gDb ? DmQueryRecord(gDb, i) : NULL;

	return h ? (const VmMsgHeader *)MemHandleLock(h) : NULL;
}

void MessagesRelease(UInt16 i)
{
	MemHandle h = gDb ? DmQueryRecord(gDb, i) : NULL;

	if (h)
		MemHandleUnlock(h);
}

UInt32 MessagesUniqueId(UInt16 i)
{
	UInt32 id;

	if (!gDb || DmRecordInfo(gDb, i, NULL, &id, NULL) != errNone)
		return 0;
	return id;
}

Boolean ConvEqual(const VmConv *a, const VmConv *b)
{
	if (a->type != b->type)
		return false;
	if (a->type == VM_CONV_CHANNEL)
		return a->channel == b->channel;
	return MemCmp(a->key, b->key, 6) == 0;
}

/* Notes the channel's secret prefix on a channel message, for MessagesRefileChannels. */
static void TagChannel(VmConv *conv)
{
	VmChannel c;

	MemSet(conv->key, sizeof(conv->key), 0);
	if (ChannelsFindSlot(conv->channel, &c))
		MemMove(conv->key, c.secret, sizeof(conv->key));
}

static Err Append(const VmMsgHeader *hdr, const Char *text, UInt16 *index)
{
	UInt16 i = dmMaxRecordIndex;
	UInt32 size = sizeof(VmMsgHeader) + hdr->textLen + 1;
	MemHandle h;
	UInt8 *p;

	while (DmNumRecords(gDb) >= VM_MAX_MESSAGES)
		DmRemoveRecord(gDb, 0);
	h = DmNewRecord(gDb, &i, size);
	if (!h)
		return DmGetLastErr();
	p = MemHandleLock(h);
	DmWrite(p, 0, hdr, sizeof(VmMsgHeader));
	DmWrite(p, sizeof(VmMsgHeader), text, hdr->textLen);
	DmSet(p, sizeof(VmMsgHeader) + hdr->textLen, 1, 0);
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, i, true);
	if (index)
		*index = i;
	return errNone;
}

Err MessagesAddIncoming(const VmMessage *m, UInt32 now)
{
	VmMsgHeader hdr;
	Char text[TEXT_MAX];
	UInt16 len, skip = 0, nameLen = 0;
	Err err = MessagesOpen();

	if (err != errNone)
		return err;
	MemSet(&hdr, sizeof(hdr), 0);
	len = TxtUtf8ToPalm(m->text, m->textLen, text, sizeof(text));
	if (m->isChannel) {
		hdr.conv.type = VM_CONV_CHANNEL;
		hdr.conv.channel = m->channelIdx;
		TagChannel(&hdr.conv);
		nameLen = TxtSplitSender(text, len);
		if (nameLen) {
			if (nameLen > VM_NAME_SIZE - 1)
				nameLen = VM_NAME_SIZE - 1;
			MemMove(hdr.sender, text, nameLen);
			skip = nameLen + 2;
		}
	} else {
		hdr.conv.type = VM_CONV_DIRECT;
		MemMove(hdr.conv.key, m->senderPrefix, 6);
		if (m->hasSigner) {           /* a room post: file it under its author */
			VmContact author;

			if (ContactsFindByPrefixN(m->signer, 4, &author))
				StrNCopy(hdr.sender, author.name, VM_NAME_SIZE - 1);
			else
				StrPrintF(hdr.sender, "%02x%02x%02x%02x", m->signer[0], m->signer[1], m->signer[2], m->signer[3]);
		}
	}
	hdr.dir = VM_DIR_IN;
	hdr.unread = 1;
	hdr.snrQuarterDb = m->snrQuarterDb;
	hdr.pathLen = m->pathLen;
	hdr.timestamp = m->timestamp;
	hdr.received = now;
	hdr.textLen = len - skip;
	return Append(&hdr, text + skip, NULL);
}

Err MessagesAddOutgoing(const VmConv *conv, const Char *text, UInt32 now, UInt16 *index)
{
	VmMsgHeader hdr;
	Err err = MessagesOpen();

	if (err != errNone)
		return err;
	MemSet(&hdr, sizeof(hdr), 0);
	hdr.conv = *conv;
	if (conv->type == VM_CONV_CHANNEL)
		TagChannel(&hdr.conv);
	hdr.dir = VM_DIR_OUT;
	hdr.status = VM_ST_PENDING;
	hdr.timestamp = now;
	hdr.received = now;
	hdr.textLen = StrLen(text);
	return Append(&hdr, text, index);
}

/* Rewrites part of a record's header in place. */
static void WriteHeader(UInt16 i, const VmMsgHeader *hdr)
{
	MemHandle h = DmGetRecord(gDb, i);

	if (!h)
		return;
	DmWrite(MemHandleLock(h), 0, hdr, sizeof(VmMsgHeader));
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, i, true);
}

static Boolean ReadHeader(UInt16 i, VmMsgHeader *hdr)
{
	const VmMsgHeader *p = MessagesLock(i);

	if (!p)
		return false;
	MemMove(hdr, p, sizeof(VmMsgHeader));
	MessagesRelease(i);
	return true;
}

void MessagesSetStatus(UInt16 index, UInt8 status, UInt32 ack, UInt32 ackDeadline)
{
	VmMsgHeader hdr;

	if (!gDb || !ReadHeader(index, &hdr))
		return;
	hdr.status = status;
	hdr.ack = ack;
	hdr.ackDeadline = ackDeadline;
	WriteHeader(index, &hdr);
	if (status == VM_ST_SENT && ack && (!gNextAckDeadline || ackDeadline < gNextAckDeadline))
		gNextAckDeadline = ackDeadline;
}

void MessagesBumpAttempt(UInt16 index)
{
	VmMsgHeader hdr;

	if (!gDb || !ReadHeader(index, &hdr))
		return;
	if (hdr.attempt < 3)
		hdr.attempt++;
	WriteHeader(index, &hdr);
}

Boolean MessagesAcked(UInt32 ack)
{
	VmMsgHeader hdr;
	UInt16 i;

	if (!gDb || ack == 0)
		return false;
	for (i = DmNumRecords(gDb); i > 0; i--) {
		if (ReadHeader(i - 1, &hdr) && hdr.dir == VM_DIR_OUT && hdr.ack == ack &&
		    (hdr.status == VM_ST_SENT || hdr.status == VM_ST_FAILED)) {
			hdr.status = VM_ST_ACKED;
			WriteHeader(i - 1, &hdr);
			return true;
		}
	}
	return false;
}

Boolean MessagesExpireAcks(UInt32 now)
{
	VmMsgHeader hdr;
	UInt16 i, n;
	UInt32 next = 0;
	Boolean changed = false;

	if (!gDb || !gNextAckDeadline || now <= gNextAckDeadline)
		return false;
	n = DmNumRecords(gDb);
	for (i = 0; i < n; i++) {
		if (!ReadHeader(i, &hdr) || hdr.dir != VM_DIR_OUT || hdr.status != VM_ST_SENT || hdr.ack == 0)
			continue;
		if (now > hdr.ackDeadline) {
			hdr.status = VM_ST_FAILED;
			WriteHeader(i, &hdr);
			changed = true;
		} else if (!next || hdr.ackDeadline < next) {
			next = hdr.ackDeadline;
		}
	}
	gNextAckDeadline = next;
	return changed;
}

void MessagesMarkRead(const VmConv *conv)
{
	VmMsgHeader hdr;
	UInt16 i, n;

	if (!gDb)
		return;
	n = DmNumRecords(gDb);
	for (i = 0; i < n; i++) {
		if (ReadHeader(i, &hdr) && hdr.unread && ConvEqual(&hdr.conv, conv)) {
			hdr.unread = 0;
			WriteHeader(i, &hdr);
		}
	}
}

void MessagesRefileChannels(MessagesConvFn fn, void *ctx)
{
	VmMsgHeader hdr;
	VmConv was;
	UInt16 i, n;

	if (MessagesOpen() != errNone)
		return;
	n = DmNumRecords(gDb);
	for (i = 0; i < n; i++) {
		if (!ReadHeader(i, &hdr) || hdr.conv.type != VM_CONV_CHANNEL)
			continue;
		was = hdr.conv;
		fn(&hdr.conv, ctx);
		if (MemCmp(&was, &hdr.conv, sizeof(VmConv)) != 0)
			WriteHeader(i, &hdr);
	}
}
