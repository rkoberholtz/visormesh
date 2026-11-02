#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "session.h"
#include "sounds.h"

static void SavePrefs(void) VM_SECT3;

#define VM_SOUNDS_PREFS_ID      1
#define VM_SOUNDS_PREFS_VERSION 1

/* Off by default: the busy channels. */
#define VM_SND_DEFAULT ((UInt16)~((1 << VM_SND_HASHTAG) | (1 << VM_SND_PUBLIC)))

typedef struct {
	UInt16 version;
	UInt16 enabled;    /* 1 << VM_SND_* */
} SoundsPrefs;

static UInt16 gEnabled = VM_SND_DEFAULT;
static UInt16 gReturnForm = HomeForm;

/* Up to three notes per kind as (Hz, ms) pairs; 0 ends a tune early. */
static const UInt16 kTunes[VM_SND_COUNT][6] = {
	{ 1760, 70, 2349, 90, 0, 0 },          /* direct: rising pair */
	{ 2093, 50, 2093, 50, 2637, 90 },      /* mention: double tap, then up */
	{ 1319, 70, 1760, 90, 0, 0 },          /* room: lower rising pair */
	{ 1568, 60, 1976, 60, 0, 0 },          /* private channel */
	{ 1397, 60, 0, 0, 0, 0 },              /* hashtag: one blip */
	{ 1175, 40, 0, 0, 0, 0 },              /* public: shorter, lower blip */
	{ 2637, 60, 1976, 60, 1568, 80 },      /* new contact: falling three */
};

void SoundsLoad(void)
{
	SoundsPrefs prefs;
	UInt16 size = sizeof(prefs);

	if (PrefGetAppPreferences(VM_CREATOR, VM_SOUNDS_PREFS_ID, &prefs, &size, true) != noPreferenceFound &&
	    size == sizeof(prefs) && prefs.version == VM_SOUNDS_PREFS_VERSION)
		gEnabled = prefs.enabled;
}

static void SavePrefs(void)
{
	SoundsPrefs prefs;

	prefs.version = VM_SOUNDS_PREFS_VERSION;
	prefs.enabled = gEnabled;
	PrefSetAppPreferences(VM_CREATOR, VM_SOUNDS_PREFS_ID, VM_SOUNDS_PREFS_VERSION, &prefs, sizeof(prefs), true);
}

void SoundsPlay(UInt8 kind)
{
	SndCommandType cmd;
	UInt16 amp = (UInt16)PrefGetPreference(prefAlarmSoundVolume);
	UInt16 i;

	if (kind >= VM_SND_COUNT || amp == 0)
		return;
	for (i = 0; i < 6 && kTunes[kind][i]; i += 2) {
		if (i)
			SysTaskDelay(1);   /* a short gap so repeated notes stay separate */
		cmd.cmd = sndCmdFreqDurationAmp;
		cmd.param1 = kTunes[kind][i];
		cmd.param2 = kTunes[kind][i + 1];
		cmd.param3 = amp;
		SndDoCmd(NULL, &cmd, false);
	}
}

void SoundsNotify(UInt16 kinds, const VmConv *onScreen)
{
	UInt8 kind;

	for (kind = 0; kind < VM_SND_COUNT; kind++) {
		if (!(kinds & (1 << kind)) || !(gEnabled & (1 << kind)))
			continue;
		/* a new contact has no conversation to be looking at */
		if (kind != VM_SND_CONTACT && onScreen && ConvEqual(SessionSoundConv(kind), onScreen))
			continue;
		SoundsPlay(kind);
		return;
	}
}

void SoundsFormOpen(UInt16 returnForm)
{
	if (returnForm != SoundsForm)
		gReturnForm = returnForm;
	FrmGotoForm(SoundsForm);
}

Boolean SoundsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	UInt16 id;
	UInt8 kind;

	switch (event->eType) {
	case frmOpenEvent:
		for (kind = 0; kind < VM_SND_COUNT; kind++)
			FrmSetControlValue(form, FrmGetObjectIndex(form, SoundsFirstCheck + kind),
			                   (gEnabled & (1 << kind)) != 0);
		FrmDrawForm(form);
		return true;
	case ctlSelectEvent:
		id = event->data.ctlSelect.controlID;
		if (id == SoundsDoneButton) {
			FrmGotoForm(gReturnForm);
			return true;
		}
		if (id >= SoundsFirstCheck && id < SoundsFirstCheck + VM_SND_COUNT) {
			kind = (UInt8)(id - SoundsFirstCheck);
			if (event->data.ctlSelect.on) {
				gEnabled |= 1 << kind;
				SoundsPlay(kind);   /* turning one on is how you hear it */
			} else {
				gEnabled &= ~(1 << kind);
			}
			SavePrefs();
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
