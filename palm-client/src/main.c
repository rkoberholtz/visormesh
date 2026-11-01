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
#include "node_form.h"
#include "settings.h"
#include "ports.h"
#include "preview_form.h"
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
	case MainMenuPorts:       FrmGotoForm(PortsForm);       return true;
	case MainMenuFontPreview: FrmGotoForm(FontPreviewForm); return true;
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
	case ConvoForm:       FrmSetEventHandler(form, ConvoFormHandleEvent);       break;
	case ContactsForm:    FrmSetEventHandler(form, ContactsFormHandleEvent);    break;
	case MainForm:        FrmSetEventHandler(form, NodeFormHandleEvent);        break;
	case SettingsForm:    FrmSetEventHandler(form, SettingsFormHandleEvent);    break;
	case PortsForm:       FrmSetEventHandler(form, PortsFormHandleEvent);       break;
	case FontPreviewForm: FrmSetEventHandler(form, FontPreviewFormHandleEvent); break;
	}
	return true;
}

/* While connected, wake regularly to handle what the radio sends and refresh the screen on show. */
static void Poll(void)
{
	UInt16 formId = FrmGetActiveFormID();

	if (SessionPoll())
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
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		UiFontInit();
		AppLoadPrefs();
		FrmGotoForm(HomeForm);
		AppEventLoop();
		SessionDisconnect();
		FrmCloseAllForms();
		ContactsClose();
		ChannelsClose();
		MessagesClose();
		UiFontRelease();
	}
	return errNone;
}
