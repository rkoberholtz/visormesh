#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "channels.h"
#include "session.h"
#include "contacts.h"
#include "scopes.h"

static Int16 FindChannel(const VmChannel *c) VM_SECT4;
static void SavePrefs(void) VM_SECT4;
static void Error(const Char *msg) VM_SECT4;
static void FillList(FormType *form) VM_SECT4;
static void ShowDefault(FormType *form) VM_SECT4;
static void AddTyped(FormType *form) VM_SECT4;
static void DeleteSelected(FormType *form) VM_SECT4;
static void SetRadioDefault(FormType *form, Boolean clear) VM_SECT4;
static void Discover(FormType *form) VM_SECT4;
static Boolean AddName(const Char *name) VM_SECT4;
static void SetDefaultLabel(FormType *form, const Char *text) VM_SECT4;

#define VM_SCOPES_PREFS_ID      4
#define VM_SCOPES_PREFS_VERSION 1
#define VM_SCOPES_MAX           12
#define VM_SCOPE_CHANS          16
#define VM_CHAN_ID_SIZE         4     /* leading bytes of the channel key */

typedef struct {
	UInt8 id[VM_CHAN_ID_SIZE];
	UInt8 mode;                       /* VM_SCOPE_* */
	Char  name[VM_SCOPE_NAME_SIZE];   /* VM_SCOPE_NAMED: kept even if it leaves the list */
} ChanScope;

typedef struct {
	UInt16    version;
	UInt16    count;
	UInt16    chanCount;
	Char      names[VM_SCOPES_MAX][VM_SCOPE_NAME_SIZE];
	ChanScope chans[VM_SCOPE_CHANS];
} ScopesPrefs;

static ScopesPrefs gScopes;
static UInt16 gReturnForm = HomeForm;

/* List text for the forms: "#name", and the pointers LstSetListChoices keeps. */
static Char  gItems[VM_SCOPES_MAX + 2][VM_SCOPE_NAME_SIZE + 1];
static Char *gItemPtrs[VM_SCOPES_MAX + 2];
static Char  gDefaultText[VM_SCOPE_NAME_SIZE + 2];

void ScopesLoad(void)
{
	UInt16 size = sizeof(gScopes);

	if (PrefGetAppPreferences(VM_CREATOR, VM_SCOPES_PREFS_ID, &gScopes, &size, true) == noPreferenceFound ||
	    size != sizeof(gScopes) || gScopes.version != VM_SCOPES_PREFS_VERSION ||
	    gScopes.count > VM_SCOPES_MAX || gScopes.chanCount > VM_SCOPE_CHANS) {
		MemSet(&gScopes, sizeof(gScopes), 0);
	}
	gScopes.version = VM_SCOPES_PREFS_VERSION;
}

static void SavePrefs(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_SCOPES_PREFS_ID, VM_SCOPES_PREFS_VERSION, &gScopes, sizeof(gScopes), true);
}

static void Error(const Char *msg)
{
	FrmCustomAlert(ScopeAlert, msg, "", "");
}

static Int16 FindChannel(const VmChannel *c)
{
	UInt16 i;

	for (i = 0; i < gScopes.chanCount; i++)
		if (MemCmp(gScopes.chans[i].id, c->secret, VM_CHAN_ID_SIZE) == 0)
			return (Int16)i;
	return -1;
}

UInt8 ScopesForChannel(UInt8 slot, UInt8 key[VM_SCOPE_KEY_SIZE])
{
	VmChannel c;
	Int16 i;

	if (!ChannelsFindSlot(slot, &c) || (i = FindChannel(&c)) < 0)
		return VM_SCOPE_DEFAULT;
	if (gScopes.chans[i].mode == VM_SCOPE_NAMED)
		VmScopeKey(gScopes.chans[i].name, key);
	return gScopes.chans[i].mode;
}

/* ---- Channel Scope dialog ---- */

void ChannelScopeChooseCurrent(void)
{
	if (FrmGetActiveFormID() != ConvoForm || gAppConv.type != VM_CONV_CHANNEL) {
		Error("Open the channel first.");
		return;
	}
	ChannelScopeChoose(gAppConv.channel);
}

void ChannelScopeChoose(UInt8 slot)
{
	VmChannel c;
	FormType *form, *prev;
	ListType *list;
	Int16 i, sel = 0;
	UInt16 n, k;
	ChanScope *cs;

	if (!ChannelsFindSlot(slot, &c))
		return;
	i = FindChannel(&c);
	StrCopy(gItems[0], "Radio default");
	StrCopy(gItems[1], "No scope");
	n = 2;
	for (k = 0; k < gScopes.count; k++, n++)
		StrPrintF(gItems[n], "#%s", gScopes.names[k]);
	if (i >= 0 && gScopes.chans[i].mode == VM_SCOPE_NONE)
		sel = 1;
	if (i >= 0 && gScopes.chans[i].mode == VM_SCOPE_NAMED) {
		sel = -1;
		for (k = 0; k < gScopes.count; k++)
			if (StrCompare(gScopes.names[k], gScopes.chans[i].name) == 0)
				sel = (Int16)(2 + k);
		if (sel < 0 && n < VM_SCOPES_MAX + 2) {    /* no longer in the list: still offer it */
			StrPrintF(gItems[n], "#%s", gScopes.chans[i].name);
			sel = (Int16)n++;
		}
	}
	for (k = 0; k < n; k++)
		gItemPtrs[k] = gItems[k];

	prev = FrmGetActiveForm();
	form = FrmInitForm(ChanScopeForm);
	FrmSetActiveForm(form);
	list = (ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ChanScopeList));
	LstSetListChoices(list, gItemPtrs, n);
	LstSetSelection(list, sel);
	LstMakeItemVisible(list, sel);
	if (FrmDoDialog(form) == ChanScopeOKButton) {
		sel = LstGetSelection(list);
		if (i < 0 && sel > 0) {
			if (gScopes.chanCount >= VM_SCOPE_CHANS) {
				FrmDeleteForm(form);
				FrmSetActiveForm(prev);
				Error("Too many channels with their own scope: set one back to Radio default first.");
				return;
			}
			i = (Int16)gScopes.chanCount++;
			MemMove(gScopes.chans[i].id, c.secret, VM_CHAN_ID_SIZE);
		}
		if (i >= 0) {
			cs = &gScopes.chans[i];
			if (sel <= 0) {            /* default: drop the entry */
				MemMove(cs, cs + 1, (gScopes.chanCount - i - 1) * sizeof(ChanScope));
				gScopes.chanCount--;
			} else if (sel == 1) {
				cs->mode = VM_SCOPE_NONE;
			} else {
				cs->mode = VM_SCOPE_NAMED;
				StrCopy(cs->name, gItems[sel] + 1);   /* skip the '#' */
			}
			SavePrefs();
		}
	}
	FrmDeleteForm(form);
	FrmSetActiveForm(prev);
}

/* ---- Flood Scopes form ---- */

static void FillList(FormType *form)
{
	ListType *list = (ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList));
	UInt16 k;

	for (k = 0; k < gScopes.count; k++) {
		StrPrintF(gItems[k], "#%s", gScopes.names[k]);
		gItemPtrs[k] = gItems[k];
	}
	LstSetListChoices(list, gItemPtrs, gScopes.count);
	LstSetSelection(list, gScopes.count ? 0 : noListSelection);
}

/* The label reserves 30 characters in the .rcp. */
static void SetDefaultLabel(FormType *form, const Char *text)
{
	UInt16 index = FrmGetObjectIndex(form, ScopesDefaultLabel);

	StrNCopy(gDefaultText, text, 30);
	gDefaultText[30] = 0;
	if (FrmVisible(form))
		FrmHideObject(form, index);
	FrmCopyLabel(form, ScopesDefaultLabel, gDefaultText);
	if (FrmVisible(form))
		FrmShowObject(form, index);
}

static void ShowDefault(FormType *form)
{
	Char name[VM_SCOPE_NAME_SIZE], text[VM_SCOPE_NAME_SIZE + 2];

	if (!SessionIsConnected())
		StrCopy(text, "(connect to see)");
	else if (SessionGetDefaultScope(name) != errNone)
		StrCopy(text, "(no reply)");
	else if (!name[0])
		StrCopy(text, "none");
	else
		StrPrintF(text, "#%s", name);
	SetDefaultLabel(form, text);
}

/* Adds a name to the list if it's new and there's room; true if added. */
static Boolean AddName(const Char *name)
{
	UInt16 k;

	name = VmScopeBareName(name);
	for (k = 0; k < gScopes.count; k++)
		if (StrCompare(gScopes.names[k], name) == 0)
			return false;
	if (gScopes.count >= VM_SCOPES_MAX)
		return false;
	StrCopy(gScopes.names[gScopes.count++], name);
	return true;
}

/*
 * Asks each neighbouring repeater (a contact with a 0-hop path) for its
 * regions and adds the public ones. Repeaters only answer direct requests,
 * and the reply comes back zero-hop, so farther ones can't be asked yet.
 */
static void Discover(FormType *form)
{
	Char names[160], name[VM_SCOPE_NAME_SIZE], msg[120], status[VM_NAME_SIZE + 16];
	UInt16 count, i, pos, asked = 0, answered = 0, added = 0, skipped = 0;
	VmContact ct;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first.");
		return;
	}
	count = ContactsCount();
	for (i = 0; i < count; i++) {
		if (!ContactsGet(i, &ct) || ct.type != VM_ADV_TYPE_REPEATER)
			continue;
		if (ct.outPathLen == 0xFF || (ct.outPathLen & 0x3F) != 0)
			continue;                   /* not a neighbour */
		asked++;
		StrPrintF(status, "asking %s...", ct.name);
		SetDefaultLabel(form, status);
		if (SessionAskRegions(ct.pubKey, names, sizeof(names)) != errNone)
			continue;
		answered++;
		pos = 0;
		while (VmNextRegionName((const UInt8 *)names, StrLen(names), &pos, name)) {
			if (!VmScopeNameValid(name)) {
				if (name[0] == '$')
					skipped++;          /* private: no key from the name */
				continue;               /* "*", the wildcard, isn't a scope */
			}
			if (AddName(name))
				added++;
		}
	}
	if (added) {
		SavePrefs();
		FillList(form);
		LstDrawList((ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList)));
	}
	ShowDefault(form);
	if (asked == 0)
		StrCopy(msg, "No neighbouring repeaters in your contacts. Repeaters only answer when they're one hop away.");
	else
		StrPrintF(msg, "%u of %u nearby repeaters answered. %u new scope%s added%s.", answered, asked, added,
		          added == 1 ? "" : "s", skipped ? "; private ($) ones skipped" : "");
	Error(msg);
}

static void AddTyped(FormType *form)
{
	FieldType *field = (FieldType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesField));
	Char *text = FldGetTextPtr(field);
	if (!text || !text[0])
		return;
	if (!VmScopeNameValid(text)) {
		Error(VmScopeBareName(text)[0] == '$'
		      ? "Private ($) scopes need a key from the repeater's owner; only public names work here."
		      : "A scope name is 1-30 letters, digits or symbols, no spaces.");
		return;
	}
	if (!AddName(text)) {
		if (gScopes.count >= VM_SCOPES_MAX)
			Error("The list is full: delete a scope first.");
		return;
	}
	SavePrefs();
	FldDelete(field, 0, FldGetTextLength(field));
	FillList(form);
	LstSetSelection((ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList)), gScopes.count - 1);
	LstDrawList((ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList)));
}

static void DeleteSelected(FormType *form)
{
	ListType *list = (ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList));
	Int16 sel = LstGetSelection(list);

	if (sel < 0 || sel >= (Int16)gScopes.count)
		return;
	MemMove(gScopes.names[sel], gScopes.names[sel + 1], (gScopes.count - sel - 1) * VM_SCOPE_NAME_SIZE);
	gScopes.count--;
	SavePrefs();
	FillList(form);
	LstDrawList(list);
}

static void SetRadioDefault(FormType *form, Boolean clear)
{
	ListType *list = (ListType *)FrmGetObjectPtr(form, FrmGetObjectIndex(form, ScopesList));
	Int16 sel = LstGetSelection(list);
	Char msg[40];
	Err err;

	if (!SessionIsConnected()) {
		Error("Connect to the radio first.");
		return;
	}
	if (!clear && (sel < 0 || sel >= (Int16)gScopes.count)) {
		Error("Select a scope in the list first.");
		return;
	}
	err = SessionSetDefaultScope(clear ? NULL : gScopes.names[sel]);
	if (err != errNone) {
		AppErrorText(err, msg);
		Error(msg);
	}
	ShowDefault(form);
}

void ScopesFormOpen(UInt16 returnForm)
{
	if (returnForm != ScopesForm)
		gReturnForm = returnForm;
	FrmGotoForm(ScopesForm);
}

Boolean ScopesFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		FillList(form);
		ShowDefault(form);
		FrmDrawForm(form);
		FrmSetFocus(form, FrmGetObjectIndex(form, ScopesField));
		return true;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case ScopesAddButton:          AddTyped(form);         return true;
		case ScopesDiscoverButton:     Discover(form);         return true;
		case ScopesDeleteButton:       DeleteSelected(form);   return true;
		case ScopesSetDefaultButton:   SetRadioDefault(form, false); return true;
		case ScopesClearDefaultButton: SetRadioDefault(form, true);  return true;
		case ScopesDoneButton:         FrmGotoForm(gReturnForm); return true;
		}
		break;
	default:
		break;
	}
	return false;
}
