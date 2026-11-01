#ifndef VM_PRESETS_H
#define VM_PRESETS_H

#include "protocol.h"

typedef struct {
	const Char *title;
	UInt32 freqKHz;
	UInt32 bwHz;
	UInt8  sf;
	UInt8  cr;
	UInt8  pathHashSize;    /* bytes; 0 = preset doesn't specify one */
} VmPreset;

extern const VmPreset gPresets[];
extern const UInt16 gPresetCount;

#endif
