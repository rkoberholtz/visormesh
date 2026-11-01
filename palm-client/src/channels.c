#include <PalmOS.h>
#include "protocol.h"
#include "session.h"
#include "channels.h"
#include "ui/text.h"

/* In the second code segment: see sections.h for the rules. */
static Boolean InUse(const VmChannel *c) VM_SECT2;

/* data arrays, not literals: this module is in the second code segment (sections.h) */
static Char DB_NAME[] = "VisorMeshChannels";
static Char kEmpty[] = "";
#define DB_TYPE     'chan'
#define DB_CREATOR  'VMsh'

static DmOpenRef gDb = 0;

Err ChannelsOpen(void)
{
	LocalID id;
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
	}
	gDb = DmOpenDatabase(0, id, dmModeReadWrite);
	return gDb ? errNone : DmGetLastErr();
}

void ChannelsClose(void)
{
	if (gDb) {
		DmCloseDatabase(gDb);
		gDb = 0;
	}
}

UInt16 ChannelsCount(void)
{
	return gDb ? DmNumRecords(gDb) : 0;
}

Boolean ChannelsGet(UInt16 i, VmChannel *out)
{
	MemHandle h;

	if (!gDb || i >= DmNumRecords(gDb))
		return false;
	h = DmQueryRecord(gDb, i);
	if (!h)
		return false;
	MemMove(out, MemHandleLock(h), sizeof(VmChannel));
	MemHandleUnlock(h);
	return true;
}

Boolean ChannelsFindSlot(UInt8 slot, VmChannel *out)
{
	UInt16 i, n = ChannelsCount();

	for (i = 0; i < n; i++)
		if (ChannelsGet(i, out) && out->idx == slot)
			return true;
	return false;
}

/*
 * A slot with an empty name is unused. The secret can't be used to tell: the
 * firmware fills unused slots' replies from a reused buffer, so the bytes after
 * the empty name are left over from the previous reply.
 */
static Boolean InUse(const VmChannel *c)
{
	return c->name[0] != 0;
}

Err ChannelsRefresh(UInt8 maxChannels)
{
	VmChannel c;
	UInt16 oldCount, i, index;
	UInt8 slot;
	MemHandle h;
	Err err = ChannelsOpen();

	if (err != errNone)
		return err;
	oldCount = DmNumRecords(gDb);
	for (slot = 0; slot < maxChannels; slot++) {
		err = SessionGetChannel(slot, &c);
		if (err == vmErrRadio)
			break;              /* NOT_FOUND: past the last slot */
		if (err != errNone)
			goto fail;
		if (!InUse(&c))
			continue;
		TxtUtf8NameToPalm(c.name, sizeof(c.name));
		index = dmMaxRecordIndex;
		h = DmNewRecord(gDb, &index, sizeof(VmChannel));
		if (!h) {
			err = DmGetLastErr();
			goto fail;
		}
		DmWrite(MemHandleLock(h), 0, &c, sizeof(VmChannel));
		MemHandleUnlock(h);
		DmReleaseRecord(gDb, index, true);
	}
	for (i = 0; i < oldCount; i++)
		DmRemoveRecord(gDb, 0);
	return errNone;

fail:
	for (i = DmNumRecords(gDb); i > oldCount; i--)
		DmRemoveRecord(gDb, i - 1);
	return err;
}

Int16 ChannelsFreeSlot(UInt8 maxChannels)
{
	VmChannel c;
	UInt8 slot;

	for (slot = 0; slot < maxChannels; slot++)
		if (!ChannelsFindSlot(slot, &c))
			return slot;
	return -1;
}

Err ChannelsSet(UInt8 slot, const Char *name, const UInt8 *secret, UInt8 maxChannels)
{
	UInt8 frame[VM_HEADER_LEN + 2 + VM_NAME_SIZE + VM_CHANNEL_SECRET_SIZE];
	UInt8 utf8[VM_NAME_SIZE];
	UInt16 n = TxtPalmToUtf8(name, StrLen(name), utf8, VM_NAME_SIZE - 1);
	UInt16 len;
	Err err;

	if (*name && n == 0)
		return vmErrTooLong;
	len = VmBuildSetChannel(frame, sizeof(frame), slot, utf8, n, secret);
	err = SessionCommand(frame, len);
	if (err == errNone)
		err = ChannelsRefresh(maxChannels);
	return err;
}

Err ChannelsRemove(UInt8 slot, UInt8 maxChannels)
{
	UInt8 zero[VM_CHANNEL_SECRET_SIZE];

	MemSet(zero, sizeof(zero), 0);
	return ChannelsSet(slot, kEmpty, zero, maxChannels);
}
