#include <PalmOS.h>
#include "db.h"
#include "protocol.h"
#include "session.h"
#include "contacts.h"
#include "ui/text.h"

/* In the second code segment: see sections.h for the rules. */
static Int16 CompareByName(void *a, void *b, Int16 other, SortRecordInfoPtr ia, SortRecordInfoPtr ib, MemHandle appInfo) VM_SECT2;
static Err AddContact(const VmContact *c, void *ctx) VM_SECT2;
static Err UpsertContact(const VmContact *c, void *ctx) VM_SECT4;

/* a data array, not a literal: this module is in the second code segment (sections.h) */
static Char DB_NAME[] = "VisorMeshContacts";
#define DB_TYPE     'cont'

static DmOpenRef gDb = 0;
static UInt32 gLastMod = 0;    /* newest lastmod of the last download in this run; 0: none yet */

Err ContactsOpen(void)
{
	return gDb ? errNone : DbOpen(DB_NAME, DB_TYPE, &gDb);
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
	return ContactsFindByPrefixN(prefix6, 6, out);
}

Boolean ContactsFindByPrefixN(const UInt8 *prefix, UInt16 len, VmContact *out)
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
		if (MemCmp(c->pubKey, prefix, len) == 0) {
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
	gLastMod = lastMod;
	return errNone;
}

/* SessionGetContacts callback for ContactsUpdate: replaces the record with the same key, or adds one. */
static Err UpsertContact(const VmContact *c, void *ctx)
{
	UInt16 i, n = DmNumRecords(gDb), index = dmMaxRecordIndex;
	VmContact copy = *c;
	MemHandle h;

	TxtUtf8NameToPalm(copy.name, sizeof(copy.name));
	for (i = 0; i < n; i++) {
		h = DmQueryRecord(gDb, i);
		if (h && MemCmp(((const VmContact *)MemHandleLock(h))->pubKey, c->pubKey, VM_PUB_KEY_SIZE) == 0) {
			MemHandleUnlock(h);
			index = i;
			break;
		}
		if (h)
			MemHandleUnlock(h);
	}
	h = index == dmMaxRecordIndex ? DmNewRecord(gDb, &index, sizeof(VmContact)) : DmGetRecord(gDb, index);
	if (!h)
		return DmGetLastErr();
	DmWrite(MemHandleLock(h), 0, &copy, sizeof(VmContact));
	MemHandleUnlock(h);
	DmReleaseRecord(gDb, index, true);
	(*(UInt16 *)ctx)++;
	return errNone;
}

Err ContactsUpdate(void)
{
	UInt32 total, lastMod = 0;
	UInt16 changed = 0;
	Err err;

	if (!gLastMod)
		return ContactsRefresh(NULL);
	err = ContactsOpen();
	if (err != errNone)
		return err;
	err = SessionGetContacts(gLastMod, UpsertContact, &changed, &total, &lastMod);
	if (changed)
		DmQuickSort(gDb, CompareByName, 0);    /* a new or renamed contact */
	if (err == errNone && lastMod > gLastMod)
		gLastMod = lastMod;
	return err;
}
