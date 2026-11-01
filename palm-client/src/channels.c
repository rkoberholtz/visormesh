#include <PalmOS.h>
#include "protocol.h"
#include "session.h"
#include "channels.h"

#define DB_NAME     "VisorMeshChannels"
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

/* A slot with no name and an all-zero secret is unused. */
static Boolean InUse(const VmChannel *c)
{
	UInt16 i;

	if (c->name[0])
		return true;
	for (i = 0; i < VM_CHANNEL_SECRET_SIZE; i++)
		if (c->secret[i])
			return true;
	return false;
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
