#include <PalmOS.h>
#include "protocol.h"
#include "ui/text.h"
#include "messages.h"

/* In the second code segment: see sections.h for the rules. */
static Err Append(const VmMsgHeader *hdr, const Char *text, UInt16 *index) VM_SECT2;
static void WriteHeader(UInt16 i, const VmMsgHeader *hdr) VM_SECT2;
static Boolean ReadHeader(UInt16 i, VmMsgHeader *hdr) VM_SECT2;

/* a data array, not a literal: this module is in the second code segment (sections.h) */
static Char DB_NAME[] = "VisorMeshMessages";
#define DB_TYPE     'msgs'
#define DB_CREATOR  'VMsh'
#define TEXT_MAX    (VM_MAX_FRAME + 1)

static DmOpenRef gDb = 0;

Err MessagesOpen(void)
{
	LocalID id;
	UInt16 attrs;
	Err err;

	if (gDb)
		return errNone;
	id = DmFindDatabase(0, DB_NAME);
	if (!id) {
		err = DmCreateDatabase(0, DB_NAME, DB_CREATOR, DB_TYPE, false);
		if (err != errNone)
			return err;
		id = DmFindDatabase(0, DB_NAME);
		if (!id)
			return DmGetLastErr();
		DmDatabaseInfo(0, id, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		attrs |= dmHdrAttrBackup;
		DmSetDatabaseInfo(0, id, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	gDb = DmOpenDatabase(0, id, dmModeReadWrite);
	return gDb ? errNone : DmGetLastErr();
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

Boolean ConvEqual(const VmConv *a, const VmConv *b)
{
	if (a->type != b->type)
		return false;
	if (a->type == VM_CONV_CHANNEL)
		return a->channel == b->channel;
	return MemCmp(a->key, b->key, 6) == 0;
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
	Boolean changed = false;

	if (!gDb)
		return false;
	n = DmNumRecords(gDb);
	for (i = 0; i < n; i++) {
		if (ReadHeader(i, &hdr) && hdr.dir == VM_DIR_OUT && hdr.status == VM_ST_SENT &&
		    hdr.ack != 0 && now > hdr.ackDeadline) {
			hdr.status = VM_ST_FAILED;
			WriteHeader(i, &hdr);
			changed = true;
		}
	}
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
