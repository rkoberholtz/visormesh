#include <PalmOS.h>
#include "resources.h"
#include "app.h"
#include "pins.h"

static Int16 FindPin(const UInt8 *key6) VM_SECT4;
static void SavePrefs(void) VM_SECT4;

#define VM_PINS_PREFS_ID      8
#define VM_PINS_PREFS_VERSION 1
#define VM_PINS_MAX           8
#define VM_PIN_ID_SIZE        6    /* the key prefix DMs are known by */

typedef struct {
	UInt16 version;
	UInt16 count;
	UInt8  ids[VM_PINS_MAX][VM_PIN_ID_SIZE];
} PinsPrefs;

static PinsPrefs gPins;

void PinsLoad(void)
{
	UInt16 size = sizeof(gPins);

	if (PrefGetAppPreferences(VM_CREATOR, VM_PINS_PREFS_ID, &gPins, &size, true) == noPreferenceFound ||
	    size != sizeof(gPins) || gPins.version != VM_PINS_PREFS_VERSION || gPins.count > VM_PINS_MAX)
		gPins.count = 0;
	gPins.version = VM_PINS_PREFS_VERSION;
}

static void SavePrefs(void)
{
	PrefSetAppPreferences(VM_CREATOR, VM_PINS_PREFS_ID, VM_PINS_PREFS_VERSION, &gPins, sizeof(gPins), true);
}

static Int16 FindPin(const UInt8 *key6)
{
	UInt16 i;

	for (i = 0; i < gPins.count; i++)
		if (MemCmp(gPins.ids[i], key6, VM_PIN_ID_SIZE) == 0)
			return (Int16)i;
	return -1;
}

Boolean DirectPinned(const UInt8 *key6)
{
	return FindPin(key6) >= 0;
}

Boolean DirectPinToggle(const UInt8 *key6)
{
	Int16 i = FindPin(key6);

	if (i >= 0) {
		MemMove(gPins.ids[i], gPins.ids[i + 1], (gPins.count - i - 1) * VM_PIN_ID_SIZE);
		gPins.count--;
	} else if (gPins.count >= VM_PINS_MAX) {
		FrmCustomAlert(PinAlert, "Too many pinned conversations: unpin one first.", "", "");
		return false;
	} else {
		MemMove(gPins.ids[gPins.count++], key6, VM_PIN_ID_SIZE);
	}
	SavePrefs();
	return true;
}
