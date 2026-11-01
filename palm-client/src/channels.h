/*
 * The radio's channels (slot, name), cached in a Palm database so the
 * conversation list works offline. Refreshed from GET_CHANNEL on connect.
 */
#ifndef VM_CHANNELS_H
#define VM_CHANNELS_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the second code segment */
#include "protocol.h"

Err     ChannelsOpen(void) VM_SECT2;
void    ChannelsClose(void) VM_SECT2;
UInt16  ChannelsCount(void) VM_SECT2;
Boolean ChannelsGet(UInt16 i, VmChannel *out) VM_SECT2;
Boolean ChannelsFindSlot(UInt8 slot, VmChannel *out) VM_SECT2;

/* The first slot not in use (from the cache), or -1 if all maxChannels are used. */
Int16   ChannelsFreeSlot(UInt8 maxChannels) VM_SECT2;
/* Stores a channel in a slot on the radio (name in the Visor's charset), then refreshes the cache. */
Err     ChannelsSet(UInt8 slot, const Char *name, const UInt8 *secret, UInt8 maxChannels) VM_SECT2;
/* Empties a slot on the radio (empty name, zero secret), then refreshes the cache. */
Err     ChannelsRemove(UInt8 slot, UInt8 maxChannels) VM_SECT2;

/* Asks the radio for slots 0..maxChannels-1 and replaces the cache with the ones in use. */
Err     ChannelsRefresh(UInt8 maxChannels) VM_SECT2;

#endif
