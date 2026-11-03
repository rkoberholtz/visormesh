/*
 * Pinned conversations: Home lists them first, in a Pinned section of their
 * own (channels in slot order, then DMs most recent first), and leaves them
 * out of the Channels and Direct sections. Kept by key (a channel's secret, a
 * contact's key prefix), so a pin survives the cache refreshes, follows a
 * channel to another slot and works for senders who aren't contacts.
 */
#ifndef VM_PINS_H
#define VM_PINS_H

#include <PalmOS.h>
#include "sections.h"
#include "convos.h"

void    PinsLoad(void) VM_SECT4;
/* type: VM_CONV_CHANNEL (id: the channel secret) or VM_CONV_DIRECT (id: the key prefix). */
Boolean Pinned(UInt8 type, const UInt8 *id) VM_SECT4;
/* Pins or unpins; false (with an alert) if the list of pins is full. */
Boolean PinToggle(UInt8 type, const UInt8 *id) VM_SECT4;

/*
 * Moves the rows marked pinned into a Pinned section at the top (keeping
 * their order) and drops sections left empty. Returns the new row count.
 */
UInt16  PinsGroup(VmConvRow *rows, UInt16 n, UInt16 max) VM_SECT4;

#endif
