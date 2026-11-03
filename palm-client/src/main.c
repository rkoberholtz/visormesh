#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "contacts.h"
#include "channels.h"
#include "messages.h"
#include "app.h"
#include "home_form.h"
#include "convo_form.h"
#include "contacts_form.h"
#include "contact_form.h"
#include "heard.h"
#include "remote.h"
#include "remote_form.h"
#include "sounds.h"
#include "mutes.h"
#include "scopes.h"
#include "map_form.h"
#include "node_form.h"
#include "settings.h"
#include "ports.h"
#include "preview_form.h"
#include "channel_form.h"
#include "bgcheck.h"
#include "ui/draw.h"

#define VM_MIN_ROM_VERSION sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0)
#define VM_POLL_TICKS      (SysTicksPerSecond() / 2)
#define VM_UPDATE_DATA     0x0100    /* frmUpdateEvent code: new messages / contacts */

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

/* The Options menu is the same on every screen. */
static Boolean HandleMenu(UInt16 itemID)
{
	switch (itemID) {
	case MainMenuHome:        FrmGotoForm(HomeForm);        return true;
	case MainMenuContacts:    FrmGotoForm(ContactsForm);    return true;
	case MainMenuNode:        FrmGotoForm(MainForm);        return true;
	case MainMenuMap:         FrmGotoForm(MapForm);         return true;
	case MainMenuPorts:       FrmGotoForm(PortsForm);       return true;
	case MainMenuFontPreview: FrmGotoForm(FontPreviewForm); return true;
	case MainMenuSounds:      SoundsFormOpen(FrmGetActiveFormID()); return true;
	case MainMenuAddChannel:  FrmGotoForm(ChannelForm);     return true;
	case MainMenuLeaveChannel: ChannelLeaveCurrent();       return true;
	case MainMenuMuteChannel: ChannelMuteToggleCurrent();   return true;
	case MainMenuChannelScope: ChannelScopeChooseCurrent(); return true;
	case MainMenuScopes:      ScopesFormOpen(FrmGetActiveFormID()); return true;
	}
	return false;
}

static Boolean AppHandleEvent(EventType *event)
{
	FormType *form;
	UInt16 formId;

	if (event->eType == menuEvent)
		return HandleMenu(event->data.menu.itemID);
	if (event->eType != frmLoadEvent)
		return false;
	formId = event->data.frmLoad.formID;
	form = FrmInitForm(formId);
	FrmSetActiveForm(form);
	switch (formId) {
	case HomeForm:        FrmSetEventHandler(form, HomeFormHandleEvent);        break;
	case ConvoForm:
		/* the Channel menu only for a channel; a DM gets the plain one */
		FrmSetMenu(form, gAppConv.type == VM_CONV_CHANNEL ? ConvoChanMenuBar : MainMenuBar);
		FrmSetEventHandler(form, ConvoFormHandleEvent);
		break;
	case ContactsForm:    FrmSetEventHandler(form, ContactsFormHandleEvent);    break;
	case MainForm:        FrmSetEventHandler(form, NodeFormHandleEvent);        break;
	case SettingsForm:    FrmSetEventHandler(form, SettingsFormHandleEvent);    break;
	case PortsForm:       FrmSetEventHandler(form, PortsFormHandleEvent);       break;
	case FontPreviewForm: FrmSetEventHandler(form, FontPreviewFormHandleEvent); break;
	case ChannelForm:     FrmSetEventHandler(form, ChannelFormHandleEvent);     break;
	case ContactForm:     FrmSetEventHandler(form, ContactFormHandleEvent);     break;
	case ContactAddForm:  FrmSetEventHandler(form, ContactAddFormHandleEvent);  break;
	case RemoteForm:      FrmSetEventHandler(form, RemoteFormHandleEvent);      break;
	case SoundsForm:      FrmSetEventHandler(form, SoundsFormHandleEvent);      break;
	case ScopesForm:      FrmSetEventHandler(form, ScopesFormHandleEvent);      break;
	case MapForm:         FrmSetEventHandler(form, MapFormHandleEvent);         break;
	}
	return true;
}

/*
 * Sleep detection: the tick counter stops while the Visor sleeps but the
 * clock keeps going, so a clock jump well past the ticks means we slept.
 * The serial/USB link doesn't survive sleep, so reconnect then; the radio
 * queued anything that arrived meanwhile and the connect syncs it.
 */
#define VM_SLEEP_GAP_SECS 5

static UInt32  gLastSecs = 0, gLastTicks = 0;
static Boolean gWantConnection = false;   /* we were connected when the Visor slept */

static void CheckWake(void)
{
	UInt32 secs = TimGetSeconds(), ticks = TimGetTicks();
	Int32 wall, run;

	if (gLastSecs) {
		wall = (Int32)(secs - gLastSecs);
		run = (Int32)((ticks - gLastTicks) / SysTicksPerSecond());
		if (wall - run > VM_SLEEP_GAP_SECS && (SessionIsConnected() || gWantConnection)) {
			gWantConnection = true;
			SessionDisconnect();
			if (AppConnect(NULL, true) == errNone)
				gWantConnection = false;
			FrmUpdateForm(FrmGetActiveFormID(), VM_UPDATE_DATA);
			secs = TimGetSeconds();
			ticks = TimGetTicks();
		}
	}
	gLastSecs = secs;
	gLastTicks = ticks;
}

/* While connected, wake regularly to handle what the radio sends and refresh the screen on show. */
static void Poll(void)
{
	UInt16 formId = FrmGetActiveFormID();
	UInt16 changed = SessionPoll();

	if (RemoteTick())
		changed |= VM_CHANGED_REMOTE;

	/* a notification sound, unless it's for the conversation on screen */
	if (changed & VM_CHANGED_ALERT)
		SoundsNotify(SessionTakeSounds(), formId == ConvoForm ? &gAppConv : NULL);
	if (changed)
		FrmUpdateForm(formId, VM_UPDATE_DATA);
	if (formId == ConvoForm)
		ConvoFormIdle();
}

static void AppEventLoop(void)
{
	EventType event;
	Err err;

	do {
		EvtGetEvent(&event, SessionIsConnected() ? VM_POLL_TICKS : evtWaitForever);
		CheckWake();
		if (event.eType == nilEvent) {
			Poll();
			continue;
		}
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
	switch (cmd) {
	case sysAppLaunchCmdAlarmTriggered:
		BgAlarmTriggered((SysAlarmTriggeredParamType *)cmdPBP);
		return errNone;
	case sysAppLaunchCmdDisplayAlarm:
		BgDisplayAlarm();
		return errNone;
	case VM_LAUNCH_BGCHECK:
		/* only with the globals BgAlarmTriggered launches it with */
		if (launchFlags & sysAppLaunchFlagNewGlobals)
			*(UInt16 *)cmdPBP = BgCheckRun();
		return errNone;
	case sysAppLaunchCmdSystemReset:
	case sysAppLaunchCmdSyncNotify:     /* installed, which drops the alarm */
	case sysAppLaunchCmdTimeChange:
		BgSchedule();
		return errNone;
	}
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		AppLoadPrefs();
		UiFontInit(SoundsLargeText());
		HeardLoad();
		FrmGotoForm(HomeForm);
		AppEventLoop();
		SessionDisconnect();
		HeardSave();
		FrmCloseAllForms();
		ContactsClose();
		ChannelsClose();
		MessagesClose();
		UiFontRelease();
		BgSchedule();
	}
	return errNone;
}
