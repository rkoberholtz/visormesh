#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "pins.h"

static Int16 FindPin(UInt8 type, const UInt8 *id) VM_SECT4;
static void SavePrefs(void) VM_SECT4;
static void MoveRow(VmConvRow *rows, UInt16 from, UInt16 to) VM_SECT4;

#define VM_PINS_PREFS_ID      8
#define VM_PINS_PREFS_VERSION 2
#define VM_PINS_MAX           12
#define VM_PIN_ID_SIZE        6    /* leading bytes of the key */

/* An entry is the conversation type, then the id. */
typedef struct {
	UInt16 version;
	UInt16 count;
	UInt8  ids[VM_PINS_MAX][1 + VM_PIN_ID_SIZE];
} PinsPrefs;

/* Version 1 held DMs only. */
#define VM_PINS_V1_MAX 8
typedef struct {
	UInt16 version;
	UInt16 count;
	UInt8  ids[VM_PINS_V1_MAX][VM_PIN_ID_SIZE];
} PinsPrefsV1;

static PinsPrefs gPins;

void PinsLoad(void)
{
	PinsPrefsV1 old;
	UInt16 size = sizeof(gPins), i;

	if (PrefGetAppPreferences(VM_CREATOR, VM_PINS_PREFS_ID, &gPins, &size, true) != noPreferenceFound &&
	    size == sizeof(gPins) && gPins.version == VM_PINS_PREFS_VERSION && gPins.count <= VM_PINS_MAX)
		return;
	gPins.version = VM_PINS_PREFS_VERSION;
	gPins.count = 0;
	size = sizeof(old);
	if (PrefGetAppPreferences(VM_CREATOR, VM_PINS_PREFS_ID, &old, &size, true) != noPreferenceFound &&
	    size == sizeof(old) && old.version == 1 && old.count <= VM_PINS_V1_MAX) {
		for (i = 0; i < old.count; i++) {
			gPins.ids[i][0] = VM_CONV_DIRECT;
			MemMove(&gPins.ids[i][1], old.ids[i], VM_PIN_ID_SIZE);
		}
		gPins.count = old.count;
		SavePrefs();
	}
}

static void SavePrefs(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_PINS_PREFS_ID, VM_PINS_PREFS_VERSION, &gPins, sizeof(gPins), true);
}

static Int16 FindPin(UInt8 type, const UInt8 *id)
{
	UInt16 i;

	for (i = 0; i < gPins.count; i++)
		if (gPins.ids[i][0] == type && MemCmp(&gPins.ids[i][1], id, VM_PIN_ID_SIZE) == 0)
			return (Int16)i;
	return -1;
}

Boolean Pinned(UInt8 type, const UInt8 *id)
{
	return FindPin(type, id) >= 0;
}

Boolean PinToggle(UInt8 type, const UInt8 *id)
{
	Int16 i = FindPin(type, id);

	if (i >= 0) {
		MemMove(gPins.ids[i], gPins.ids[i + 1], (gPins.count - i - 1) * (1 + VM_PIN_ID_SIZE));
		gPins.count--;
	} else if (gPins.count >= VM_PINS_MAX) {
		FrmCustomAlert(PinAlert, "Too many pinned conversations: unpin one first.", "", "");
		return false;
	} else {
		gPins.ids[gPins.count][0] = type;
		MemMove(&gPins.ids[gPins.count][1], id, VM_PIN_ID_SIZE);
		gPins.count++;
	}
	SavePrefs();
	return true;
}

/* Moves rows[from] to rows[to] (to <= from), shifting the rows between down one. */
static void MoveRow(VmConvRow *rows, UInt16 from, UInt16 to)
{
	VmConvRow tmp;

	if (from == to)
		return;
	tmp = rows[from];
	MemMove(&rows[to + 1], &rows[to], (from - to) * sizeof(VmConvRow));
	rows[to] = tmp;
}

UInt16 PinsGroup(VmConvRow *rows, UInt16 n, UInt16 max)
{
	UInt16 i, k = 0;

	for (i = 0; i < n; i++)
		if (rows[i].kind == VM_ROW_CONV && rows[i].pinned)
			MoveRow(rows, i, k++);
	if (k) {
		/* the section header goes first; with the list full, the last row makes room */
		if (n == max)
			n--;
		MemMove(&rows[1], &rows[0], n * sizeof(VmConvRow));
		MemSet(&rows[0], sizeof(VmConvRow), 0);
		rows[0].kind = VM_ROW_SECTION;
		StrCopy(rows[0].name, "Pinned");
		n++;
	}
	/* drop a section header with nothing under it */
	for (i = 0; i < n; ) {
		if (rows[i].kind == VM_ROW_SECTION && (i + 1 == n || rows[i + 1].kind == VM_ROW_SECTION)) {
			MemMove(&rows[i], &rows[i + 1], (n - i - 1) * sizeof(VmConvRow));
			n--;
		} else {
			i++;
		}
	}
	return n;
}
