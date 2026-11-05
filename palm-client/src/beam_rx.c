/*
 * Receiving a beam (sysAppLaunchCmdExgReceiveData). That launch can come
 * while the app isn't running, without globals and with only the first code
 * segment, so this stays in segment 1, touches no globals and calls nothing
 * in another segment: it only saves what came to the inbox database and
 * sets a feature. The app asks what to do with it when it next runs
 * (beam.c, BeamCheckInbox).
 */
#include <PalmOS.h>
#include "app.h"
#include "beam.h"

#define INBOX_TYPE 'inbx'

Err BeamReceive(ExgSocketPtr s)
{
	UInt8 buf[VM_BEAM_MAX];
	UInt32 n = 0, got;
	LocalID id;
	DmOpenRef db;
	MemHandle h;
	UInt16 at = dmMaxRecordIndex;
	Err err;

	err = ExgAccept(s);
	if (err != errNone)
		return err;
	do {
		got = ExgReceive(s, buf + n, VM_BEAM_MAX - n, &err);
		n += got;
	} while (got > 0 && err == errNone && n < VM_BEAM_MAX);
	ExgDisconnect(s, err);
	if (err != errNone || n == 0 || n >= VM_BEAM_MAX)
		return err;
	id = DmFindDatabase(0, VM_INBOX_NAME);
	if (!id) {
		err = DmCreateDatabase(0, VM_INBOX_NAME, VM_CREATOR, INBOX_TYPE, false);
		if (err != errNone)
			return err;
		id = DmFindDatabase(0, VM_INBOX_NAME);
	}
	db = id ? DmOpenDatabase(0, id, dmModeReadWrite) : 0;
	if (!db)
		return dmErrCantOpen;
	h = DmNewRecord(db, &at, n);
	if (h) {
		DmWrite(MemHandleLock(h), 0, buf, n);
		MemHandleUnlock(h);
		DmReleaseRecord(db, at, true);
	}
	DmCloseDatabase(db);
	FtrSet(VM_CREATOR, VM_FTR_INBOX, 1);
	return h ? errNone : dmErrMemError;
}
