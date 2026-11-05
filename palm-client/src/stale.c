#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "session.h"
#include "contacts.h"
#include "messages.h"
#include "ui/title.h"
#include "stale.h"

static UInt16 TalkedTo(UInt8 (*keys)[6], UInt16 max) VM_SECT5;
static Boolean Listed(UInt8 (*keys)[6], UInt16 n, const UInt8 *key) VM_SECT5;

#define TALKED_MAX 100

/* The contacts there are messages with (DMs and room posts), by key prefix. */
static UInt16 TalkedTo(UInt8 (*keys)[6], UInt16 max)
{
	const VmMsgHeader *h;
	UInt16 i, n = 0, count;

	MessagesOpen();
	count = MessagesCount();
	for (i = 0; i < count; i++) {
		h = MessagesLock(i);
		if (!h)
			continue;
		if (h->conv.type == VM_CONV_DIRECT && !Listed(keys, n, h->conv.key) && n < max)
			MemMove(keys[n++], h->conv.key, 6);
		MessagesRelease(i);
	}
	return n;
}

static Boolean Listed(UInt8 (*keys)[6], UInt16 n, const UInt8 *key)
{
	UInt16 i;

	for (i = 0; i < n; i++)
		if (MemCmp(keys[i], key, 6) == 0)
			return true;
	return false;
}

Boolean StaleRemove(void)
{
	UInt8 (*talked)[6];
	UInt8 (*gone)[VM_PUB_KEY_SIZE];
	UInt16 pick, i, n = 0, nTalked, count, removed = 0;
	UInt32 days, now = SessionNow();
	VmContact c;
	Char what[48], msg[48];
	Err err = errNone;

	if (!SessionIsConnected()) {
		FrmCustomAlert(SettingsErrorAlert, "Connect to the radio first (Node).", "", "");
		return false;
	}
	pick = FrmAlert(StaleAlert);
	if (pick > 2)
		return false;
	days = pick == 0 ? 14 : pick == 1 ? 30 : 90;
	count = ContactsCount();
	talked = MemPtrNew(TALKED_MAX * 6);
	gone = count ? MemPtrNew(count * VM_PUB_KEY_SIZE) : NULL;
	if (!talked || !gone) {
		if (talked)
			MemPtrFree(talked);
		if (gone)
			MemPtrFree(gone);
		if (count == 0)
			FrmCustomAlert(NoteAlert, "There are no contacts.", "", "");
		return false;
	}
	nTalked = TalkedTo(talked, TALKED_MAX);
	for (i = 0; i < count; i++) {
		if (!ContactsGet(i, &c) || c.lastAdvert == 0 || c.lastAdvert + days * 86400UL > now)
			continue;
		if (Listed(talked, nTalked, c.pubKey) || SessionIsLoggedIn(c.pubKey))
			continue;
		MemMove(gone[n++], c.pubKey, VM_PUB_KEY_SIZE);
	}
	if (n == 0) {
		StrPrintF(msg, "Every contact was heard in the last %lu days.", days);
		FrmCustomAlert(NoteAlert, msg, "", "");
	} else {
		StrPrintF(what, "%u contact%s not heard in %lu days", n, n == 1 ? "" : "s", days);
		if (FrmCustomAlert(StaleConfirmAlert, what, "", "") == 0) {
			TitleStatusReset();
			for (i = 0; i < n && err == errNone; i++) {
				if (i % 10 == 0) {
					StrPrintF(msg, "removing %u", n - i);
					TitleStatus(msg);
				}
				err = SessionKeyCommand(VM_CMD_REMOVE_CONTACT, gone[i]);
				if (err == vmErrRadio)
					err = errNone;     /* already gone */
				else if (err == errNone)
					removed++;
			}
			TitleStatus("");
			if (err == errNone)
				err = ContactsRefresh(NULL);   /* removals only show in a full download */
			if (err != errNone) {
				AppErrorText(err, msg);
				FrmCustomAlert(SettingsErrorAlert, msg, "", "");
			}
		}
	}
	MemPtrFree(gone);
	MemPtrFree(talked);
	return removed > 0;
}
