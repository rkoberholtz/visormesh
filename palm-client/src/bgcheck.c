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

/* What the last check did, for Preferences (unsaved prefs: not backed up). */
#define VM_BG_STATUS_ID      6
#define VM_BG_STATUS_VERSION 1
#define VM_BG_LAUNCHING  1    /* the check started and didn't finish */
#define VM_BG_LAUNCH_ERR 2    /* SysAppLaunch failed: err */
#define VM_BG_HOTSYNC    3    /* skipped: HotSync was running */
#define VM_BG_NO_RADIO   4    /* couldn't connect: err */
#define VM_BG_DONE       5    /* checked: kinds found */

typedef struct {
	UInt16 version;
	UInt16 stage;
	UInt32 when;     /* TimGetSeconds */
	UInt16 err;
	UInt16 kinds;
} BgStatus;

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

static void WriteStatus(UInt16 stage, Err err, UInt16 kinds)
{
	BgStatus st;

	st.version = VM_BG_STATUS_VERSION;
	st.stage = stage;
	st.when = TimGetSeconds();
	st.err = err;
	st.kinds = kinds;
	PrefSetAppPreferences(VM_CREATOR, VM_BG_STATUS_ID, VM_BG_STATUS_VERSION, &st, sizeof(st), false);
}

void BgSchedule(void)
{
	UInt16 minutes = BgInterval();

	SetAlarm(minutes ? TimGetSeconds() + (UInt32)minutes * 60 : 0);
}

void BgAlarmTriggered(SysAlarmTriggeredParamType *p)
{
	UInt16 card, curCard;
	LocalID id, curId;
	UInt32 creator = 0, shown = 0, result;
	UInt16 kinds = 0;

	Err err;

	p->purgeAlarm = true;
	BgSchedule();       /* first, so a failed check doesn't end the chain */
	if (!FindSelf(&card, &id))
		return;
	if (SysCurAppDatabase(&curCard, &curId) == errNone) {
		if (curId == id)
			return;     /* the app is open and checks for itself */
		DmDatabaseInfo(curCard, curId, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		               NULL, NULL, NULL, &creator);
	}
	if (creator == sysFileCSync) {
		WriteStatus(VM_BG_HOTSYNC, errNone, 0);   /* HotSync has the USB port */
		return;
	}
	WriteStatus(VM_BG_LAUNCHING, errNone, 0);     /* BgCheckRun overwrites it */
	err = SysAppLaunch(card, id, sysAppLaunchFlagNewGlobals, VM_LAUNCH_BGCHECK, &kinds, &result);
	if (err != errNone)
		WriteStatus(VM_BG_LAUNCH_ERR, err, 0);
	if (err != errNone || !kinds)
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

	Err err;

	AppLoadPrefs();
	err = SessionConnectLight();
	if (err == errNone) {
		for (n = 0; n < VM_BG_SYNC_ROUNDS && SessionMessageWaiting(); n++)
			SessionSyncMessages();
		SessionDisconnect();
	}
	kinds = SessionTakeSounds() & SoundsEnabledKinds();
	SoundsNotify(kinds, NULL);
	WriteStatus(err == errNone ? VM_BG_DONE : VM_BG_NO_RADIO, err, kinds);
	ContactsClose();
	ChannelsClose();
	MessagesClose();
	return kinds;
}

/* "18:05" (24-hour: the label has no room for am/pm). */
static void AppendTime(Char *out, UInt32 secs)
{
	DateTimeType dt;

	TimSecondsToDateTime(secs, &dt);
	TimeToAscii((UInt8)dt.hour, (UInt8)dt.minute, tfColon24h, out + StrLen(out));
}

void BgStatusText(Char *out)
{
	BgStatus st;
	UInt16 size = sizeof(st), card, n, kind;
	LocalID id;
	UInt32 ref, next = 0;

	out[0] = 0;
	if (PrefGetAppPreferences(VM_CREATOR, VM_BG_STATUS_ID, &st, &size, false) != noPreferenceFound &&
	    size == sizeof(st) && st.version == VM_BG_STATUS_VERSION) {
		StrCopy(out, "Last ");
		AppendTime(out, st.when);
		switch (st.stage) {
		case VM_BG_LAUNCHING:  StrCat(out, " stopped"); break;
		case VM_BG_HOTSYNC:    StrCat(out, " HotSync"); break;
		case VM_BG_NO_RADIO:   StrCat(out, " no radio"); break;
		case VM_BG_LAUNCH_ERR:
			StrCat(out, " err ");
			StrIToA(out + StrLen(out), st.err);
			break;
		case VM_BG_DONE:
			for (n = 0, kind = 0; kind < VM_SND_COUNT; kind++)
				if (st.kinds & (1 << kind))
					n++;
			StrCat(out, n ? " new" : " ok");
			break;
		}
	} else {
		StrCopy(out, "Not checked yet");
	}
	if (FindSelf(&card, &id))
		next = AlmGetAlarm(card, id, &ref);
	StrCat(out, next ? ", next " : ", no alarm");
	if (next)
		AppendTime(out, next);
}
