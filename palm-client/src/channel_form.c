/*
 * Add Channel: a #hashtag channel (key = first 16 bytes of SHA-256 of the
 * name, so anyone who knows the name can read it) or a private channel joined
 * with its 32-hex-digit key. Creating a new private channel isn't offered:
 * the Visor has no good source of randomness for the key.
 */
#include <PalmOS.h>
#include "resources.h"
#include "protocol.h"
#include "sha256.h"
#include "session.h"
#include "channels.h"
#include "app.h"
#include "ui/text.h"
#include "channel_form.h"

/* In the fourth code segment: see sections.h for the rules. */
static void *GetObject(FormType *form, UInt16 id) VM_SECT4;
static const Char *FieldText(FormType *form, UInt16 id) VM_SECT4;
static Boolean IsPrivate(FormType *form) VM_SECT4;
static void ShowKeyField(FormType *form) VM_SECT4;
static void Error(const Char *msg) VM_SECT4;
static Boolean HashtagKey(const Char *name, UInt8 *key) VM_SECT4;
static Boolean Add(FormType *form) VM_SECT4;

static void *GetObject(FormType *form, UInt16 id)
{
	return FrmGetObjectPtr(form, FrmGetObjectIndex(form, id));
}

static const Char *FieldText(FormType *form, UInt16 id)
{
	const Char *t = FldGetTextPtr(GetObject(form, id));

	return t ? t : "";
}

static Boolean IsPrivate(FormType *form)
{
	return FrmGetObjectId(form, FrmGetControlGroupSelection(form, ChannelTypeGroup)) == ChannelPrivatePush;
}

/* The key field only matters for a private channel. */
static void ShowKeyField(FormType *form)
{
	UInt16 label = FrmGetObjectIndex(form, ChannelKeyLabel), field = FrmGetObjectIndex(form, ChannelKeyField);

	if (IsPrivate(form)) {
		FrmShowObject(form, label);
		FrmShowObject(form, field);
	} else {
		FrmHideObject(form, label);
		FrmHideObject(form, field);
	}
}

static void Error(const Char *msg)
{
	FrmCustomAlert(SettingsErrorAlert, msg, "", "");
}

/* The key for a #hashtag channel: the first 16 bytes of SHA-256 of its exact name, '#' included. */
static Boolean HashtagKey(const Char *name, UInt8 *key)
{
	UInt8 utf8[VM_NAME_SIZE], hash[32];
	UInt16 n = TxtPalmToUtf8(name, StrLen(name), utf8, VM_NAME_SIZE - 1);

	if (n == 0)
		return false;
	VmSha256(utf8, n, hash);
	MemMove(key, hash, VM_CHANNEL_SECRET_SIZE);
	return true;
}

Boolean ChannelJoin(const Char *name, const UInt8 *secret)
{
	UInt8 key[VM_CHANNEL_SECRET_SIZE];
	UInt8 maxChannels = SessionDevice()->maxChannels;
	Char msg[48];
	Int16 slot;
	UInt16 i;
	VmChannel c;
	VmConv conv;
	Err err;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first.");
		return false;
	}
	if (secret)
		MemMove(key, secret, sizeof(key));
	else if (!HashtagKey(name, key)) {
		Error("That name is too long.");
		return false;
	}
	MemSet(&conv, sizeof(conv), 0);
	conv.type = VM_CONV_CHANNEL;
	ChannelsOpen();     /* opened lazily (AGENTS.md): this can come before Home has shown */
	/* already joined (perhaps by another name): open it */
	for (i = 0; i < maxChannels; i++) {
		if (ChannelsFindSlot((UInt8)i, &c) && MemCmp(c.secret, key, sizeof(key)) == 0) {
			conv.channel = (UInt8)i;
			AppOpenConversation(&conv);
			return true;
		}
	}
	slot = ChannelsFreeSlot(maxChannels);
	if (slot < 0) {
		StrPrintF(msg, "All %u channel slots are in use.", maxChannels);
		Error(msg);
		return false;
	}
	err = ChannelsSet((UInt8)slot, name, key, maxChannels);
	if (err != errNone) {
		AppErrorText(err, msg);
		Error(msg);
		return false;
	}
	conv.channel = (UInt8)slot;
	AppOpenConversation(&conv);
	return true;
}

static Boolean Add(FormType *form)
{
	const Char *typed = FieldText(form, ChannelNameField);
	Char name[VM_NAME_SIZE + 1];
	UInt8 key[VM_CHANNEL_SECRET_SIZE];

	if (!*typed) {
		Error("Enter a channel name.");
		return false;
	}
	if (IsPrivate(form)) {
		StrNCopy(name, typed, VM_NAME_SIZE - 1);
		name[VM_NAME_SIZE - 1] = 0;
		if (!VmParseHex(FieldText(form, ChannelKeyField), key, sizeof(key))) {
			Error("The key is 32 hex digits (0-9, a-f).");
			return false;
		}
		return ChannelJoin(name, key);
	}
	/* hashtag: the key is derived from the exact name, '#' included */
	StrPrintF(name, "%s%s", typed[0] == '#' ? "" : "#", typed);
	return ChannelJoin(name, NULL);
}

Boolean ChannelFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		FrmSetControlGroupSelection(form, ChannelTypeGroup, ChannelHashtagPush);
		FrmDrawForm(form);
		ShowKeyField(form);
		FrmSetFocus(form, FrmGetObjectIndex(form, ChannelNameField));
		return true;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ChannelHashtagPush:
		case ChannelPrivatePush:
			ShowKeyField(form);
			return true;
		case ChannelAddButton:
			Add(form);
			return true;
		case ChannelCancelButton:
			FrmGotoForm(HomeForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

void ChannelLeaveCurrent(void)
{
	if (FrmGetActiveFormID() != ConvoForm || gAppConv.type != VM_CONV_CHANNEL) {
		Error("Open the channel you want to leave first.");
		return;
	}
	ChannelLeave(gAppConv.channel);
}

void ChannelLeave(UInt8 slot)
{
	VmChannel c;
	Char name[VM_NAME_SIZE + 1], msg[48];
	Err err;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first.");
		return;
	}
	StrCopy(name, ChannelsFindSlot(slot, &c) ? c.name : "this channel");
	if (FrmCustomAlert(LeaveChannelAlert, name, "", "") != 0)
		return;
	err = ChannelsRemove(slot, SessionDevice()->maxChannels);
	if (err != errNone) {
		AppErrorText(err, msg);
		Error(msg);
		return;
	}
	FrmGotoForm(HomeForm);
}
