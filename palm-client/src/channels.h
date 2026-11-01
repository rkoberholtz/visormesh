/*
 * The radio's channels (slot, name), cached in a Palm database so the
 * conversation list works offline. Refreshed from GET_CHANNEL on connect.
 */
#ifndef VM_CHANNELS_H
#define VM_CHANNELS_H

#include <PalmOS.h>
#include "protocol.h"

Err     ChannelsOpen(void);
void    ChannelsClose(void);
UInt16  ChannelsCount(void);
Boolean ChannelsGet(UInt16 i, VmChannel *out);
Boolean ChannelsFindSlot(UInt8 slot, VmChannel *out);

/* The first slot not in use (from the cache), or -1 if all maxChannels are used. */
Int16   ChannelsFreeSlot(UInt8 maxChannels);
/* Stores a channel in a slot on the radio (name in the Visor's charset), then refreshes the cache. */
Err     ChannelsSet(UInt8 slot, const Char *name, const UInt8 *secret, UInt8 maxChannels);
/* Empties a slot on the radio (empty name, zero secret), then refreshes the cache. */
Err     ChannelsRemove(UInt8 slot, UInt8 maxChannels);

/* Asks the radio for slots 0..maxChannels-1 and replaces the cache with the ones in use. */
Err     ChannelsRefresh(UInt8 maxChannels);

#endif
