/*
 * Diagnostics: lists the Serial Manager drivers on this device (creator,
 * feature flags, description), to find which one reaches the USB cradle.
 */
#include <PalmOS.h>
#include "resources.h"
#include "ports.h"

#define LINE_H     11
#define FIRST_Y    18
#define MAX_LINES  11

static void DrawLine(UInt16 line, const Char *text)
{
	WinDrawChars(text, StrLen(text), 2, FIRST_Y + line * LINE_H);
}

static void DrawPorts(void)
{
	DeviceInfoType info;
	UInt16 count = 0, i, line = 0;
	Char buf[48];
	Char crid[5];
	Err err;

	err = SrmGetDeviceCount(&count);
	if (err != errNone) {
		StrPrintF(buf, "SrmGetDeviceCount err 0x%x", err);
		DrawLine(line, buf);
		return;
	}
	StrPrintF(buf, "%u serial device(s):", count);
	DrawLine(line++, buf);

	for (i = 0; i < count && line + 1 < MAX_LINES; i++) {
		MemSet(&info, sizeof(info), 0);
		err = SrmGetDeviceInfo(i, &info);
		if (err != errNone) {
			StrPrintF(buf, "%u: err 0x%x", i, err);
			DrawLine(line++, buf);
			continue;
		}
		MemMove(crid, &info.serDevCreator, 4);
		crid[4] = 0;
		StrPrintF(buf, "%u '%s' ftr %lx max %lu", i, crid, info.serDevFtrInfo, info.serDevMaxBaudRate);
		DrawLine(line++, buf);
		if (info.serDevPortInfoStr) {
			StrNCopy(buf, "   ", sizeof(buf));
			StrNCat(buf, info.serDevPortInfoStr, sizeof(buf));
			DrawLine(line++, buf);
		}
	}
}

Boolean PortsFormHandleEvent(EventType *event)
{
	switch (event->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		DrawPorts();
		return true;
	case ctlSelectEvent:
		if (event->data.ctlSelect.controlID == PortsDoneButton) {
			FrmGotoForm(MainForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
