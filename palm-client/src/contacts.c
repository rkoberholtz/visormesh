#include <PalmOS.h>
#include "protocol.h"
#include "session.h"
#include "contacts.h"
#include "ui/text.h"

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

Boolean ContactsFindByPrefix(const UInt8 *prefix6, VmContact *out)
{
	UInt16 i, n;
	MemHandle h;
	Boolean found = false;

	if (ContactsOpen() != errNone)
		return false;
	n = DmNumRecords(gDb);
	for (i = 0; i < n && !found; i++) {
		const VmContact *c;

		h = DmQueryRecord(gDb, i);
		if (!h)
			continue;
		c = MemHandleLock(h);
		if (MemCmp(c->pubKey, prefix6, 6) == 0) {
			MemMove(out, c, sizeof(VmContact));
			found = true;
		}
		MemHandleUnlock(h);
	}
	return found;
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
	VmContact copy = *c;

	if (!h)
		return DmGetLastErr();
	TxtUtf8NameToPalm(copy.name, sizeof(copy.name));
	DmWrite(MemHandleLock(h), 0, &copy, sizeof(VmContact));
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, index, true);
	return errNone;
}

Err ContactsRefresh(UInt32 *count)
{
	UInt16 oldCount, i;
	UInt32 lastMod, total;
	Err err;

	err = ContactsOpen();
	if (err != errNone)
		return err;
	oldCount = DmNumRecords(gDb);

	err = SessionGetContacts(0, AddContact, NULL, &total, &lastMod);
	if (count)
		*count = total;

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
