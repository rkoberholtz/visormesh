#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "transport.h"
#include "ports.h"

#define VM_MIN_ROM_VERSION sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)
#define VM_QUERY_TIMEOUT   (2 * SysTicksPerSecond())
#define VM_QUERY_ATTEMPTS  3     /* over USB the host bridge needs a moment to attach after SrmOpen */

static VmFrameDecoder gDecoder;

static Err RomVersionCompatible(UInt32 required, UInt16 launchFlags)
{
	UInt32 romVersion;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
	if (romVersion >= required)
		return errNone;
	if ((launchFlags & (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) ==
	    (sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp))
		FrmAlert(RomIncompatibleAlert);
	return sysErrRomIncompatible;
}

static void SetStatus(const Char *text)
{
	FormType *form = FrmGetActiveForm();

	FrmHideObject(form, FrmGetObjectIndex(form, MainStatusLabel));
	FrmCopyLabel(form, MainStatusLabel, text);
	FrmShowObject(form, FrmGetObjectIndex(form, MainStatusLabel));
}

/* Sends CMD_DEVICE_QUERY and waits for any '>' frame in reply. */
static void QueryRadio(void)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt8 rx[32];
	UInt16 frameLen, n, i, attempt;
	UInt32 deadline;
	Char msg[32];
	Err err;

	SetStatus("Opening port...");
	err = TransportOpen();
	if (err != errNone) {
		StrPrintF(msg, "Open failed (0x%x)", err);
		SetStatus(msg);
		return;
	}

	frameLen = VmBuildDeviceQuery(frame, sizeof(frame));
	VmDecoderReset(&gDecoder);

	/* Over USB the Visor only appears to the host after SrmOpen; give the bridge time to attach. */
	if (TransportGetPort() == VM_PORT_USB)
		SysTaskDelay(SysTicksPerSecond());

	for (attempt = 1; attempt <= VM_QUERY_ATTEMPTS; attempt++) {
		err = TransportSend(frame, frameLen);
		if (err == serErrTimeOut) {
			/* Nobody reading yet (host still attaching): try again next attempt. */
			StrPrintF(msg, "Host not ready (%u)...", attempt);
			SetStatus(msg);
			continue;
		}
		if (err != errNone) {
			StrPrintF(msg, "Send failed (0x%x)", err);
			SetStatus(msg);
			TransportClose();
			return;
		}

		StrPrintF(msg, "Waiting for reply (%u)...", attempt);
		SetStatus(msg);
		deadline = TimGetTicks() + VM_QUERY_TIMEOUT;
		while (TimGetTicks() < deadline) {
			n = TransportRecv(rx, sizeof(rx), deadline - TimGetTicks(), &err);
			for (i = 0; i < n; i++) {
				if (VmDecoderFeed(&gDecoder, rx[i])) {
					StrPrintF(msg, "Reply 0x%x, %u bytes",
					          gDecoder.len ? gDecoder.payload[0] : 0, gDecoder.len);
					SetStatus(msg);
					TransportClose();
					return;
				}
			}
		}
	}
	SetStatus(err == serErrTimeOut ? "Send timed out" : "No reply");
	TransportClose();
}

static Boolean MainFormHandleEvent(EventType *event)
{
	switch (event->eType) {
	case frmOpenEvent: {
		FormType *form = FrmGetActiveForm();
		UInt16 link = (TransportGetPort() == VM_PORT_USB) ? MainUsbPush : MainSerialPush;

		FrmSetControlGroupSelection(form, MainLinkGroup, link);
		FrmDrawForm(form);
		return true;
	}
	case ctlSelectEvent:
		if (event->data.ctlSelect.controlID == MainSerialPush) {
			TransportSetPort(VM_PORT_SERIAL);
			return true;
		}
		if (event->data.ctlSelect.controlID == MainUsbPush) {
			TransportSetPort(VM_PORT_USB);
			return true;
		}
		if (event->data.ctlSelect.controlID == MainQueryButton) {
			QueryRadio();
			return true;
		}
		if (event->data.ctlSelect.controlID == MainPortsButton) {
			FrmGotoForm(PortsForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

static Boolean AppHandleEvent(EventType *event)
{
	FormType *form;
	UInt16 formId;

	if (event->eType != frmLoadEvent)
		return false;
	formId = event->data.frmLoad.formID;
	form = FrmInitForm(formId);
	FrmSetActiveForm(form);
	if (formId == MainForm)
		FrmSetEventHandler(form, MainFormHandleEvent);
	else if (formId == PortsForm)
		FrmSetEventHandler(form, PortsFormHandleEvent);
	return true;
}

static void AppEventLoop(void)
{
	EventType event;
	Err err;

	do {
		EvtGetEvent(&event, evtWaitForever);
		if (SysHandleEvent(&event))
			continue;
		if (MenuHandleEvent(0, &event, &err))
			continue;
		if (AppHandleEvent(&event))
			continue;
		FrmDispatchEvent(&event);
	} while (event.eType != appStopEvent);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	Err err = RomVersionCompatible(VM_MIN_ROM_VERSION, launchFlags);

	if (err != errNone)
		return err;
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		FrmGotoForm(MainForm);
		AppEventLoop();
		TransportClose();
		FrmCloseAllForms();
	}
	return errNone;
}
