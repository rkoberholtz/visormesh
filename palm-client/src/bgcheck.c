#include <PalmOS.h>
#include "app.h"
#include "session.h"
#include "sounds.h"
#include "contacts.h"
#include "channels.h"
#include "messages.h"
#include "resources.h"
#include "bgcheck.h"

#define VM_BG_PREFS_ID      6
#define VM_BG_PREFS_VERSION 1
#define VM_FTR_BG_KINDS     0    /* feature: sound kinds found by checks, until shown */
#define VM_BG_SYNC_ROUNDS   4    /* SessionSyncMessages takes up to VM_SYNC_MAX per round */

typedef struct {
	UInt16 version;
	UInt16 minutes;
} BgPrefs;

/* Everything up to BgCheckRun runs in launches without globals: no globals, no extra segments. */

UInt16 BgInterval(void)
{
	BgPrefs prefs;
	UInt16 size = sizeof(prefs);

	if (PrefGetAppPreferences(VM_CREATOR, VM_BG_PREFS_ID, &prefs, &size, true) != noPreferenceFound &&
	    size == sizeof(prefs) && prefs.version == VM_BG_PREFS_VERSION)
		return prefs.minutes;
	return 0;
}

void BgSetInterval(UInt16 minutes)
{
	BgPrefs prefs;

	prefs.version = VM_BG_PREFS_VERSION;
	prefs.minutes = minutes;
	PrefSetAppPreferences(VM_CREATOR, VM_BG_PREFS_ID, VM_BG_PREFS_VERSION, &prefs, sizeof(prefs), true);
}

/* The app's own database: SysCurAppDatabase is whichever app is in front. */
static Boolean FindSelf(UInt16 *card, LocalID *id)
{
	DmSearchStateType state;

	return DmGetNextDatabaseByTypeCreator(true, &state, sysFileTApplication, VM_CREATOR,
	                                      true, card, id) == errNone;
}

static void SetAlarm(UInt32 when)
{
	UInt16 card;
	LocalID id;

	if (FindSelf(&card, &id))
		AlmSetAlarm(card, id, 0, when, true);
}

void BgSchedule(void)
{
	UInt16 minutes = BgInterval();

	SetAlarm(minutes ? TimGetSeconds() + (UInt32)minutes * 60 : 0);
}

void BgCancel(void)
{
	SetAlarm(0);
}

void BgAlarmTriggered(SysAlarmTriggeredParamType *p)
{
	UInt16 card, curCard;
	LocalID id, curId;
	UInt32 creator = 0, shown = 0, result;
	UInt16 kinds = 0;

	p->purgeAlarm = true;
	if (!FindSelf(&card, &id))
		return;
	if (SysCurAppDatabase(&curCard, &curId) == errNone) {
		if (curId == id)
			return;     /* the app is open; it sets the alarm again when it exits */
		DmDatabaseInfo(curCard, curId, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		               NULL, NULL, NULL, &creator);
	}
	BgSchedule();       /* first, so a failed check doesn't end the chain */
	if (creator == sysFileCSync)
		return;         /* HotSync has the USB port */
	if (SysAppLaunch(card, id, sysAppLaunchFlagNewGlobals, VM_LAUNCH_BGCHECK, &kinds, &result) != errNone ||
	    !kinds)
		return;
	FtrGet(VM_CREATOR, VM_FTR_BG_KINDS, &shown);
	FtrSet(VM_CREATOR, VM_FTR_BG_KINDS, shown | kinds);
	p->purgeAlarm = false;     /* the system sends sysAppLaunchCmdDisplayAlarm next */
}

/* One line per kind, in priority order. A switch, not a table: a pointer table needs globals. */
static void AppendKind(Char *text, UInt8 kind)
{
	const Char *name;

	switch (kind) {
	case VM_SND_DIRECT:  name = "Direct message"; break;
	case VM_SND_MENTION: name = "Mention of you"; break;
	case VM_SND_ROOM:    name = "Room post"; break;
	case VM_SND_PRIVATE: name = "Private channel"; break;
	case VM_SND_HASHTAG: name = "Hashtag channel"; break;
	case VM_SND_PUBLIC:  name = "Public channel"; break;
	case VM_SND_CONTACT: name = "New contact"; break;
	default:             return;
	}
	if (text[0])
		StrCat(text, "\n");
	StrCat(text, name);
}

void BgDisplayAlarm(void)
{
	UInt32 kinds = 0;
	UInt16 card;
	LocalID id;
	UInt8 kind;
	Char text[VM_SND_COUNT * 17];

	if (FtrGet(VM_CREATOR, VM_FTR_BG_KINDS, &kinds) != errNone || !kinds)
		return;
	FtrUnregister(VM_CREATOR, VM_FTR_BG_KINDS);
	text[0] = 0;
	for (kind = 0; kind < VM_SND_COUNT; kind++)
		if (kinds & (1 << kind))
			AppendKind(text, kind);
	if (FrmCustomAlert(BgNewAlert, text, "", "") == 0 && FindSelf(&card, &id))
		SysUIAppSwitch(card, id, sysAppLaunchCmdNormalLaunch, NULL);
}

UInt16 BgCheckRun(void)
{
	UInt16 kinds, n;

	AppLoadPrefs();
	if (SessionConnectLight() == errNone) {
		for (n = 0; n < VM_BG_SYNC_ROUNDS && SessionMessageWaiting(); n++)
			SessionSyncMessages();
		SessionDisconnect();
	}
	kinds = SessionTakeSounds() & SoundsEnabledKinds();
	SoundsNotify(kinds, NULL);
	ContactsClose();
	ChannelsClose();
	MessagesClose();
	return kinds;
}
