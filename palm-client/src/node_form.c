/*
 * Node: this radio's details, the link (USB / serial), connect/disconnect,
 * settings and sending an advert.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "transport.h"
#include "session.h"
#include "app.h"
#include "node_form.h"
#include "ui/draw.h"

#define VM_LABEL_CHARS 22    /* the info labels in the .rcp are padded to this width */

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
	const VmDeviceInfo *dev = SessionDevice();
	const VmSelfInfo *self = SessionSelf();
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
	SetLabel(form, MainNameLabel, self->name);
	FormatFreq(buf, self->freqKHz);
	SetLabel(form, MainFreqLabel, buf);
	FormatBandwidth(bw, self->bwHz);
	StrPrintF(buf, "BW %s SF%u CR%u", bw, self->sf, self->cr);
	SetLabel(form, MainModeLabel, buf);
	StrPrintF(buf, "%d dBm (max %u)", (Int16)self->txPowerDbm, self->maxTxPowerDbm);
	SetLabel(form, MainPowerLabel, buf);
	StrPrintF(buf, "%s (%u)", dev->version, dev->fwVerCode);
	SetLabel(form, MainFirmwareLabel, buf);
	CtlSetLabel(FrmGetObjectPtr(form, FrmGetObjectIndex(form, MainConnectButton)), "Disconnect");
}

/*
 * Clock check, small font, under the radio details:
 *   Visor 11/1 17:02 UTC 11/1 22:02 (-5:00)
 *   radio 11/1 11:57 UTC  sys 300
 * "sys" is Palm OS's own minutes-west-of-GMT preference (the offset's default).
 */
static void DrawClockLine(void)
{
	DateTimeType dt;
	RectangleType r;
	Char line[56], utc[16], radio[16];
	UInt32 now = TimGetSeconds();
	Int16 off = SessionUtcOffset();
	UInt32 rt = SessionRadioTime();
	FontID old = FntSetFont(vmSmallFont);

	RctSetRectangle(&r, 0, 104, 160, 20);
	WinEraseRectangle(&r, 0);
	TimSecondsToDateTime(SessionNow() + 2082844800UL, &dt);
	StrPrintF(utc, "%u/%u %02u:%02u", dt.month, dt.day, dt.hour, dt.minute);
	TimSecondsToDateTime(now, &dt);
	StrPrintF(line, "Visor %u/%u %02u:%02u UTC %s (%s%d:%02d)", dt.month, dt.day, dt.hour, dt.minute, utc,
	          off < 0 ? "-" : "+", (off < 0 ? -off : off) / 60, (off < 0 ? -off : off) % 60);
	WinDrawChars(line, StrLen(line), 5, 105);
	if (rt) {
		TimSecondsToDateTime(rt + 2082844800UL, &dt);
		StrPrintF(radio, "%u/%u %02u:%02u", dt.month, dt.day, dt.hour, dt.minute);
	} else {
		StrCopy(radio, "--");
	}
	if (SessionClockMismatch())
		StrPrintF(line, "radio %s UTC  MISMATCH: check date", radio);
	else
		StrPrintF(line, "radio %s UTC  sys %ld", radio, (Int32)PrefGetPreference(prefMinutesWestOfGMT));
	WinDrawChars(line, StrLen(line), 5, 113);
	FntSetFont(old);
}

static FormType *gStatusForm;

static void ShowStatus(const Char *text)
{
	SetLabel(gStatusForm, MainNameLabel, text);
}

static void ConnectOrDisconnect(FormType *form)
{
	Char msg[40];
	Err err;

	if (SessionIsConnected()) {
		SessionDisconnect();
		ShowRadioInfo(form);
		return;
	}
	gStatusForm = form;
	err = AppConnect(ShowStatus, true);
	ShowRadioInfo(form);
	DrawClockLine();
	if (err != errNone) {
		AppErrorText(err, msg);
		SetLabel(form, MainNameLabel, msg);
	}
}

static void SendAdvert(void)
{
	UInt16 choice;
	Char msg[40];
	Err err;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first.", "", "");
		return;
	}
	choice = FrmAlert(AdvertAlert);       /* 0 zero-hop, 1 flood, 2 cancel */
	if (choice > 1)
		return;
	err = SessionSendAdvert(choice == 1);
	if (err != errNone) {
		AppErrorText(err, msg);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
	}
}

Boolean NodeFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent: {
		UInt16 link = (TransportGetPort() == VM_PORT_USB) ? MainUsbPush : MainSerialPush;

		FrmSetControlGroupSelection(form, MainLinkGroup, link);
		ShowRadioInfo(form);
		FrmDrawForm(form);
		DrawClockLine();
		return true;
	}
	case frmUpdateEvent:
		ShowRadioInfo(form);
		DrawClockLine();
		return true;

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
			AppSavePrefs();
			return true;
		case MainConnectButton:
			ConnectOrDisconnect(form);
			return true;
		case MainSettingsButton:
			if (SessionIsConnected())
				FrmGotoForm(SettingsForm);
			else
				FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first.", "", "");
			return true;
		case MainAdvertButton:
			SendAdvert();
			return true;
		case MainDoneButton:
			FrmGotoForm(HomeForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
