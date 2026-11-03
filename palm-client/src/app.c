#include <PalmOS.h>
#include "resources.h"
#include "transport.h"
#include "session.h"
#include "app.h"
#include "sounds.h"
#include "mutes.h"
#include "scopes.h"
#include "pins.h"

VmConv  gAppConv;
Boolean gAppAutoConnectTried = false;

typedef struct {
	UInt16 version;
	UInt32 port;       /* VM_PORT_USB, VM_PORT_SERIAL or VM_PORT_SBLORA */
	Int16  utcOffset;  /* local minus UTC, minutes */
} AppPrefs;

void AppOpenConversation(const VmConv *conv)
{
	gAppConv = *conv;
	FrmGotoForm(ConvoForm);
}

void AppLoadPrefs(void)
{
	AppPrefs prefs;
	UInt16 size = sizeof(prefs);
	Int32 west;

	SoundsLoad();
	MutesLoad();
	ScopesLoad();
	PinsLoad();
	if (PrefGetAppPreferences(VM_CREATOR, VM_PREFS_ID, &prefs, &size, true) != noPreferenceFound &&
	    size == sizeof(prefs) && prefs.version == VM_PREFS_VERSION) {
		/*
		 * Slot isn't restored: with no module in the slot, the first bus read
		 * at csSlot0 is a bus error and resets the Visor, and the Home form
		 * auto-connects at launch, so a saved Slot made every launch reset.
		 */
		if (prefs.port != VM_PORT_SBLORA)
			TransportSetPort(prefs.port);
		SessionSetUtcOffset(prefs.utcOffset);
		return;
	}
	/*
	 * No saved offset yet: start from the system's, if it's a real zone. The
	 * Neo's held 1140 (19 h), which made times 19 h off; ignore anything past +-14 h.
	 */
	west = (Int32)PrefGetPreference(prefMinutesWestOfGMT);
	SessionSetUtcOffset(west >= -840 && west <= 720 ? (Int16)-west : 0);
}

void AppSavePrefs(void)
{
	AppPrefs prefs;

	prefs.version = VM_PREFS_VERSION;
	prefs.port = TransportGetPort();
	prefs.utcOffset = SessionUtcOffset();
	PrefSetAppPreferences(VM_CREATOR, VM_PREFS_ID, VM_PREFS_VERSION, &prefs, sizeof(prefs), true);
}

void AppErrorText(Err err, Char *out)
{
	if (err == vmErrNoReply)
		StrCopy(out, "No reply from radio.");
	else if (err == vmErrRadio)
		StrCopy(out, "Radio returned error.");
	else if (err == vmErrBadReply)
		StrCopy(out, "Bad reply from radio.");
	else if (err == vmErrTooLong)
		StrCopy(out, "Message too long.");
	else if (err == serErrNotOpen)
		StrCopy(out, "Not connected.");
	else if (err == vmErrNoModule)
		StrCopy(out, "No module in the slot.");
	else if (err == vmErrLoginFailed)
		StrCopy(out, "Login refused: wrong password?");
	else
		StrPrintF(out, "Error 0x%x.", err);
}

Err AppConnect(AppStatusFn status, Boolean quiet)
{
	Char msg[40];
	Err err;

	if (SessionIsConnected())
		return errNone;
	if (status)
		status("Connecting...");
	err = SessionConnect();
	if (err != errNone && !quiet) {
		AppErrorText(err, msg);
		FrmCustomAlert(SettingsErrorAlert, msg, "", "");
	}
	return err;
}
