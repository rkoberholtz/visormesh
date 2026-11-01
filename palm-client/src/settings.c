/*
 * Node settings: name, radio parameters (with MeshCore's suggested presets),
 * TX power and path hash size. Save sends only what changed, each command
 * must come back RESP_OK, then the radio is re-read.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "presets.h"
#include "session.h"
#include "settings.h"

#define CUSTOM_LABEL "Custom"

static const Char *gPresetTitles[64];   /* list choices; more than the snapshot has */

static void *GetObject(FormType *form, UInt16 id)
{
	return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static void SetFieldText(FormType *form, UInt16 id, const Char *text)
{
	FieldType *field = GetObject(form, id);
	MemHandle oldH = FldGetTextHandle(field);
	MemHandle h = MemHandleNew(StrLen(text) + 1);

	if (!h)
		return;
	StrCopy(MemHandleLock(h), text);
	MemHandleUnlock(h);
	FldSetTextHandle(field, h);
	if (oldH)
		MemHandleFree(oldH);
	if (FrmVisible(form))
		FldDrawField(field);
}

static const Char *GetFieldText(FormType *form, UInt16 id)
{
	const Char *t = FldGetTextPtr(GetObject(form, id));

	return t ? t : "";
}

static void SetStatus(FormType *form, const Char *text)
{
	UInt16 index = FrmGetObjectIndex(form, SettingsStatusLabel);

	FrmHideObject(form, index);
	FrmCopyLabel(form, SettingsStatusLabel, text);
	FrmShowObject(form, index);
}

static void ShowError(const Char *msg)
{
	FrmCustomAlert(SettingsErrorAlert, msg, "", "");
}

static void SelectHashSize(FormType *form, UInt8 bytes)
{
	UInt16 id = bytes == 3 ? SettingsHash3Push : bytes == 2 ? SettingsHash2Push : SettingsHash1Push;

	FrmSetControlGroupSelection(form, SettingsHashGroup, id);
}

static UInt8 SelectedHashSize(FormType *form)
{
	UInt16 id = FrmGetObjectId(form, FrmGetControlGroupSelection(form, SettingsHashGroup));

	return id == SettingsHash3Push ? 3 : id == SettingsHash2Push ? 2 : 1;
}

static void SetRadioFields(FormType *form, UInt32 freqKHz, UInt32 bwHz, UInt8 sf, UInt8 cr)
{
	Char buf[16];

	VmFormatDecimal(buf, freqKHz, 3);
	SetFieldText(form, SettingsFreqField, buf);
	VmFormatDecimal(buf, bwHz, 3);
	SetFieldText(form, SettingsBwField, buf);
	StrIToA(buf, sf);
	SetFieldText(form, SettingsSfField, buf);
	StrIToA(buf, cr);
	SetFieldText(form, SettingsCrField, buf);
}

/* Index of the preset matching these radio settings, or -1. */
static Int16 FindPreset(UInt32 freqKHz, UInt32 bwHz, UInt8 sf, UInt8 cr)
{
	UInt16 i;

	for (i = 0; i < gPresetCount; i++)
		if (gPresets[i].freqKHz == freqKHz && gPresets[i].bwHz == bwHz &&
		    gPresets[i].sf == sf && gPresets[i].cr == cr)
			return (Int16)i;
	return -1;
}

static void InitForm(FormType *form)
{
	const VmSelfInfo *self = SessionSelf();
	const VmDeviceInfo *dev = SessionDevice();
	ListType *list = GetObject(form, SettingsPresetList);
	ControlType *trigger = GetObject(form, SettingsPresetTrigger);
	UInt16 i, n = gPresetCount;
	Int16 match;
	Char buf[8];

	if (n > sizeof(gPresetTitles) / sizeof(gPresetTitles[0]))
		n = sizeof(gPresetTitles) / sizeof(gPresetTitles[0]);
	for (i = 0; i < n; i++)
		gPresetTitles[i] = gPresets[i].title;
	LstSetListChoices(list, (Char **)gPresetTitles, n);
	LstSetHeight(list, n < 10 ? n : 10);

	SetFieldText(form, SettingsNameField, self->name);
	SetRadioFields(form, self->freqKHz, self->bwHz, self->sf, self->cr);
	StrIToA(buf, self->txPowerDbm);
	SetFieldText(form, SettingsPowerField, buf);
	SelectHashSize(form, (UInt8)(dev->pathHashMode + 1));
	if (!dev->hasPathHashMode) {
		CtlSetEnabled(GetObject(form, SettingsHash1Push), false);
		CtlSetEnabled(GetObject(form, SettingsHash2Push), false);
		CtlSetEnabled(GetObject(form, SettingsHash3Push), false);
	}

	match = FindPreset(self->freqKHz, self->bwHz, self->sf, self->cr);
	if (match >= 0) {
		LstSetSelection(list, match);
		CtlSetLabel(trigger, gPresets[match].title);
	} else {
		LstSetSelection(list, noListSelection);
		CtlSetLabel(trigger, CUSTOM_LABEL);
	}
}

static void ApplyPreset(FormType *form, UInt16 index)
{
	const VmPreset *p = &gPresets[index];

	SetRadioFields(form, p->freqKHz, p->bwHz, p->sf, p->cr);
	if (p->pathHashSize && SessionDevice()->hasPathHashMode)
		SelectHashSize(form, p->pathHashSize);
}

/* Sends one command, reporting which setting failed. Returns true on RESP_OK. */
static Boolean Send(FormType *form, const UInt8 *frame, UInt16 len, const Char *what)
{
	Char msg[64];
	Err err;

	StrPrintF(msg, "Setting %s...", what);
	SetStatus(form, msg);
	err = SessionCommand(frame, len);
	if (err == errNone)
		return true;
	if (err == vmErrRadio)
		StrPrintF(msg, "The radio rejected the %s.", what);
	else if (err == vmErrNoReply)
		StrPrintF(msg, "No reply while setting the %s.", what);
	else
		StrPrintF(msg, "Error 0x%x while setting the %s.", err, what);
	SetStatus(form, "");
	ShowError(msg);
	return false;
}

static Boolean Save(FormType *form)
{
	const VmSelfInfo *self = SessionSelf();
	const VmDeviceInfo *dev = SessionDevice();
	const Char *name = GetFieldText(form, SettingsNameField);
	UInt8 frame[VM_HEADER_LEN + 1 + VM_NAME_MAX];
	UInt32 freq, bw;
	Int32 sf, cr, power;
	UInt8 hashMode = (UInt8)(SelectedHashSize(form) - 1);
	UInt16 len;
	Char msg[64];

	/* Validate everything first, with the firmware's limits, so nothing is half-applied. */
	if (StrLen(name) == 0 || StrLen(name) > VM_NAME_MAX) {
		ShowError("Enter a node name (1-31 characters).");
		return false;
	}
	if (!VmParseDecimal(GetFieldText(form, SettingsFreqField), 3, &freq) ||
	    freq < VM_FREQ_MIN_KHZ || freq > VM_FREQ_MAX_KHZ) {
		ShowError("Frequency must be 150-2500 MHz, up to 3 decimals.");
		return false;
	}
	if (!VmParseDecimal(GetFieldText(form, SettingsBwField), 3, &bw) ||
	    bw < VM_BW_MIN_HZ || bw > VM_BW_MAX_HZ) {
		ShowError("Bandwidth must be 7-500 kHz.");
		return false;
	}
	if (!VmParseInt(GetFieldText(form, SettingsSfField), &sf) || sf < VM_SF_MIN || sf > VM_SF_MAX) {
		ShowError("SF must be 5-12.");
		return false;
	}
	if (!VmParseInt(GetFieldText(form, SettingsCrField), &cr) || cr < VM_CR_MIN || cr > VM_CR_MAX) {
		ShowError("CR must be 5-8.");
		return false;
	}
	if (!VmParseInt(GetFieldText(form, SettingsPowerField), &power) ||
	    power < VM_TX_POWER_MIN || power > self->maxTxPowerDbm) {
		StrPrintF(msg, "TX power must be %d to %u dBm.", VM_TX_POWER_MIN, self->maxTxPowerDbm);
		ShowError(msg);
		return false;
	}

	if (StrCompare(name, self->name) != 0) {
		len = VmBuildSetAdvertName(frame, sizeof(frame), name);
		if (!Send(form, frame, len, "name"))
			return false;
	}
	if (freq != self->freqKHz || bw != self->bwHz || sf != self->sf || cr != self->cr) {
		len = VmBuildSetRadioParams(frame, sizeof(frame), freq, bw, (UInt8)sf, (UInt8)cr,
		                            dev->hasRepeat, dev->repeatEnabled);
		if (!Send(form, frame, len, "radio settings"))
			return false;
	}
	if (power != self->txPowerDbm) {
		len = VmBuildSetTxPower(frame, sizeof(frame), (Int8)power);
		if (!Send(form, frame, len, "TX power"))
			return false;
	}
	if (dev->hasPathHashMode && hashMode != dev->pathHashMode) {
		len = VmBuildSetPathHashMode(frame, sizeof(frame), hashMode);
		if (!Send(form, frame, len, "path hash size"))
			return false;
	}

	SetStatus(form, "Reading back...");
	if (SessionRefresh() != errNone) {
		SetStatus(form, "");
		ShowError("Saved, but reading the settings back failed. Reconnect to check them.");
	}
	return true;
}

Boolean SettingsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		InitForm(form);
		FrmDrawForm(form);
		FrmSetFocus(form, FrmGetObjectIndex(form, SettingsNameField));
		return true;
	case popSelectEvent:
		if (event->data.popSelect.listID == SettingsPresetList && event->data.popSelect.selection >= 0)
			ApplyPreset(form, (UInt16)event->data.popSelect.selection);
		return false;   /* let the OS update the trigger's label */
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case SettingsSaveButton:
			if (Save(form))
				FrmGotoForm(MainForm);
			return true;
		case SettingsCancelButton:
			FrmGotoForm(MainForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
