#include <PalmOS.h>
#include "protocol.h"
#include "session.h"
#include "contacts.h"

#define DB_NAME     "VisorMeshContacts"
#define DB_TYPE     'cont'
#define DB_CREATOR  'VMsh'

static DmOpenRef gDb = 0;

Err ContactsOpen(void)
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

void ContactsClose(void)
{
	if (gDb) {
		DmCloseDatabase(gDb);
		gDb = 0;
	}
}

UInt16 ContactsCount(void)
{
	return gDb ? DmNumRecords(gDb) : 0;
}

Boolean ContactsGet(UInt16 index, VmContact *out)
{
	MemHandle h;

	if (!gDb || index >= DmNumRecords(gDb))
		return false;
	h = DmQueryRecord(gDb, index);
	if (!h)
		return false;
	MemMove(out, MemHandleLock(h), sizeof(VmContact));
	MemHandleUnlock(h);
	return true;
}

static Int16 CompareByName(void *a, void *b, Int16 other, SortRecordInfoPtr ia, SortRecordInfoPtr ib,
                           MemHandle appInfo)
{
	return StrCaselessCompare(((VmContact *)a)->name, ((VmContact *)b)->name);
}

/* SessionGetContacts callback: appends one record. */
static Err AddContact(const VmContact *c, void *ctx)
{
	UInt16 index = dmMaxRecordIndex;
	MemHandle h = DmNewRecord(gDb, &index, sizeof(VmContact));

	if (!h)
		return DmGetLastErr();
	DmWrite(MemHandleLock(h), 0, c, sizeof(VmContact));
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, index, true);
	return errNone;
}

Err ContactsRefresh(UInt32 *count)
{
	UInt16 oldCount, i;
	UInt32 lastMod;
	Err err;

	err = ContactsOpen();
	if (err != errNone)
		return err;
	oldCount = DmNumRecords(gDb);

	err = SessionGetContacts(0, AddContact, NULL, count, &lastMod);

	if (err != errNone) {
		/* drop what this attempt added; keep the previous list */
		for (i = DmNumRecords(gDb); i > oldCount; i--)
			DmRemoveRecord(gDb, i - 1);
		return err;
	}
	for (i = 0; i < oldCount; i++)
		DmRemoveRecord(gDb, 0);
	DmQuickSort(gDb, CompareByName, 0);
	return errNone;
}
