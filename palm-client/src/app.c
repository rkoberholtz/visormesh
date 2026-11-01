#include <PalmOS.h>
#include "resources.h"
#include "transport.h"
#include "session.h"
#include "app.h"

VmConv  gAppConv;
Boolean gAppAutoConnectTried = false;

typedef struct {
	UInt16 version;
	UInt32 port;       /* VM_PORT_USB or VM_PORT_SERIAL */
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

	if (PrefGetAppPreferences(VM_CREATOR, VM_PREFS_ID, &prefs, &size, true) != noPreferenceFound &&
	    size == sizeof(prefs) && prefs.version == VM_PREFS_VERSION) {
		TransportSetPort(prefs.port);
		SessionSetUtcOffset(prefs.utcOffset);
		return;
	}
	/* no saved offset yet: start from the system's, if it has one */
	SessionSetUtcOffset(-(Int16)PrefGetPreference(prefMinutesWestOfGMT));
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
		StrCopy(out, "No reply from the radio.");
	else if (err == vmErrRadio)
		StrCopy(out, "The radio returned an error.");
	else if (err == vmErrBadReply)
		StrCopy(out, "Unexpected reply from the radio.");
	else if (err == vmErrTooLong)
		StrCopy(out, "Message too long.");
	else if (err == serErrNotOpen)
		StrCopy(out, "Not connected.");
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
