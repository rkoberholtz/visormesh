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

/* Asks the radio for slots 0..maxChannels-1 and replaces the cache with the ones in use. */
Err     ChannelsRefresh(UInt8 maxChannels);

#endif
