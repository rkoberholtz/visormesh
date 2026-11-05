#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "session.h"
#include "contacts.h"
#include "channels.h"
#include "channel_form.h"
#include "protocol2.h"
#include "ui/text.h"
#include "beam.h"

static void Send(const Char *link, UInt16 len, const Char *what) VM_SECT5;
static Boolean Handle(const Char *link, UInt16 len, Boolean *keep) VM_SECT5;

static void Send(const Char *link, UInt16 len, const Char *what)
{
	ExgSocketType s;
	Char name[VM_NAME_SIZE + 8];
	Err err;

	if (len == 0) {
		FrmCustomAlert(SettingsErrorAlert, "That name is too long to beam.", "", "");
		return;
	}
	StrPrintF(name, "VisorMesh.%s", VM_BEAM_EXT);
	MemSet(&s, sizeof(s), 0);
	s.target = VM_CREATOR;
	s.description = (Char *)what;
	s.name = name;
	s.length = len;
	err = ExgPut(&s);
	if (err == errNone)
		ExgSend(&s, link, len, &err);
	ExgDisconnect(&s, err);
}

void BeamContact(const VmContact *c)
{
	Char link[VM_BEAM_MAX];
	UInt8 utf8[VM_NAME_SIZE];
	UInt16 n = TxtPalmToUtf8(c->name, StrLen(c->name), utf8, sizeof(utf8));

	Send(link, n ? VmShareContact(link, sizeof(link), utf8, n, c->pubKey, c->type) : 0, c->name);
}

void BeamSelf(void)
{
	const VmSelfInfo *self = SessionSelf();
	Char link[VM_BEAM_MAX];
	UInt8 utf8[VM_NAME_SIZE];
	UInt16 n;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first: your key comes from it.", "", "");
		return;
	}
	n = TxtPalmToUtf8(self->name, StrLen(self->name), utf8, sizeof(utf8));
	Send(link, n ? VmShareContact(link, sizeof(link), utf8, n, self->pubKey, self->advType ? self->advType : VM_ADV_TYPE_CHAT) : 0,
	     self->name);
}

void BeamChannel(UInt8 slot)
{
	VmChannel c;
	Char link[VM_BEAM_MAX];
	UInt8 utf8[VM_NAME_SIZE];
	UInt16 n;

	if (!ChannelsFindSlot(slot, &c)) {
		FrmCustomAlert(SettingsErrorAlert, "Open the channel you want to beam first.", "", "");
		return;
	}
	n = TxtPalmToUtf8(c.name, StrLen(c.name), utf8, sizeof(utf8));
	Send(link, n ? VmShareChannel(link, sizeof(link), utf8, n, c.secret) : 0, c.name);
}

void BeamInboxNote(void)
{
	LocalID id = DmFindDatabase(0, VM_INBOX_NAME);
	DmOpenRef db;

	if (!id)
		return;
	db = DmOpenDatabase(0, id, dmModeReadOnly);
	if (!db)
		return;
	if (DmNumRecords(db) > 0)
		FtrSet(VM_CREATOR, VM_FTR_INBOX, 1);
	DmCloseDatabase(db);
}

Boolean BeamInboxWaiting(void)
{
	UInt32 v;

	return FtrGet(VM_CREATOR, VM_FTR_INBOX, &v) == errNone && v;
}

/* One beam: true if it's done with (added, or not something we read); *keep for Later. */
static Boolean Handle(const Char *link, UInt16 len, Boolean *keep)
{
	VmShare s;
	Char name[VM_NAME_SIZE + 1], msg[48];
	UInt16 n, pick;
	Err err;

	*keep = false;
	if (!VmShareParse(link, len, &s))
		return true;
	n = TxtUtf8ToPalm(s.name, s.nameLen, name, VM_NAME_SIZE - 1);
	name[n] = 0;
	if (s.kind == VM_SHARE_CONTACT) {
		pick = FrmCustomAlert(InboxContactAlert, name,
		                      s.type == VM_ADV_TYPE_REPEATER ? "repeater" : s.type == VM_ADV_TYPE_ROOM ? "room" :
		                      s.type == VM_ADV_TYPE_SENSOR ? "sensor" : "chat", "");
		if (pick == 2)
			return true;
		if (pick == 1 || !SessionIsConnected()) {
			if (pick == 0)
				FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first. It's kept for when you do.", "", "");
			*keep = true;
			return false;
		}
		err = SessionAddContact(s.key, s.type, name);
		if (err == errNone)
			err = ContactsUpdate();
		if (err != errNone) {
			AppErrorText(err, msg);
			FrmCustomAlert(SettingsErrorAlert, msg, "", "");
			*keep = true;
			return false;
		}
		return true;
	}
	pick = FrmCustomAlert(InboxChannelAlert, name, "", "");
	if (pick == 2)
		return true;
	if (pick == 1) {
		*keep = true;
		return false;
	}
	if (!ChannelJoin(name, s.secret)) {
		*keep = true;
		return false;
	}
	return true;
}

void BeamCheckInbox(void)
{
	LocalID id = DmFindDatabase(0, VM_INBOX_NAME);
	DmOpenRef db;
	MemHandle h;
	Char link[VM_BEAM_MAX];
	UInt16 i, len;
	Boolean keep;

	FtrUnregister(VM_CREATOR, VM_FTR_INBOX);
	if (!id || !(db = DmOpenDatabase(0, id, dmModeReadWrite)))
		return;
	for (i = 0; i < DmNumRecords(db); ) {
		h = DmQueryRecord(db, i);
		if (!h) {
			i++;
			continue;
		}
		len = (UInt16)MemHandleSize(h);
		if (len >= sizeof(link))
			len = sizeof(link) - 1;
		MemMove(link, MemHandleLock(h), len);
		MemHandleUnlock(h);
		link[len] = 0;
		if (Handle(link, len, &keep))
			DmRemoveRecord(db, i);
		else if (keep)
			break;      /* Later: ask again next launch */
		else
			i++;
	}
	DmCloseDatabase(db);
}
