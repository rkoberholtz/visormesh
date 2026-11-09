#include <PalmOS.h>
#include "app.h"
#include "session.h"
#include "sounds.h"
#include "contacts.h"
#include "channels.h"
#include "messages.h"
#include "reactions.h"
#include "mutes.h"
#include "convos.h"
#include "resources.h"
#include "transport.h"
#include "bgcheck.h"

#define VM_BG_PREFS_ID      6
#define VM_BG_PREFS_VERSION 1
#define VM_FTR_APP_OPEN     1    /* feature: set while the app runs normally */
#define VM_BG_SYNC_ROUNDS   4    /* SessionSyncMessages takes up to VM_SYNC_MAX per round */

typedef struct {
	UInt16 version;
	UInt16 minutes;
} BgPrefs;

/* What the last check did, for Preferences (unsaved prefs: not backed up). */
#define VM_BG_STATUS_ID      6
#define VM_BG_STATUS_VERSION 2
#define VM_BG_LAUNCHING  1    /* the check started and didn't finish */
#define VM_BG_LAUNCH_ERR 2    /* SysAppLaunch failed: err */
#define VM_BG_NO_RADIO   4    /* couldn't connect: err */
#define VM_BG_DONE       5    /* checked: kinds found */

typedef struct {
	UInt16 version;
	UInt16 stage;
	UInt32 when;     /* TimGetSeconds */
	UInt16 err;
	UInt16 kinds;
	UInt16 misses;   /* checks in a row that found no radio: they space out (BgSchedule) */
} BgStatus;

#define VM_BG_MAX_BACKOFF 4     /* at most 16 times the interval... */
#define VM_BG_MAX_MINUTES 60    /* ...and no more than an hour, unless the interval is longer */

/*
 * New messages found by checks, per conversation, until the alert shows them
 * (unsaved prefs: the display launch has no globals). Names are looked up
 * while the check has globals.
 */
#define VM_BG_PENDING_ID      7
#define VM_BG_PENDING_VERSION 1
#define VM_BG_PENDING_MAX     5      /* conversations listed; the rest count in 'more' */
#define VM_BG_NAME_LEN        20

typedef struct {
	VmConv conv;
	UInt16 count;
	Char   name[VM_BG_NAME_LEN + 2];   /* even, to keep the entries aligned */
} BgPendingConv;

typedef struct {
	UInt16 version;
	UInt16 total;
	UInt16 more;     /* messages in conversations past VM_BG_PENDING_MAX */
	UInt16 n;
	BgPendingConv convs[VM_BG_PENDING_MAX];
} BgPending;

static void CountConv(BgPending *pend, const VmConv *conv) VM_SECT4;
static void AddPending(UInt32 since) VM_SECT4;

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

static Boolean ReadStatus(BgStatus *st)
{
	UInt16 size = sizeof(*st);

	return PrefGetAppPreferences(VM_CREATOR, VM_BG_STATUS_ID, st, &size, false) != noPreferenceFound &&
	       size == sizeof(*st) && st->version == VM_BG_STATUS_VERSION;
}

/* Keeps the count of checks without a radio: up one for VM_BG_NO_RADIO, cleared by VM_BG_DONE. */
static void WriteStatus(UInt16 stage, Err err, UInt16 kinds)
{
	BgStatus st;
	UInt16 misses = ReadStatus(&st) ? st.misses : 0;

	st.version = VM_BG_STATUS_VERSION;
	st.stage = stage;
	st.when = TimGetSeconds();
	st.err = err;
	st.kinds = kinds;
	st.misses = stage == VM_BG_NO_RADIO ? misses + 1 : stage == VM_BG_DONE ? 0 : misses;
	PrefSetAppPreferences(VM_CREATOR, VM_BG_STATUS_ID, VM_BG_STATUS_VERSION, &st, sizeof(st), false);
}

static Boolean LoadPending(BgPending *pend)
{
	UInt16 size = sizeof(*pend);

	return PrefGetAppPreferences(VM_CREATOR, VM_BG_PENDING_ID, pend, &size, false) != noPreferenceFound &&
	       size == sizeof(*pend) && pend->version == VM_BG_PENDING_VERSION;
}

static void SavePending(BgPending *pend)
{
	pend->version = VM_BG_PENDING_VERSION;
	PrefSetAppPreferences(VM_CREATOR, VM_BG_PENDING_ID, VM_BG_PENDING_VERSION, pend, sizeof(*pend), false);
}

static void ClearPending(void)
{
	BgPending pend;

	MemSet(&pend, sizeof(pend), 0);
	SavePending(&pend);
}

/*
 * Without a radio (out of the cradle, say) each check wakes the Visor and
 * holds the port for seconds, so after misses the gap doubles, up to an hour.
 */
void BgSchedule(void)
{
	UInt16 minutes = BgInterval(), shift;
	UInt32 delay;
	BgStatus st;

	if (!minutes) {
		SetAlarm(0);
		return;
	}
	delay = minutes;
	if (ReadStatus(&st) && st.misses) {
		shift = st.misses < VM_BG_MAX_BACKOFF ? st.misses : VM_BG_MAX_BACKOFF;
		delay <<= shift;
		if (delay > VM_BG_MAX_MINUTES)
			delay = minutes > VM_BG_MAX_MINUTES ? minutes : VM_BG_MAX_MINUTES;
	}
	SetAlarm(TimGetSeconds() + delay * 60);
}

void BgScheduleIfIdle(void)
{
	UInt16 card;
	LocalID id;
	UInt32 ref;

	if (!FindSelf(&card, &id) || AlmGetAlarm(card, id, &ref) <= TimGetSeconds())
		BgSchedule();
}

void BgRadioSeen(void)
{
	BgStatus st;

	if (!ReadStatus(&st) || !st.misses)
		return;
	st.misses = 0;
	PrefSetAppPreferences(VM_CREATOR, VM_BG_STATUS_ID, VM_BG_STATUS_VERSION, &st, sizeof(st), false);
	BgSchedule();
}

void BgSetAppOpen(Boolean open)
{
	if (open)
		FtrSet(VM_CREATOR, VM_FTR_APP_OPEN, 1);
	else
		FtrUnregister(VM_CREATOR, VM_FTR_APP_OPEN);
}

/*
 * SysCurAppDatabase can't tell whether the app is open: in this launch it
 * names the app itself. While HotSync has the USB port, opening it just fails.
 */
void BgAlarmTriggered(SysAlarmTriggeredParamType *p)
{
	UInt16 card;
	LocalID id;
	UInt32 open, result;
	UInt16 kinds = 0;
	Err err;

	p->purgeAlarm = true;
	BgSchedule();       /* first, so a failed check doesn't end the chain */
	if (!FindSelf(&card, &id))
		return;
	if (FtrGet(VM_CREATOR, VM_FTR_APP_OPEN, &open) == errNone)
		return;         /* the app is open and checks for itself */
	WriteStatus(VM_BG_LAUNCHING, errNone, 0);     /* BgCheckRun overwrites it */
	err = SysAppLaunch(card, id, sysAppLaunchFlagNewGlobals, VM_LAUNCH_BGCHECK, &kinds, &result);
	if (err != errNone)
		WriteStatus(VM_BG_LAUNCH_ERR, err, 0);
	BgSchedule();       /* again: a miss (or a radio found again) changes the gap */
	if (err != errNone || !kinds)
		return;
	p->purgeAlarm = false;     /* the system sends sysAppLaunchCmdDisplayAlarm next */
}

void BgDisplayAlarm(void)
{
	BgPending pend;
	UInt16 card, i;
	LocalID id;
	Char text[24 + VM_BG_PENDING_MAX * (VM_BG_NAME_LEN + 9) + 24];

	if (!LoadPending(&pend) || !pend.total)
		return;
	ClearPending();
	StrIToA(text, pend.total);
	StrCat(text, pend.total == 1 ? " new message" : " new messages");
	for (i = 0; i < pend.n; i++) {
		StrCat(text, "\n");
		StrCat(text, pend.convs[i].name);
		StrCat(text, ": ");
		StrIToA(text + StrLen(text), pend.convs[i].count);
	}
	if (pend.more) {
		StrCat(text, "\n");
		StrIToA(text + StrLen(text), pend.more);
		StrCat(text, " more elsewhere");
	}
	if (FrmCustomAlert(BgNewAlert, text, "", "") == 0 && FindSelf(&card, &id))
		SysUIAppSwitch(card, id, sysAppLaunchCmdNormalLaunch, NULL);
}

/* ---- with globals (VM_LAUNCH_BGCHECK) ---- */

static void CountConv(BgPending *pend, const VmConv *conv)
{
	UInt16 i;

	pend->total++;
	for (i = 0; i < pend->n; i++)
		if (ConvEqual(&pend->convs[i].conv, conv)) {
			pend->convs[i].count++;
			return;
		}
	if (pend->n == VM_BG_PENDING_MAX) {
		pend->more++;
		return;
	}
	pend->convs[i].conv = *conv;
	pend->convs[i].count = 1;
	ConvName(conv, pend->convs[i].name, sizeof(pend->convs[i].name));
	pend->n++;
}

/* Adds the messages received since 'since' (outside muted channels) to what the alert will list. */
static void AddPending(UInt32 since)
{
	BgPending pend;
	const VmMsgHeader *h;
	VmChannel ch;
	UInt16 i;
	Boolean newer;

	if (!LoadPending(&pend))
		MemSet(&pend, sizeof(pend), 0);
	for (i = MessagesCount(); i-- > 0; ) {
		h = MessagesLock(i);
		if (!h)
			continue;
		newer = h->received >= since;
		if (newer && h->dir == VM_DIR_IN &&
		    !(h->conv.type == VM_CONV_CHANNEL && ChannelsFindSlot(h->conv.channel, &ch) && ChannelMuted(&ch)))
			CountConv(&pend, &h->conv);
		MessagesRelease(i);
		if (!newer)
			break;     /* records are in arrival order */
	}
	SavePending(&pend);
}

void BgArmWake(void)
{
	SbWakeArm(BgInterval() != 0 && TransportGetPort() == VM_PORT_SBLORA);
}

UInt16 BgCheckRun(void)
{
	UInt16 kinds, n;
	UInt32 start;
	Err err;

	AppLoadPrefs();
	/* the light connect doesn't refresh them, which is what opens them in the app */
	ChannelsOpen();
	ContactsOpen();
	start = SessionNow();
	err = SessionConnectLight();
	if (err == errNone) {
		for (n = 0; n < VM_BG_SYNC_ROUNDS && SessionMessageWaiting(); n++)
			SessionSyncMessages();
		SessionDisconnect();
	}
	kinds = SessionTakeSounds() & SoundsEnabledKinds();
	SoundsNotify(kinds, NULL);
	if (kinds)
		AddPending(start);
	WriteStatus(err == errNone ? VM_BG_DONE : VM_BG_NO_RADIO, err, kinds);
	BgArmWake();        /* the connect took the slot's handler over */
	ContactsClose();
	ChannelsClose();
	MessagesClose();
	ReactionsClose();
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
	UInt16 card, n, kind;
	LocalID id;
	UInt32 ref, next = 0;

	out[0] = 0;
	if (ReadStatus(&st)) {
		StrCopy(out, "Last ");
		AppendTime(out, st.when);
		switch (st.stage) {
		case VM_BG_LAUNCHING:  StrCat(out, " stopped"); break;
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
