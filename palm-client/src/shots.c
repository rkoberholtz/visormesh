#include <PalmOS.h>
#include "app.h"
#include "shots.h"

#define SHOTS_DB_TYPE 'shot'

static void ShotAlarm(UInt16 cmd, SysAlarmTriggeredParamType *p)
{
	if (cmd == almProcCmdTriggered)
		ShotCapture();
}

void ShotCapture(void)
{
	BitmapType *bmp = WinGetBitmap(WinGetDisplayWindow());
	ShotHeader hdr;
	UInt32 bits;
	UInt16 index = dmMaxRecordIndex;
	LocalID id;
	DmOpenRef db;
	MemHandle h;
	void *p;

	if (!bmp)
		return;
	hdr.width = (UInt16)bmp->width;
	hdr.height = (UInt16)bmp->height;
	hdr.rowBytes = bmp->rowBytes;
	hdr.pixelSize = bmp->pixelSize;
	hdr.reserved = 0;
	hdr.time = TimGetSeconds();
	bits = (UInt32)hdr.rowBytes * hdr.height;

	id = DmFindDatabase(0, "VisorMeshShots");
	if (!id && DmCreateDatabase(0, "VisorMeshShots", VM_CREATOR, SHOTS_DB_TYPE, false) == errNone)
		id = DmFindDatabase(0, "VisorMeshShots");
	db = id ? DmOpenDatabase(0, id, dmModeReadWrite) : 0;
	if (!db) {
		SndPlaySystemSound(sndError);
		return;
	}
	h = DmNewRecord(db, &index, sizeof(hdr) + bits);
	if (h) {
		p = MemHandleLock(h);
		DmWrite(p, 0, &hdr, sizeof(hdr));
		DmWrite(p, sizeof(hdr), BmpGetBits(bmp), bits);
		MemHandleUnlock(h);
		DmReleaseRecord(db, index, true);
		SndPlaySystemSound(sndClick);
	} else {
		SndPlaySystemSound(sndError);
	}
	DmCloseDatabase(db);
}

void ShotLater(UInt16 seconds)
{
	AlmSetProcAlarm(ShotAlarm, 0, TimGetSeconds() + seconds);
}

void ShotCancel(void)
{
	AlmSetProcAlarm(ShotAlarm, 0, 0);
}
