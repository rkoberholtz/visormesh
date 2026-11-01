#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "transport.h"
#include "session.h"
#include "ports.h"

#define VM_MIN_ROM_VERSION sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)
#define VM_LABEL_CHARS     22    /* the info labels in the .rcp are padded to this width */

/* Radio state shown on the main form; kept here so it survives a trip to the Ports form. */
static VmDeviceInfo gDevice;
static VmSelfInfo   gSelf;

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

/* Replaces a label's text, truncated to the width reserved for it in the .rcp. */
static void SetLabel(FormType *form, UInt16 id, const Char *text)
{
	Char buf[VM_LABEL_CHARS + 1];
	UInt16 index = FrmGetObjectIndex(form, id);
	Boolean visible = FrmVisible(form);

	StrNCopy(buf, text, VM_LABEL_CHARS);
	buf[VM_LABEL_CHARS] = 0;
	if (visible)
		FrmHideObject(form, index);
	FrmCopyLabel(form, id, buf);
	if (visible)
		FrmShowObject(form, index);
}

/* Formats e.g. 869618 kHz as "869.618 MHz" and 62500 Hz as "62.5" (kHz), without floating point. */
static void FormatFreq(Char *out, UInt32 kHz)
{
	StrPrintF(out, "%lu.%03lu MHz", kHz / 1000, kHz % 1000);
}

static void FormatBandwidth(Char *out, UInt32 hz)
{
	if (hz % 1000)
		StrPrintF(out, "%lu.%lu", hz / 1000, (hz % 1000) / 100);
	else
		StrPrintF(out, "%lu", hz / 1000);
}

static void ShowRadioInfo(FormType *form)
{
	Char buf[40], bw[12];

	if (!SessionIsConnected()) {
		SetLabel(form, MainNameLabel, "Not connected");
		SetLabel(form, MainFreqLabel, "-");
		SetLabel(form, MainModeLabel, "-");
		SetLabel(form, MainPowerLabel, "-");
		SetLabel(form, MainFirmwareLabel, "-");
		CtlSetLabel(FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainConnectButton)), "Connect");
		return;
	}
	SetLabel(form, MainNameLabel, gSelf.name);
	FormatFreq(buf, gSelf.freqKHz);
	SetLabel(form, MainFreqLabel, buf);
	FormatBandwidth(bw, gSelf.bwHz);
	StrPrintF(buf, "BW %s SF%u CR%u", bw, gSelf.sf, gSelf.cr);
	SetLabel(form, MainModeLabel, buf);
	StrPrintF(buf, "%u dBm (max %u)", gSelf.txPowerDbm, gSelf.maxTxPowerDbm);
	SetLabel(form, MainPowerLabel, buf);
	StrPrintF(buf, "%s (%u)", gDevice.version, gDevice.fwVerCode);
	SetLabel(form, MainFirmwareLabel, buf);
	CtlSetLabel(FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainConnectButton)), "Disconnect");
}

static void ConnectOrDisconnect(FormType *form)
{
	Char msg[32];
	Err err;

	if (SessionIsConnected()) {
		SessionDisconnect();
		ShowRadioInfo(form);
		return;
	}

	SetLabel(form, MainNameLabel, "Connecting...");
	err = SessionConnect(&gDevice, &gSelf);
	if (err == errNone) {
		ShowRadioInfo(form);
		return;
	}
	ShowRadioInfo(form);
	if (err == vmErrNoReply)
		StrCopy(msg, "No reply from radio");
	else if (err == vmErrRadio)
		StrCopy(msg, "Radio returned an error");
	else if (err == vmErrBadReply)
		StrCopy(msg, "Unexpected reply");
	else
		StrPrintF(msg, "Port error 0x%x", err);
	SetLabel(form, MainNameLabel, msg);
}

static Boolean MainFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent: {
		UInt16 link = (TransportGetPort() == VM_PORT_USB) ? MainUsbPush : MainSerialPush;

		FrmSetControlGroupSelection(form, MainLinkGroup, link);
		ShowRadioInfo(form);
		FrmDrawForm(form);
		return true;
	}
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case MainSerialPush:
		case MainUsbPush:
			/* Changing the link drops any open connection; it applies on the next Connect. */
			if (SessionIsConnected()) {
				SessionDisconnect();
				ShowRadioInfo(form);
			}
			TransportSetPort(event->data.ctlSelect.controlID == MainUsbPush ? VM_PORT_USB : VM_PORT_SERIAL);
			return true;
		case MainConnectButton:
			ConnectOrDisconnect(form);
			return true;
		case MainPortsButton:
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
		SessionDisconnect();
		FrmCloseAllForms();
	}
	return errNone;
}
