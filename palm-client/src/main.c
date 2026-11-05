#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "contacts.h"
#include "channels.h"
#include "messages.h"
#include "reactions.h"
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
#include "shots.h"
#include "stats_form.h"
#include "packets_form.h"
#include "trace_form.h"
#include "beam.h"
#include "blocks.h"
#include "stale.h"
#include "ui/draw.h"
#include "ui/title.h"

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

static void Connect(void);

/* Go, Channel and Options commands, whichever screen's menu they're on; Edit is the form's. */
static Boolean HandleMenu(UInt16 itemID)
{
	switch (itemID) {
	case MainMenuHome:        FrmGotoForm(HomeForm);        return true;
	case MainMenuContacts:    FrmGotoForm(ContactsForm);    return true;
	case MainMenuNode:        FrmGotoForm(MainForm);        return true;
	case MainMenuMap:         FrmGotoForm(MapForm);         return true;
	case MainMenuStats:       FrmGotoForm(StatsForm);       return true;
	case MainMenuPackets:     FrmGotoForm(PacketsForm);     return true;
	case MainMenuTrace:       TraceFormOpen(NULL);          return true;
	case MainMenuBeamSelf:    BeamSelf();                   return true;
	case MainMenuBlocked:     BlocksManage(ContactsBlockedList); return true;
	case MainMenuStale:
		if (StaleRemove())
			FrmUpdateForm(FrmGetActiveFormID(), VM_UPDATE_DATA);
		return true;
	case MainMenuBeamChannel:
		if (FrmGetActiveFormID() == ConvoForm && gAppConv.type == VM_CONV_CHANNEL)
			BeamChannel(gAppConv.channel);
		else
			FrmCustomAlert(SettingsErrorAlert, "Open the channel you want to beam first.", "", "");
		return true;
	case MainMenuPorts:       FrmGotoForm(PortsForm);       return true;
	case MainMenuFontPreview: FrmGotoForm(FontPreviewForm); return true;
	case MainMenuSounds:      SoundsFormOpen(FrmGetActiveFormID()); return true;
	case MainMenuAddChannel:  FrmGotoForm(ChannelForm);     return true;
	case MainMenuLeaveChannel: ChannelLeaveCurrent();       return true;
	case MainMenuMuteChannel: ChannelMuteToggleCurrent();   return true;
	case MainMenuChannelScope: ChannelScopeChooseCurrent(); return true;
	case MainMenuScopes:      ScopesFormOpen(FrmGetActiveFormID()); return true;
	case MainMenuShot:        ShotCapture();                return true;   /* the menu has gone by now */
	case MainMenuShotLater:   ShotLater(5);                 return true;
	case MainMenuAbout:       AppAbout();                   return true;
	case MainMenuConnect:     Connect();                    return true;
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
		/* the Channel menu only for a channel */
		FrmSetMenu(form, gAppConv.type == VM_CONV_CHANNEL ? ConvoChanMenuBar : ConvoMenuBar);
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
	case StatsForm:       FrmSetEventHandler(form, StatsFormHandleEvent);       break;
	case PacketsForm:     FrmSetEventHandler(form, PacketsFormHandleEvent);     break;
	case TraceForm:       FrmSetEventHandler(form, TraceFormHandleEvent);       break;
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

/*
 * Reconnects after a wake or a lost link run in steps (SessionConnectStep on
 * idle events), so the app stays usable meanwhile. One that fails (the USB
 * link isn't back yet, the bridge is stopped) is tried again 2, 4, 8 ... 256 s
 * later (8.5 min in all).
 */
#define VM_RETRIES   8
#define VM_STEP_TICKS (SysTicksPerSecond() / 4)
static UInt32 gRetryAt = 0;    /* ticks; 0: no retry due */
static UInt16 gRetries = 0;

static void ScheduleRetry(void)
{
	if (gRetries >= VM_RETRIES)
		return;
	gRetries++;
	gRetryAt = TimGetTicks() + ((UInt32)SysTicksPerSecond() << gRetries);
	if (!gRetryAt)
		gRetryAt = 1;
}

static void Reconnected(Err err)
{
	if (err == errNone) {
		gWantConnection = false;
		gRetries = 0;
	} else {
		ScheduleRetry();
	}
	FrmUpdateForm(FrmGetActiveFormID(), VM_UPDATE_DATA);
}

static void StartReconnect(void)
{
	Err err;

	gRetryAt = 0;
	err = SessionConnectStart();
	if (err == vmErrPending)
		FrmUpdateForm(FrmGetActiveFormID(), VM_UPDATE_DATA);    /* "connecting" */
	else
		Reconnected(err);
}

/* On idle events: the next reconnect step, or a retry that's due. */
static void StepReconnect(void)
{
	Err err;

	if (SessionConnecting()) {
		err = SessionConnectStep();
		if (err != vmErrPending)
			Reconnected(err);
	} else if (gRetryAt && !SessionIsConnected() && (Int32)(TimGetTicks() - gRetryAt) >= 0) {
		StartReconnect();
	}
}

/* Options > Connect, or a tap on "offline": in one go, with progress in the title on Home and conversations. */
static void Connect(void)
{
	UInt16 formId = FrmGetActiveFormID();

	gRetryAt = 0;
	if (AppConnect(formId == HomeForm || formId == ConvoForm ? TitleStatus : NULL, false) == errNone) {
		gWantConnection = false;
		gRetries = 0;
	}
	FrmUpdateForm(formId, VM_UPDATE_DATA);
}

/* How long to wait for an event: poll while connected, step a reconnect, wake for a retry that's due. */
static Int32 EventTimeout(void)
{
	Int32 left;

	if (BeamInboxWaiting())
		return 1;               /* a beam came: ask about it on the next nilEvent */
	if (SessionConnecting())
		return VM_STEP_TICKS;
	if (SessionIsConnected())
		return VM_POLL_TICKS;
	if (!gRetryAt)
		return evtWaitForever;
	left = (Int32)(gRetryAt - TimGetTicks());
	return left > 1 ? left : 1;
}

static void CheckWake(void)
{
	UInt32 secs = TimGetSeconds(), ticks = TimGetTicks();
	Int32 wall, run;

	if (gLastSecs) {
		wall = (Int32)(secs - gLastSecs);
		run = (Int32)((ticks - gLastTicks) / SysTicksPerSecond());
		if (wall - run > VM_SLEEP_GAP_SECS && (SessionIsConnected() || SessionConnecting() || gWantConnection)) {
			gWantConnection = true;
			gRetries = 0;
			StartReconnect();
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
	else if (formId == StatsForm)
		StatsFormIdle();
	else if (formId == PacketsForm)
		PacketsFormIdle();
}

static void AppEventLoop(void)
{
	EventType event;
	Err err;

	do {
		if (SessionTakeLost()) {
			/* the radio stopped answering: show offline and keep trying, as after a wake */
			gWantConnection = true;
			gRetries = 0;
			gRetryAt = TimGetTicks() + 2 * SysTicksPerSecond();
			FrmUpdateForm(FrmGetActiveFormID(), VM_UPDATE_DATA);
		}
		EvtGetEvent(&event, EventTimeout());
		CheckWake();
		if (event.eType == nilEvent) {
			if (BeamInboxWaiting())
				BeamCheckInbox();
			StepReconnect();
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
		ExgRegisterData(VM_CREATOR, exgRegExtensionID, VM_BEAM_EXT);   /* beams named *.vmsh come here */
		BgSchedule();
		return errNone;
	case sysAppLaunchCmdTimeChange:
		BgSchedule();
		return errNone;
	case sysAppLaunchCmdExgReceiveData:
		/* maybe without globals: beam_rx.c only saves it for later */
		return BeamReceive((ExgSocketPtr)cmdPBP);
	}
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		BgSetAppOpen(true);
		AppLoadPrefs();
		BeamInboxNote();
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
		ReactionsClose();
		UiFontRelease();
		ShotCancel();
		BgSetAppOpen(false);
		if (SessionDevice()->fwVerCode)
			BgRadioSeen();      /* the radio's back: check at the normal interval */
		BgScheduleIfIdle();
	}
	return errNone;
}
