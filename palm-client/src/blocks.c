#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "ui/text.h"
#include "blocks.h"

static void    Load(void) VM_SECT5;
static void    Save(void) VM_SECT5;
static Int16   FindKey(const UInt8 *key) VM_SECT5;
static Int16   FindName(const Char *name) VM_SECT5;
static void    Remove(UInt16 i) VM_SECT5;

#define VM_BLOCKS_PREFS_ID      9
#define VM_BLOCKS_PREFS_VERSION 1

typedef struct {
	UInt8  hasKey;
	UInt8  key[VM_BLOCK_KEY_SIZE];
	Char   name[VM_NAME_SIZE];
} Block;

typedef struct {
	UInt16 version;
	UInt16 count;
	Block  list[VM_BLOCKS_MAX];
} BlocksPrefs;

static BlocksPrefs gBlocks;
static Boolean     gLoaded = false;   /* loaded on first use: the closed-app check has fresh globals */

static void Load(void)
{
	UInt16 size = sizeof(gBlocks);

	if (gLoaded)
		return;
	gLoaded = true;
	if (PrefGetAppPreferences(VM_CREATOR, VM_BLOCKS_PREFS_ID, &gBlocks, &size, true) == noPreferenceFound ||
	    size != sizeof(gBlocks) || gBlocks.version != VM_BLOCKS_PREFS_VERSION || gBlocks.count > VM_BLOCKS_MAX)
		gBlocks.count = 0;
	gBlocks.version = VM_BLOCKS_PREFS_VERSION;
}

static void Save(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_BLOCKS_PREFS_ID, VM_BLOCKS_PREFS_VERSION, &gBlocks, sizeof(gBlocks), true);
}

static Int16 FindKey(const UInt8 *key)
{
	UInt16 i;

	for (i = 0; i < gBlocks.count; i++)
		if (gBlocks.list[i].hasKey && MemCmp(gBlocks.list[i].key, key, VM_BLOCK_KEY_SIZE) == 0)
			return (Int16)i;
	return -1;
}

static Int16 FindName(const Char *name)
{
	UInt16 i;

	for (i = 0; i < gBlocks.count; i++)
		if (gBlocks.list[i].name[0] && StrCompare(gBlocks.list[i].name, name) == 0)
			return (Int16)i;
	return -1;
}

static void Remove(UInt16 i)
{
	if (i >= gBlocks.count)
		return;
	MemMove(&gBlocks.list[i], &gBlocks.list[i + 1], (gBlocks.count - i - 1) * sizeof(Block));
	gBlocks.count--;
	Save();
}

Boolean BlocksMessage(const VmMessage *m)
{
	Char name[VM_NAME_SIZE];
	UInt16 n;

	Load();
	if (gBlocks.count == 0)
		return false;
	if (m->isChannel) {
		/* a channel names its sender only as "Name: " */
		for (n = 0; n < m->textLen && m->text[n] != ':'; n++)
			;
		if (n == m->textLen)
			return false;
		n = TxtUtf8ToPalm(m->text, n, name, sizeof(name) - 1);
		name[n] = 0;
		return n > 0 && FindName(name) >= 0;
	}
	if (m->hasSigner) {
		UInt16 i;

		/* a room post: its author by the first 4 bytes of their key */
		for (i = 0; i < gBlocks.count; i++)
			if (gBlocks.list[i].hasKey && MemCmp(gBlocks.list[i].key, m->signer, 4) == 0)
				return true;
		return false;
	}
	return FindKey(m->senderPrefix) >= 0;
}

Boolean BlocksIsContact(const VmContact *c)
{
	Load();
	return FindKey(c->pubKey) >= 0;
}

Boolean BlocksIsName(const Char *name)
{
	Load();
	return FindName(name) >= 0;
}

/* Asks, then blocks c (its key, and its name for channels) or unblocks it. */
void BlocksToggleContact(const VmContact *c)
{
	Int16 i;
	Block *b;

	Load();
	i = FindKey(c->pubKey);
	if (i < 0)
		i = FindName(c->name);
	if (i >= 0) {
		if (FrmCustomAlert(UnblockAlert, c->name, "", "") == 0)
			Remove((UInt16)i);
		return;
	}
	if (FrmCustomAlert(BlockAlert, c->name, "", "") != 0)
		return;
	if (gBlocks.count >= VM_BLOCKS_MAX) {
		FrmCustomAlert(SettingsErrorAlert, "The block list is full. Unblock someone first (Contacts > Blocked Senders).", "", "");
		return;
	}
	b = &gBlocks.list[gBlocks.count++];
	MemSet(b, sizeof(*b), 0);
	b->hasKey = 1;
	MemMove(b->key, c->pubKey, VM_BLOCK_KEY_SIZE);
	StrNCopy(b->name, c->name, VM_NAME_SIZE - 1);
	Save();
}

Boolean BlocksAskName(const Char *name)
{
	Block *b;

	Load();
	if (FindName(name) >= 0)
		return false;
	if (FrmCustomAlert(BlockAlert, name, "", "") != 0)
		return false;
	if (gBlocks.count >= VM_BLOCKS_MAX) {
		FrmCustomAlert(SettingsErrorAlert, "The block list is full. Unblock someone first (Contacts > Blocked Senders).", "", "");
		return false;
	}
	b = &gBlocks.list[gBlocks.count++];
	MemSet(b, sizeof(*b), 0);
	StrNCopy(b->name, name, VM_NAME_SIZE - 1);
	Save();
	return true;
}

void BlocksManage(UInt16 listId)
{
	FormType *form = FrmGetActiveForm();
	ListType *list = FrmGetObjectPtr(form, FrmGetObjectIndex(form, listId));
	Char **names, (*keyed)[VM_NAME_SIZE + 14];
	UInt16 i;
	Int16 pick;

	Load();
	if (gBlocks.count == 0) {
		FrmCustomAlert(NoteAlert, "No one is blocked. Block someone from their contact (More) or by holding their message.", "", "");
		return;
	}
	/* on the heap: the stack is small */
	names = MemPtrNew(gBlocks.count * sizeof(Char *));
	keyed = MemPtrNew(gBlocks.count * sizeof(*keyed));
	if (!names || !keyed) {
		if (names)
			MemPtrFree(names);
		if (keyed)
			MemPtrFree(keyed);
		return;
	}
	for (i = 0; i < gBlocks.count; i++) {
		names[i] = keyed[i];
		if (gBlocks.list[i].name[0]) {
			StrCopy(keyed[i], gBlocks.list[i].name);
		} else {
			StrCopy(keyed[i], "key ");
			TxtHex(gBlocks.list[i].key, VM_BLOCK_KEY_SIZE, keyed[i] + 4);
		}
	}
	LstSetListChoices(list, names, gBlocks.count);
	LstSetHeight(list, gBlocks.count < 8 ? gBlocks.count : 8);
	pick = LstPopupList(list);
	LstSetListChoices(list, NULL, 0);
	if (pick >= 0 && FrmCustomAlert(UnblockAlert, keyed[pick], "", "") == 0)
		Remove((UInt16)pick);
	MemPtrFree(keyed);
	MemPtrFree(names);
}
