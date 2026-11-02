#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "channels.h"
#include "mutes.h"

static Int16 FindMute(const VmChannel *c) VM_SECT3;
static void SavePrefs(void) VM_SECT3;

#define VM_MUTES_PREFS_ID      2
#define VM_MUTES_PREFS_VERSION 1
#define VM_MUTES_MAX           16
#define VM_MUTE_ID_SIZE        4    /* leading bytes of the channel key */

typedef struct {
	UInt16 version;
	UInt16 count;
	UInt8  ids[VM_MUTES_MAX][VM_MUTE_ID_SIZE];
} MutesPrefs;

static MutesPrefs gMutes;

void MutesLoad(void)
{
	UInt16 size = sizeof(gMutes);

	if (PrefGetAppPreferences(VM_CREATOR, VM_MUTES_PREFS_ID, &gMutes, &size, true) == noPreferenceFound ||
	    size != sizeof(gMutes) || gMutes.version != VM_MUTES_PREFS_VERSION || gMutes.count > VM_MUTES_MAX)
		gMutes.count = 0;
	gMutes.version = VM_MUTES_PREFS_VERSION;
}

static void SavePrefs(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_MUTES_PREFS_ID, VM_MUTES_PREFS_VERSION, &gMutes, sizeof(gMutes), true);
}

static Int16 FindMute(const VmChannel *c)
{
	UInt16 i;

	for (i = 0; i < gMutes.count; i++)
		if (MemCmp(gMutes.ids[i], c->secret, VM_MUTE_ID_SIZE) == 0)
			return (Int16)i;
	return -1;
}

Boolean ChannelMuted(const VmChannel *c)
{
	return FindMute(c) >= 0;
}

Boolean ChannelSlotMuted(UInt8 slot)
{
	VmChannel c;

	return ChannelsFindSlot(slot, &c) && ChannelMuted(&c);
}

void ChannelMuteToggleCurrent(void)
{
	VmChannel c;
	Char msg[VM_NAME_SIZE + 48];
	Int16 i;

	if (FrmGetActiveFormID() != ConvoForm || gAppConv.type != VM_CONV_CHANNEL ||
	    !ChannelsFindSlot(gAppConv.channel, &c)) {
		FrmCustomAlert(MuteAlert, "Open the channel you want to mute first.", "", "");
		return;
	}
	i = FindMute(&c);
	if (i >= 0) {
		MemMove(gMutes.ids[i], gMutes.ids[i + 1], (gMutes.count - i - 1) * VM_MUTE_ID_SIZE);
		gMutes.count--;
		StrPrintF(msg, "%s is no longer muted.", c.name);
	} else if (gMutes.count >= VM_MUTES_MAX) {
		StrCopy(msg, "Too many muted channels: unmute one first.");
		FrmCustomAlert(MuteAlert, msg, "", "");
		return;
	} else {
		MemMove(gMutes.ids[gMutes.count++], c.secret, VM_MUTE_ID_SIZE);
		StrPrintF(msg, "%s is muted: no sounds, except when you're mentioned.", c.name);
	}
	SavePrefs();
	FrmCustomAlert(MuteAlert, msg, "", "");
}
