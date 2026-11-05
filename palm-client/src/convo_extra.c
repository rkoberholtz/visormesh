#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "session.h"
#include "channels.h"
#include "channel_form.h"
#include "contacts.h"
#include "messages.h"
#include "sha256.h"
#include "ui/text.h"
#include "convo_extra.h"

static Boolean Have(Char (*names)[VM_NAME_SIZE], UInt16 n, const Char *name) VM_SECT5;

#define PICK_MAX     40
#define PICK_RECENT  20    /* senders from the conversation, newest first, before the contacts */

void ConvoOpenHashtag(const Char *tag)
{
	UInt8 utf8[VM_NAME_SIZE], hash[32];
	UInt16 n = TxtPalmToUtf8(tag, StrLen(tag), utf8, VM_NAME_SIZE - 1), i;
	UInt8 maxChannels = SessionDevice()->maxChannels;
	VmChannel c;
	VmConv conv;

	/* joined already (by its key, whatever it's called here): just open it */
	if (n) {
		VmSha256(utf8, n, hash);
		ChannelsOpen();
		for (i = 0; i < (maxChannels ? maxChannels : 8); i++) {
			if (ChannelsFindSlot((UInt8)i, &c) && MemCmp(c.secret, hash, VM_CHANNEL_SECRET_SIZE) == 0) {
				MemSet(&conv, sizeof(conv), 0);
				conv.type = VM_CONV_CHANNEL;
				conv.channel = (UInt8)i;
				AppOpenConversation(&conv);
				return;
			}
		}
	}
	if (FrmCustomAlert(HashtagAlert, tag, "", "") == 0)
		ChannelJoin(tag, NULL);
}

static Boolean Have(Char (*names)[VM_NAME_SIZE], UInt16 n, const Char *name)
{
	UInt16 i;

	for (i = 0; i < n; i++)
		if (StrCompare(names[i], name) == 0)
			return true;
	return false;
}

Boolean ConvoMentionPick(UInt16 listId, UInt16 fieldId, Boolean typedAt)
{
	FormType *form = FrmGetActiveForm();
	ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, listId));
	FieldType *field = FrmGetObjectPtr(form, FrmGetObjectIndex(form, fieldId));
	Char (*names)[VM_NAME_SIZE];
	Char **items;
	const VmMsgHeader *h;
	UInt16 n = 0, i, count;
	VmContact c;
	Char buf[VM_NAME_SIZE + 5];
	Int16 pick;

	names = MemPtrNew(PICK_MAX * VM_NAME_SIZE);
	items = MemPtrNew(PICK_MAX * sizeof(Char *));
	if (!names || !items) {
		if (names)
			MemPtrFree(names);
		if (items)
			MemPtrFree(items);
		return false;
	}
	/* who spoke here lately, then the companions in the contacts */
	count = MessagesCount();
	for (i = count; i > 0 && n < PICK_RECENT; i--) {
		h = MessagesLock(i - 1);
		if (!h)
			continue;
		if (h->dir == VM_DIR_IN && h->sender[0] && ConvEqual(&h->conv, &gAppConv) && !Have(names, n, h->sender))
			StrCopy(names[n++], h->sender);
		MessagesRelease(i - 1);
	}
	count = ContactsCount();
	for (i = 0; i < count && n < PICK_MAX; i++)
		if (ContactsGet(i, &c) && c.type == VM_ADV_TYPE_CHAT && c.name[0] && !Have(names, n, c.name))
			StrCopy(names[n++], c.name);
	for (i = 0; i < n; i++)
		items[i] = names[i];
	pick = -1;
	if (n) {
		LstSetListChoices(list, items, n);
		LstSetHeight(list, n < 8 ? n : 8);
		pick = LstPopupList(list);
		LstSetListChoices(list, NULL, 0);
	}
	FrmSetFocus(form, FrmGetObjectIndex(form, fieldId));
	if (pick >= 0) {
		StrPrintF(buf, "@[%s] ", names[pick]);
		FldInsert(field, buf, StrLen(buf));
	} else if (typedAt) {
		FldInsert(field, "@", 1);
	}
	MemPtrFree(items);
	MemPtrFree(names);
	return pick >= 0 || typedAt;
}
